#include "search.h"
#include <algorithm>
#include <cstring>

Search::Search(TranspositionTable &tt, Evaluation &eval)
    : tt_(&tt), eval_(&eval)
{
    clearHistory();
}

SearchResult Search::searchRoot(const Board &board, int depth, SearchInfo &info)
{
    SearchResult result;
    result.best_move = Move::NO_MOVE;
    result.score = -MATE_VALUE;
    result.nodes = 0;
    result.pv.clear();

    Board board_copy = board;
    chess::Movelist moves;
    chess::movegen::legalmoves(moves, board_copy);

    if (moves.empty())
    {
        // Checkmate or stalemate
        result.score = board_copy.inCheck() ? -MATE_VALUE + 0 : 0;
        return result;
    }

    // Order root moves
    orderMoves(moves, board_copy, Move::NO_MOVE, 0);

    int alpha = -MATE_VALUE;
    int beta = MATE_VALUE;
    bool pv_found = false;

    for (size_t i = 0; i < moves.size() && !info.should_stop(); ++i)
    {
        Move move = moves[i];
        board_copy.makeMove(move);

        int score;
        PVLine pv;

        if (!pv_found)
        {
            // First move - search with full window
            score = -search(board_copy, depth - 1, 1, -beta, -alpha, pv, info);
        }
        else
        {
            // Try null window search first
            score = -search(board_copy, depth - 1, 1, -alpha - 1, -alpha, pv, info);

            if (score > alpha && score < beta)
            {
                // Re-search with full window
                pv.clear();
                score = -search(board_copy, depth - 1, 1, -beta, -alpha, pv, info);
            }
        }

        board_copy.unmakeMove(move);

        if (info.should_stop())
        {
            break;
        }

        if (score > alpha)
        {
            alpha = score;
            result.best_move = move;
            result.score = score;

            // Update PV
            result.pv.clear();
            result.pv.push(move);
            for (int j = 0; j < pv.count; ++j)
            {
                result.pv.push(pv.moves[j]);
            }

            pv_found = true;

            // Update transposition table
            tt_->store(board.hash(), depth, score, TT_EXACT, move);
        }
    }

    result.nodes = info.nodes;
    return result;
}

int Search::search(Board &board, int depth, int ply, int alpha, int beta,
                   PVLine &pv, SearchInfo &info, bool null_move_allowed)
{
    pv.clear();

    if (info.should_stop())
    {
        return 0;
    }

    info.nodes++;

    // Mate distance pruning
    mateDistancePruning(alpha, beta, ply);
    if (alpha >= beta)
    {
        return alpha;
    }

    // Check for draw by repetition or 50-move rule
    if (ply > 0 && (board.isRepetition() || board.isHalfMoveDraw()))
    {
        return 0;
    }

    bool in_check = board.inCheck();
    bool pv_node = (beta - alpha > 1);

    // Quiescence search at leaf nodes
    if (depth <= 0)
    {
        return quiescence(board, ply, alpha, beta, info);
    }

    // Transposition table lookup
    Move hash_move = Move::NO_MOVE;
    TTEntry *entry = tt_->probe(board.hash());
    if (entry && entry->hash == board.hash())
    {
        hash_move = entry->move;

        if (entry->depth >= depth && !pv_node)
        {
            int tt_score = entry->score;

            // Adjust mate scores
            if (tt_score > MATE_IN_MAX_PLY)
            {
                tt_score -= ply;
            }
            else if (tt_score < -MATE_IN_MAX_PLY)
            {
                tt_score += ply;
            }

            switch (entry->flag)
            {
            case TT_EXACT:
                return tt_score;
            case TT_LOWER:
                if (tt_score >= beta)
                    return tt_score;
                break;
            case TT_UPPER:
                if (tt_score <= alpha)
                    return tt_score;
                break;
            }
        }
    }

    // Null move pruning
    if (null_move_allowed && !pv_node && !in_check && depth >= 3 && canDoNullMove(board))
    {
        int R = 3 + depth / 6; // Adaptive reduction

        board.makeNullMove();
        int null_score = -search(board, depth - R - 1, ply + 1, -beta, -beta + 1, pv, info, false);
        board.unmakeNullMove();

        if (null_score >= beta)
        {
            return null_score;
        }
    }

    // Generate moves
    chess::Movelist moves;
    chess::movegen::legalmoves(moves, board);

    if (moves.empty())
    {
        return in_check ? -MATE_VALUE + ply : 0;
    }

    // Order moves
    orderMoves(moves, board, hash_move, ply);

    int best_score = -MATE_VALUE;
    Move best_move = Move::NO_MOVE;
    TTFlag flag = TT_UPPER;
    bool legal_move_found = false;

    for (size_t i = 0; i < moves.size() && !info.should_stop(); ++i)
    {
        Move move = moves[i];

        // Make move
        board.makeMove(move);
        legal_move_found = true;

        // Extensions
        int extension = getExtension(move, board, in_check);
        int new_depth = depth - 1 + extension;

        int score;
        PVLine child_pv;

        // Late Move Reductions (LMR)
        if (i >= 4 && depth >= 3 && !in_check && !board.inCheck() &&
            move.typeOf() == Move::NORMAL && !board.isCapture(move))
        {

            int reduction = getReduction(move, depth, static_cast<int>(i), pv_node);

            // Search with reduction
            score = -search(board, new_depth - reduction, ply + 1, -alpha - 1, -alpha,
                            child_pv, info);

            // If LMR search fails high, re-search without reduction
            if (score > alpha)
            {
                score = -search(board, new_depth, ply + 1, -alpha - 1, -alpha,
                                child_pv, info);
            }
        }
        else if (i == 0 || pv_node)
        {
            // First move or PV node - search with full window
            score = -search(board, new_depth, ply + 1, -beta, -alpha, child_pv, info);
        }
        else
        {
            // Try zero-window search first
            score = -search(board, new_depth, ply + 1, -alpha - 1, -alpha, child_pv, info);

            // If it fails high, re-search with full window
            if (score > alpha && score < beta)
            {
                child_pv.clear();
                score = -search(board, new_depth, ply + 1, -beta, -alpha, child_pv, info);
            }
        }

        board.unmakeMove(move);

        if (info.should_stop())
        {
            break;
        }

        if (score > best_score)
        {
            best_score = score;
            best_move = move;

            if (score > alpha)
            {
                alpha = score;
                flag = TT_EXACT;

                // Update PV
                pv.clear();
                pv.push(move);
                for (int j = 0; j < child_pv.count; ++j)
                {
                    pv.push(child_pv.moves[j]);
                }

                if (score >= beta)
                {
                    flag = TT_LOWER;

                    // Update history and killer moves for quiet moves
                    if (!board.isCapture(move))
                    {
                        updateHistory(move, board.sideToMove(), depth);
                        updateKillers(move, ply);
                    }

                    break; // Beta cutoff
                }
            }
        }
    }

    if (!legal_move_found)
    {
        return in_check ? -MATE_VALUE + ply : 0;
    }

    // Store in transposition table
    int store_score = best_score;
    if (store_score > MATE_IN_MAX_PLY)
    {
        store_score += ply;
    }
    else if (store_score < -MATE_IN_MAX_PLY)
    {
        store_score -= ply;
    }

    tt_->store(board.hash(), depth, store_score, flag, best_move);

    return best_score;
}

int Search::quiescence(Board &board, int ply, int alpha, int beta, SearchInfo &info)
{
    if (info.should_stop())
    {
        return 0;
    }

    info.nodes++;

    // Stand pat
    int stand_pat = eval_->evaluate(board);

    if (stand_pat >= beta)
    {
        return beta;
    }

    if (stand_pat > alpha)
    {
        alpha = stand_pat;
    }

    // Generate capturing moves
    chess::Movelist moves;
    chess::movegen::legalmoves<chess::movegen::MoveGenType::CAPTURE>(moves, board);

    // Order captures by MVV-LVA
    std::sort(moves.begin(), moves.end(), [&](const Move &a, const Move &b)
              { return getMoveScore(a, board, Move::NO_MOVE, ply) >
                       getMoveScore(b, board, Move::NO_MOVE, ply); });

    for (const auto &move : moves)
    {
        if (info.should_stop())
        {
            break;
        }

        // Delta pruning - don't consider captures that can't improve alpha
        if (!board.inCheck())
        {
            int captured_value = 0;
            if (board.at(move.to()) != Piece::NONE)
            {
                switch (board.at(move.to()).type())
                {
                case static_cast<int>(chess::PieceType::PAWN):
                    captured_value = 100;
                    break;
                case static_cast<int>(chess::PieceType::KNIGHT):
                    captured_value = 320;
                    break;
                case static_cast<int>(chess::PieceType::BISHOP):
                    captured_value = 330;
                    break;
                case static_cast<int>(chess::PieceType::ROOK):
                    captured_value = 500;
                    break;
                case static_cast<int>(chess::PieceType::QUEEN):
                    captured_value = 900;
                    break;
                default:
                    break;
                }
            }

            if (stand_pat + captured_value + 200 < alpha)
            {
                continue;
            }
        }

        board.makeMove(move);
        int score = -quiescence(board, ply + 1, -beta, -alpha, info);
        board.unmakeMove(move);

        if (score >= beta)
        {
            return beta;
        }

        if (score > alpha)
        {
            alpha = score;
        }
    }

    return alpha;
}

void Search::orderMoves(chess::Movelist &moves, const Board &board, Move hash_move, int ply)
{
    std::sort(moves.begin(), moves.end(), [&](const Move &a, const Move &b)
              { return getMoveScore(a, board, hash_move, ply) > getMoveScore(b, board, hash_move, ply); });
}

int Search::getMoveScore(const Move &move, const Board &board, Move hash_move, int ply)
{
    // Hash move gets highest priority
    if (move == hash_move)
    {
        return 1000000;
    }

    int score = 0;

    // Captures
    if (board.isCapture(move))
    {
        auto victim = board.at(move.to()).type();
        auto attacker = board.at(move.from()).type();

        if (victim != PieceType::NONE && attacker != PieceType::NONE)
        {
            score = 10000 + MVV_LVA[static_cast<int>(victim)][static_cast<int>(attacker)];
        }
    }
    // Killer moves
    else if (ply < MAX_PLY && (move.move() == killer_moves_[ply][0] ||
                               move.move() == killer_moves_[ply][1]))
    {
        score = 9000;
    }
    // History heuristic
    else
    {
        int from = move.from().index();
        int to = move.to().index();
        int color = static_cast<int>(board.sideToMove());
        score = history_[color][from][to];
    }

    return score;
}

int Search::getExtension(const Move &move, const Board &board, bool in_check)
{
    // Check extension
    if (board.inCheck())
    {
        return 1;
    }

    // Promotion extension
    if (move.typeOf() == Move::PROMOTION)
    {
        return 1;
    }

    return 0;
}

int Search::getReduction(const Move &move, int depth, int move_count, bool pv_node)
{
    if (pv_node)
    {
        return std::max(0, depth / 6 + move_count / 8 - 1);
    }
    else
    {
        return std::max(0, depth / 4 + move_count / 6);
    }
}

bool Search::canDoNullMove(const Board &board)
{
    // Don't do null move if only king and pawns remain
    return board.hasNonPawnMaterial(board.sideToMove());
}

void Search::mateDistancePruning(int &alpha, int &beta, int ply)
{
    int mate_alpha = -MATE_VALUE + ply;
    int mate_beta = MATE_VALUE - ply - 1;

    if (alpha < mate_alpha)
        alpha = mate_alpha;
    if (beta > mate_beta)
        beta = mate_beta;
}

void Search::updateHistory(const Move &move, Color color, int depth)
{
    int from = move.from().index();
    int to = move.to().index();
    int color_idx = static_cast<int>(color);

    history_[color_idx][from][to] += depth * depth;

    // Prevent overflow
    if (history_[color_idx][from][to] > 10000)
    {
        for (int i = 0; i < 64; ++i)
        {
            for (int j = 0; j < 64; ++j)
            {
                history_[color_idx][i][j] /= 2;
            }
        }
    }
}

void Search::updateKillers(const Move &move, int ply)
{
    if (ply < MAX_PLY)
    {
        if (killer_moves_[ply][0] != static_cast<int>(move.move()))
        {
            killer_moves_[ply][1] = killer_moves_[ply][0];
            killer_moves_[ply][0] = move.move();
        }
    }
}

void Search::clearHistory()
{
    std::memset(history_, 0, sizeof(history_));
    std::memset(killer_moves_, 0, sizeof(killer_moves_));
}