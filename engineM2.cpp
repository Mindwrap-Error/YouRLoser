#include <iostream>
#include <vector>
#include <algorithm>
#include <climits>
#include <chrono>
#include <thread>
#include <sstream>
#include <string>
#include <unordered_map>
#include <fstream>
#include <random>
#include "./lib/chess.hpp"
#include "./Fathom/src/tbprobe.h"

using std::cin;
using std::cout;
using std::endl;
using std::getline;
using std::max;
using std::min;
using std::pair;
using std::sort;
using std::string;
using std::vector;
using namespace chess;

struct TTEntry
{
    int value;
    int depth;
    int flag; // 0 = EXACT, 1 = LOWER_BOUND, 2 = UPPER_BOUND
    Move bestMove;
    bool valid;

    TTEntry() : value(0), depth(0), flag(0), bestMove(), valid(false) {}
};

class EndgameDatabase
{
private:
    bool tb_available;
    int tb_pieces;

public:
    EndgameDatabase() : tb_available(false), tb_pieces(0) {}

    bool initTablebase(const std::string &path)
    {
        if (path.empty())
            return false;

        tb_available = tb_init("./syzygy");
        if (tb_available)
        {
            tb_pieces = TB_LARGEST;
            std::cout << "Tablebase initialized with " << tb_pieces << " pieces" << std::endl;
        }
        return tb_available;
    }

    int probeWDL(const Board &board)
        const
    {
        if (!tb_available || board.occ().count() > tb_pieces)
        {
            return -1;
        }

        // Convert board to fathom format
        unsigned int white = board.us(Color::WHITE).getBits();
        unsigned int black = board.us(Color::BLACK).getBits();

        unsigned int kings = board.pieces(PieceType::KING).getBits();
        unsigned int queens = board.pieces(PieceType::QUEEN).getBits();
        unsigned int rooks = board.pieces(PieceType::ROOK).getBits();
        unsigned int bishops = board.pieces(PieceType::BISHOP).getBits();
        unsigned int knights = board.pieces(PieceType::KNIGHT).getBits();
        unsigned int pawns = board.pieces(PieceType::PAWN).getBits();

        unsigned int ep = (board.enpassantSq() != Square::NO_SQ) ? (1ULL << board.enpassantSq().index()) : 0;

        bool turn = (board.sideToMove() == Color::WHITE);

        return tb_probe_wdl(white, black, kings, queens, rooks, bishops, knights, pawns, 0, 0, ep, turn);
    }

    Move probeDTZ(const Board &board)
        const
    {
        if (!tb_available || board.occ().count() > tb_pieces)
        {
            return Move();
        }

        // Similar conversion as above
        unsigned int white = board.us(Color::WHITE).getBits();
        unsigned int black = board.us(Color::BLACK).getBits();

        unsigned int kings = board.pieces(PieceType::KING).getBits();
        unsigned int queens = board.pieces(PieceType::QUEEN).getBits();
        unsigned int rooks = board.pieces(PieceType::ROOK).getBits();
        unsigned int bishops = board.pieces(PieceType::BISHOP).getBits();
        unsigned int knights = board.pieces(PieceType::KNIGHT).getBits();
        unsigned int pawns = board.pieces(PieceType::PAWN).getBits();

        unsigned int ep = (board.enpassantSq() != Square::NO_SQ) ? (1ULL << board.enpassantSq().index()) : 0;

        bool turn = (board.sideToMove() == Color::WHITE);

        unsigned int result = tb_probe_root(white, black, kings, queens, rooks, bishops, knights, pawns, 0, 0, ep, turn, nullptr);

        if (result == TB_RESULT_FAILED)
        {
            return Move();
        }

        unsigned int from = TB_GET_FROM(result);
        unsigned int to = TB_GET_TO(result);
        unsigned int promotes = TB_GET_PROMOTES(result);

        if (promotes != TB_PROMOTES_NONE)
        {
            PieceType promo;
            switch (promotes)
            {
            case TB_PROMOTES_QUEEN:
                promo = PieceType::QUEEN;
                break;
            case TB_PROMOTES_ROOK:
                promo = PieceType::ROOK;
                break;
            case TB_PROMOTES_BISHOP:
                promo = PieceType::BISHOP;
                break;
            case TB_PROMOTES_KNIGHT:
                promo = PieceType::KNIGHT;
                break;
            default:
                return Move();
            }
            return Move::make<Move::PROMOTION>(Square(from), Square(to), promo);
        }

        return Move::make<Move::NORMAL>(Square(from), Square(to));
    }

    bool isAvailable() const { return false; }
    int getMaxPieces() const { return 0; }
};

class OpeningBook
{
private:
    struct BookEntry
    {
        Move move;
        uint16_t weight;
        uint32_t learn_count;
        uint32_t learn_points;
    };

    std::unordered_map<uint64_t, std::vector<BookEntry>> book_positions;
    std::mt19937 rng;
    bool use_book;
    int book_depth_limit;

public:
    OpeningBook() : rng(std::random_device{}()), use_book(true), book_depth_limit(20) {}

    bool loadBook(const std::string &filename)
    {
        std::ifstream file(filename, std::ios::binary);
        if (!file.is_open())
            return false;

        struct PolyglotEntry
        {
            uint64_t key;
            uint16_t move;
            uint16_t weight;
            uint32_t learn;
        };

        PolyglotEntry entry;
        while (file.read(reinterpret_cast<char *>(&entry), sizeof(entry)))
        {
            // Convert from big-endian if needed
            entry.key = __builtin_bswap64(entry.key);
            entry.move = __builtin_bswap16(entry.move);
            entry.weight = __builtin_bswap16(entry.weight);
            entry.learn = __builtin_bswap32(entry.learn);

            BookEntry bookEntry;
            bookEntry.move = polyglotToMove(entry.move);
            bookEntry.weight = entry.weight;
            bookEntry.learn_count = entry.learn & 0xFFFF;
            bookEntry.learn_points = entry.learn >> 16;

            book_positions[entry.key].push_back(bookEntry);
        }

        return true;
    }

    Move probeBook(const Board &board)
    {
        if (!use_book || board.fullMoveNumber() > book_depth_limit)
        {
            return Move();
        }

        uint64_t key = board.hash();
        auto it = book_positions.find(key);
        if (it == book_positions.end())
        {
            return Move();
        }

        const auto &entries = it->second;
        if (entries.empty())
            return Move();

        // Calculate total weight
        uint32_t total_weight = 0;
        for (const auto &entry : entries)
        {
            total_weight += entry.weight;
        }

        if (total_weight == 0)
            return Move();

        // Select move based on weight
        uint32_t random_weight = rng() % total_weight;
        uint32_t current_weight = 0;

        for (const auto &entry : entries)
        {
            current_weight += entry.weight;
            if (random_weight < current_weight)
            {
                // Validate move is legal
                Movelist legal_moves;
                movegen::legalmoves(legal_moves, board);
                for (const auto &legal_move : legal_moves)
                {
                    if (legal_move == entry.move)
                    {
                        return entry.move;
                    }
                }
            }
        }

        return Move();
    }

private:
    Move polyglotToMove(uint16_t poly_move)
    {
        int from = (poly_move >> 6) & 0x3F;
        int to = poly_move & 0x3F;
        int promotion = (poly_move >> 12) & 0x7;

        if (promotion != 0)
        {
            // Fix this line - use the correct enum values
            PieceType promo_piece;
            switch (promotion)
            {
            case 1:
                promo_piece = PieceType::KNIGHT;
                break;
            case 2:
                promo_piece = PieceType::BISHOP;
                break;
            case 3:
                promo_piece = PieceType::ROOK;
                break;
            case 4:
                promo_piece = PieceType::QUEEN;
                break;
            default:
                promo_piece = PieceType::QUEEN;
                break;
            }
            return Move::make<Move::PROMOTION>(Square(from), Square(to), promo_piece);
        }

        return Move::make<Move::NORMAL>(Square(from), Square(to));
    }
};

class SimpleTranspositionTable
{
private:
    std::vector<TTEntry> table;
    size_t size;

public:
    static constexpr int EXACT = 0;
    static constexpr int LOWER_BOUND = 1;
    static constexpr int UPPER_BOUND = 2;

    SimpleTranspositionTable(size_t sizeInMB)
    {
        size = (sizeInMB * 1024 * 1024) / sizeof(TTEntry);
        table.resize(size);
    }

    // Simplified probe without key comparison
    bool probe(int depth, int alpha, int beta, int &value, Move &bestMove, size_t index)
    {
        TTEntry &entry = table[index % size];
        if (entry.valid && entry.depth >= depth)
        {
            bestMove = entry.bestMove;
            if (entry.flag == EXACT)
            {
                value = entry.value;
                return true;
            }
            else if (entry.flag == LOWER_BOUND && entry.value >= beta)
            {
                value = entry.value;
                return true;
            }
            else if (entry.flag == UPPER_BOUND && entry.value <= alpha)
            {
                value = entry.value;
                return true;
            }
        }
        return false;
    }

    void store(int value, int depth, int flag, const Move &bestMove, size_t index)
    {
        TTEntry &entry = table[index % size];
        if (!entry.valid || entry.depth <= depth)
        {
            entry.value = value;
            entry.depth = depth;
            entry.flag = flag;
            entry.bestMove = bestMove;
            entry.valid = true;
        }
    }

    void clear()
    {
        for (auto &entry : table)
        {
            entry.valid = false;
        }
    }
};

class ChessEngine
{
public:
    OpeningBook opening_book;
    EndgameDatabase endgame_db;
    bool use_opening_book;
    bool use_endgame_db;

    void setOption(const std::string &name, const std::string &value)
    {
        if (name == "OwnBook")
        {
            use_opening_book = (value == "true");
        }
        else if (name == "SyzygyPath")
        {
            endgame_db.initTablebase(value);
        }
    }

    ChessEngine() : board(), use_opening_book(true), use_endgame_db(true)
    {
        // Initialize systems
        opening_book.loadBook("book.bin");     // Polyglot opening book
        endgame_db.initTablebase("./syzygy/"); // Syzygy tablebase path
    }

    Move findBestMove(int depth = 4)
    {
        // Check opening book first
        if (use_opening_book && board.fullMoveNumber() <= 20)
        {
            Move book_move = opening_book.probeBook(board);
            if (book_move != Move())
            {
                cout << "info string Book move found: " << uci::moveToUci(book_move) << endl;
                return book_move;
            }
        }

        // Check endgame tablebase
        if (use_endgame_db && board.occ().count() <= endgame_db.getMaxPieces())
        {
            Move tb_move = endgame_db.probeDTZ(board);
            if (tb_move != Move())
            {
                cout << "info string Tablebase move found: " << uci::moveToUci(tb_move) << endl;
                return tb_move;
            }
        }

        // Fall back to normal search
        return findBestMoveSearch(depth);
    }
    Board board;
    SimpleTranspositionTable tt{64};

    int evaluateKingSafety(Color color) const
    {
        Square kingSquare = board.kingSq(color);
        int safety = 0;

        File kingFile = kingSquare.file();
        Rank kingRank = kingSquare.rank();

        // Pawn shield evaluation
        for (int fileOffset = -1; fileOffset <= 1; ++fileOffset)
        {
            int newFile = static_cast<int>(kingFile) + fileOffset;
            int newRank = static_cast<int>(kingRank) + (color == Color::WHITE ? 1 : -1);

            if (newFile >= 0 && newFile <= 7 && newRank >= 0 && newRank <= 7)
            {
                Square shieldSquare = Square(newRank * 8 + newFile);
                Piece piece = board.at(shieldSquare);
                if (piece.type() == PieceType::PAWN && piece.color() == color)
                {
                    safety += 15;
                }
                else
                {
                    safety -= 10;
                }
            }
        }
        return safety;
    }

    static constexpr int PIECE_VALUES[6] = {100, 320, 330, 500, 900, 20000}; // P N B R Q K

    static constexpr int PAWN_TABLE[64] = {
        0, 0, 0, 0, 0, 0, 0, 0,
        50, 50, 50, 50, 50, 50, 50, 50,
        10, 10, 20, 30, 30, 20, 10, 10,
        5, 5, 10, 25, 25, 10, 5, 5,
        0, 0, 0, 20, 20, 0, 0, 0,
        -5, -5, -10, -25, -25, -10, -5, -5,
        -10, -10, -20, -30, -30, -20, -10, -10,
        -50, -50, -50, -50, -50, -50, -50, -50};

    static constexpr int KNIGHT_TABLE[64] = {
        -50, -40, -30, -30, -30, -30, -40, -50,
        -40, -20, 0, 0, 0, 0, -20, -40,
        -30, 0, 10, 15, 15, 10, 0, -30,
        -30, 5, 15, 20, 20, 15, 5, -30,
        -30, 0, 15, 20, 20, 15, 0, -30,
        -40, -20, 0, 5, 5, 0, -20, -40,
        -50, -40, -30, -30, -30, -30, -40, -50,
        -50, -50, -50, -50, -50, -50, -50, -50};

    int getPieceValue(PieceType piece) const
    {
        return PIECE_VALUES[static_cast<int>(piece)];
    }

    int evaluatePosition() const
    {
        if (use_endgame_db && board.occ().count() <= endgame_db.getMaxPieces())
        {
            int wdl = endgame_db.probeWDL(board);
            if (wdl != TB_RESULT_FAILED)
            {
                if (wdl > 0)
                    return 10000; // Win
                else if (wdl < 0)
                    return -10000; // Loss
                else
                    return 0; // Draw
            }
        }
        int score = 0;

        // Material evaluation
        for (Square sq = Square::SQ_A1; sq <= Square::SQ_H8; ++sq)
        {
            Piece piece = board.at(sq);
            if (piece != Piece::NONE)
            {
                int pieceValue = getPieceValue(piece.type());

                if (piece.type() == PieceType::PAWN)
                {
                    int tableIndex = sq.index();
                    if (piece.color() == Color::BLACK)
                    {
                        tableIndex = 63 - tableIndex;
                    }
                    pieceValue += PAWN_TABLE[tableIndex];
                }
                else if (piece.type() == PieceType::KNIGHT)
                {
                    int tableIndex = sq.index();
                    if (piece.color() == Color::BLACK)
                    {
                        tableIndex = 63 - tableIndex;
                    }
                    pieceValue += KNIGHT_TABLE[tableIndex];
                }

                if (piece.color() == Color::WHITE)
                {
                    score += pieceValue;
                }
                else
                {
                    score -= pieceValue;
                }
            }
        }

        // Mobility Bonus
        Movelist moves;
        movegen::legalmoves(moves, board);
        if (board.sideToMove() == Color::WHITE)
        {
            score += moves.size() * 2;
        }
        else
        {
            score -= moves.size() * 2;
        }

        score += evaluateKingSafety(board.sideToMove());
        return board.sideToMove() == Color::WHITE ? score : -score;
    }

    int scoreMoveForOrdering(const Move &move) const
    {
        int score = 0;

        // 1. Evaluate captures with SEE
        if (board.at(move.to()) != Piece::NONE || move.typeOf() == Move::ENPASSANT)
        {
            int seeScore = staticExchangeEvaluation(move);
            if (seeScore > 0)
            {
                score += 10000 + seeScore;
            }
            else if (seeScore == 0)
            {
                score += 5000;
            }
            else
            {
                score -= 1000;
            }
        }

        // 2. Promotions
        if (move.typeOf() == Move::PROMOTION)
        {
            score += 9000;
        }

        // 3. Checks
        Board tempBoard = board;
        tempBoard.makeMove(move);
        if (tempBoard.inCheck())
        {
            score += 800;
        }

        // 4. Castling
        if (move.typeOf() == Move::CASTLING)
        {
            score += 600;
        }

        // 5. Central control
        Square to = move.to();
        if (to == Square::SQ_E4 || to == Square::SQ_E5 ||
            to == Square::SQ_D4 || to == Square::SQ_D5)
        {
            score += 50;
        }

        return score;
    }

    int staticExchangeEvaluation(const Move &move) const
    {
        Square to = move.to();
        Square from = move.from();

        int capturedValue = 0;
        if (board.at(to) != Piece::NONE)
        {
            capturedValue = getPieceValue(board.at(to).type());
        }
        else if (move.typeOf() == Move::ENPASSANT)
        {
            capturedValue = getPieceValue(PieceType::PAWN);
        }
        else
        {
            return 0;
        }

        int attackerValue = getPieceValue(board.at(from).type());

        if (capturedValue >= attackerValue)
        {
            return capturedValue - attackerValue;
        }

        Board tempBoard = board;
        tempBoard.makeMove(move);

        Movelist counterMoves;
        movegen::legalmoves(counterMoves, tempBoard);
        bool isDefended = false;

        for (const auto &counterMove : counterMoves)
        {
            if (counterMove.to() == to)
            {
                isDefended = true;
                break;
            }
        }

        if (isDefended)
        {
            return capturedValue - attackerValue;
        }
        else
        {
            return capturedValue;
        }
    }

    // Simple hash function for position (without Zobrist)
    size_t simplePositionHash() const
    {
        size_t hash = 0;

        // Hash based on piece positions (simple but effective)
        for (Square sq = Square::SQ_A1; sq <= Square::SQ_H8; ++sq)
        {
            Piece piece = board.at(sq);
            if (piece != Piece::NONE)
            {
                hash ^= std::hash<int>{}(static_cast<int>(piece.internal()) * 64 + sq.index());
            }
        }

        // Add side to move
        hash ^= std::hash<int>{}(static_cast<int>(board.sideToMove()));

        // Add castling rights
        hash ^= std::hash<string>{}(board.getCastleString());

        // Add en passant square
        if (board.enpassantSq() != Square::NO_SQ)
        {
            hash ^= std::hash<int>{}(board.enpassantSq().index() + 1000);
        }

        return hash;
    }

public:
    ChessEngine(const string &fen) : board(fen) {}

    bool isGameOver()
    {
        return board.isGameOver().second != GameResult::NONE;
    }

    pair<GameResultReason, GameResult> getGameStatus()
    {
        return board.isGameOver();
    }

    Color getCurrentPlayer() const
    {
        return board.sideToMove();
    }

    Movelist getValidMoves() const
    {
        Movelist moves;
        movegen::legalmoves(moves, board);
        orderMoves(moves);
        return moves;
    }

    void getValidMoves(Movelist &moves) const
    {
        moves.clear();
        movegen::legalmoves(moves, board);
        orderMoves(moves);
    }

    bool hasNonPawnMaterial() const
    {
        for (Square sq = Square::SQ_A1; sq <= Square::SQ_H8; ++sq)
        {
            Piece piece = board.at(sq);
            if (piece != Piece::NONE && piece.type() != PieceType::PAWN && piece.type() != PieceType::KING)
            {
                return true;
            }
        }
        return false;
    }

    void orderMoves(Movelist &moves) const
    {
        sort(moves.begin(), moves.end(), [this](const Move &a, const Move &b)
             { return scoreMoveForOrdering(a) > scoreMoveForOrdering(b); });
    }

    // Minimax with alpha-beta pruning (simplified without Zobrist)
    int minimax(int depth, int alpha, int beta, bool maximizingPlayer)
    {
        // Simple position hash for TT
        size_t positionHash = simplePositionHash();
        int ttValue;
        Move ttMove;

        // Simplified TT probe
        if (tt.probe(depth, alpha, beta, ttValue, ttMove, positionHash))
        {
            return ttValue;
        }

        // Null move pruning
        if (depth >= 3 && !board.inCheck() && hasNonPawnMaterial())
        {
            board.makeNullMove();
            int nullScore = -minimax(depth - 3, -beta, -beta + 1, !maximizingPlayer);
            board.unmakeNullMove();

            if (nullScore >= beta)
            {
                return beta;
            }
        }

        if (depth == 0 || isGameOver())
        {
            int eval = evaluatePosition();
            tt.store(eval, depth, SimpleTranspositionTable::EXACT, Move(), positionHash);
            return eval;
        }

        Movelist moves;
        getValidMoves(moves);

        // Try TT move first
        if (ttMove != Move())
        {
            auto it = std::find(moves.begin(), moves.end(), ttMove);
            if (it != moves.end())
            {
                std::swap(*moves.begin(), *it);
            }
        }

        Move bestMove;
        int originalAlpha = alpha;

        if (maximizingPlayer)
        {
            int maxEval = INT_MIN;
            for (const auto &move : moves)
            {
                board.makeMove(move);
                int eval = minimax(depth - 1, alpha, beta, false);
                board.unmakeMove(move);

                if (eval > maxEval)
                {
                    maxEval = eval;
                    bestMove = move;
                }

                alpha = max(alpha, eval);
                if (beta <= alpha)
                {
                    break;
                }
            }

            int flag = (maxEval <= originalAlpha) ? SimpleTranspositionTable::UPPER_BOUND : (maxEval >= beta) ? SimpleTranspositionTable::LOWER_BOUND
                                                                                                              : SimpleTranspositionTable::EXACT;
            tt.store(maxEval, depth, flag, bestMove, positionHash);
            return maxEval;
        }
        else
        {
            int minEval = INT_MAX;
            for (const auto &move : moves)
            {
                board.makeMove(move);
                int eval = minimax(depth - 1, alpha, beta, true);
                board.unmakeMove(move);

                if (eval < minEval)
                {
                    minEval = eval;
                    bestMove = move;
                }

                beta = min(beta, eval);
                if (beta <= alpha)
                {
                    break;
                }
            }

            int flag = (minEval >= originalAlpha) ? SimpleTranspositionTable::LOWER_BOUND : (minEval <= beta) ? SimpleTranspositionTable::UPPER_BOUND
                                                                                                              : SimpleTranspositionTable::EXACT;
            tt.store(minEval, depth, flag, bestMove, positionHash);
            return minEval;
        }
    }

    Move findBestMoveSearch(int depth = 4)
    {
        Move bestMove;
        int bestValue = INT_MIN;
        bool maximizingPlayer = (board.sideToMove() == Color::WHITE);

        Movelist moves;
        getValidMoves(moves);

        for (const auto &move : moves)
        {
            board.makeMove(move);

            int moveValue;
            if (maximizingPlayer)
            {
                moveValue = minimax(depth - 1, INT_MIN, INT_MAX, false);
            }
            else
            {
                moveValue = minimax(depth - 1, INT_MIN, INT_MAX, true);
                moveValue = -moveValue;
            }

            board.unmakeMove(move);

            if (moveValue > bestValue)
            {
                bestValue = moveValue;
                bestMove = move;
            }
        }

        return bestMove;
    }

    int quiescenceSearch(int alpha, int beta)
    {
        int standPat = evaluatePosition();
        if (standPat >= beta)
            return beta;
        if (standPat > alpha)
            alpha = standPat;

        Movelist captures;
        movegen::legalmoves(captures, board);

        for (const auto &move : captures)
        {
            if (staticExchangeEvaluation(move) < 0)
                continue;

            board.makeMove(move);
            int score = -quiescenceSearch(-beta, -alpha);
            board.unmakeMove(move);

            if (score >= beta)
                return beta;
            if (score > alpha)
                alpha = score;
        }

        return alpha;
    }

    Move findBestMoveIterativeDeepening(int maxDepth = 10, int timeLimitMs = 5000)
    {
        Move bestMove;
        auto startTime = std::chrono::steady_clock::now();

        for (int depth = 1; depth <= maxDepth; depth++)
        {
            auto currentTime = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(currentTime - startTime).count();

            if (elapsed > timeLimitMs)
                break;

            bestMove = findBestMove(depth);
            cout << "info depth " << depth
                 << " time " << elapsed << " pv " << uci::moveToUci(bestMove) << endl;
        }

        return bestMove;
    }

    void makeMove(const Move &move)
    {
        board.makeMove(move);
    }

    void unmakeMove(const Move &move)
    {
        board.unmakeMove(move);
    }

    Board &getBoard() { return board; }

    void playEngineMove(int depth = 4)
    {
        if (isGameOver())
        {
            auto [reason, result] = getGameStatus();
            cout << "Game Over! Result: " << static_cast<int>(result) << endl;
            return;
        }

        cout << "Engine is thinking..." << endl;
        Move bestMove = findBestMove(depth);
        makeMove(bestMove);
        cout << "Engine played: " << uci::moveToUci(bestMove) << endl;
    }

    void printMoveOrdering() const
    {
        auto moves = getValidMoves();
        cout << "Ordered Moves:" << endl;
        for (size_t i = 0; i < moves.size() && i < 10; i++)
        {
            const auto &move = moves[i];
            int score = scoreMoveForOrdering(move);
            cout << i + 1 << ". " << uci::moveToUci(move) << " (score: " << score << ")" << endl;
        }
    }

    // Display the current board position
    void printBoard() const
    {
        cout << "\n   +---+---+---+---+---+---+---+---+" << endl;
        for (int rank = 7; rank >= 0; rank--)
        {
            cout << " " << (rank + 1) << " |";
            for (int file = 0; file < 8; file++)
            {
                Square sq = Square(rank * 8 + file);
                Piece piece = board.at(sq);
                char pieceChar = ' ';

                if (piece != Piece::NONE)
                {
                    // Replace switch with if-else chain
                    if (piece.type() == PieceType::PAWN)
                    {
                        pieceChar = (piece.color() == Color::WHITE) ? 'P' : 'p';
                    }
                    else if (piece.type() == PieceType::KNIGHT)
                    {
                        pieceChar = (piece.color() == Color::WHITE) ? 'N' : 'n';
                    }
                    else if (piece.type() == PieceType::BISHOP)
                    {
                        pieceChar = (piece.color() == Color::WHITE) ? 'B' : 'b';
                    }
                    else if (piece.type() == PieceType::ROOK)
                    {
                        pieceChar = (piece.color() == Color::WHITE) ? 'R' : 'r';
                    }
                    else if (piece.type() == PieceType::QUEEN)
                    {
                        pieceChar = (piece.color() == Color::WHITE) ? 'Q' : 'q';
                    }
                    else if (piece.type() == PieceType::KING)
                    {
                        pieceChar = (piece.color() == Color::WHITE) ? 'K' : 'k';
                    }
                    else
                    {
                        pieceChar = ' ';
                    }
                }
                cout << " " << pieceChar << " |";
            }
            cout << endl
                 << "   +---+---+---+---+---+---+---+---+" << endl;
        }
        cout << "     a   b   c   d   e   f   g   h" << endl
             << endl;
        cout << "Turn: " << (board.sideToMove() == Color::WHITE ? "White" : "Black") << endl;
    }

    // Parse a move from algebraic notation (e.g., "e2e4")
    Move parseMove(const string &moveStr)
    {
        try
        {
            return uci::uciToMove(board, moveStr);
        }
        catch (...)
        {
            return Move();
        }
    }

    bool isValidMove(const Move &move)
    {
        if (move == Move())
            return false;

        Movelist legalMoves;
        movegen::legalmoves(legalMoves, board);

        for (const auto &legalMove : legalMoves)
        {
            if (legalMove == move)
            {
                return true;
            }
        }
        return false;
    }
};

class UCIHandler
{
private:
    ChessEngine engine;
    bool isRunning;

public:
    UCIHandler() : isRunning(false) {}

    void run()
    {
        isRunning = true;
        string line;
        while (isRunning && getline(cin, line))
        {
            processCommand(line);
        }
    }

private:
    void processCommand(const string line)
    {
        std::istringstream iss(line);
        string command;
        iss >> command;

        if (command == "uci")
        {
            handleUCI();
        }
        else if (command == "isready")
        {
            handleIsReady();
        }
        else if (command == "ucinewgame")
        {
            handleNewGame();
        }
        else if (command == "position")
        {
            handlePosition(iss);
        }
        else if (command == "go")
        {
            handleGo(iss);
        }
        else if (command == "stop")
        {
            handleStop();
        }
        else if (command == "quit")
        {
            handleQuit();
        }
        else if (command == "setoption")
        {
            handleSetOption(iss);
        }
    }

    void handleUCI()
    {
        cout << "id name YouRLoser_M2" << endl;
        cout << "id author Raffolk" << endl;
        cout << "option name Hash type spin default 64 min 1 max 1024" << endl;
        cout << "option name OwnBook type check default true" << endl;
        cout << "option name BookFile type string default book.bin" << endl;
        cout << "option name SyzygyPath type string default" << endl;
        cout << "option name SyzygyProbeDepth type spin default 1 min 1 max 100" << endl;
        cout << "option name SyzygyProbeLimit type spin default 7 min 0 max 7" << endl;
        cout << "option name Hash type spin default 64 min 1 max 1024" << endl;
        cout << "option name Threads type spin default 1 min 1 max 16" << endl;
        cout << "option name Move Overhead type spin default 10 min 0 max 5000" << endl;
        cout << "option name Skill Level type spin default 20 min 0 max 20" << endl;
        cout << "option name UCI_LimitStrength type check default false" << endl;
        cout << "option name UCI_Elo type spin default 1500 min 1000 max 3000" << endl;
        cout << "option name UCI_ShowWDL type string default" << endl;
        cout << "option name SyzygyPath type string default" << endl;
        cout << "uciok" << endl;
    }

    void handleSetOption(std::istringstream &iss)
    {
        std::string name, value;
        std::string token;

        while (iss >> token)
        {
            if (token == "name")
            {
                std::getline(iss, name);
                name = name.substr(1); // Remove leading space
            }
            else if (token == "value")
            {
                std::getline(iss, value);
                value = value.substr(1); // Remove leading space
            }
        }

        engine.setOption(name, value);
    }

    void handleIsReady()
    {
        cout << "readyok" << endl;
    }

    void handleNewGame()
    {
        engine = ChessEngine();
    }

    void handlePosition(std::istringstream &iss)
    {
        string token;
        iss >> token;

        if (token == "startpos")
        {
            engine = ChessEngine();
            iss >> token;
            if (token == "moves")
            {
                string moveStr;
                while (iss >> moveStr)
                {
                    Move move = uci::uciToMove(engine.getBoard(), moveStr);
                    engine.makeMove(move);
                }
            }
        }
        else if (token == "fen")
        {
            string fen;
            string fenPart;
            for (int i = 0; i < 6 && iss >> fenPart; i++)
            {
                if (i > 0)
                    fen += " ";
                fen += fenPart;
            }

            engine = ChessEngine(fen);
            iss >> token;
            if (token == "moves")
            {
                string moveStr;
                while (iss >> moveStr)
                {
                    Move move = uci::uciToMove(engine.getBoard(), moveStr);
                    engine.makeMove(move);
                }
            }
        }
    }

    void handleGo(std::istringstream &iss)
    {
        int depth = 14;
        int movetime = 0;
        int wtime = 0, btime = 0;
        int winc = 0, binc = 0;
        bool infinite = false;
        string token;

        while (iss >> token)
        {
            if (token == "depth")
            {
                iss >> depth;
            }
            else if (token == "movetime")
            {
                iss >> movetime;
            }
            else if (token == "wtime")
            {
                iss >> wtime;
            }
            else if (token == "btime")
            {
                iss >> btime;
            }
            else if (token == "winc")
            {
                iss >> winc;
            }
            else if (token == "binc")
            {
                iss >> binc;
            }
            else if (token == "infinite")
            {
                infinite = true;
            }
        }

        std::thread searchThread([this, depth]()
                                 { searchAndSendBestMove(depth); });
        searchThread.detach();
    }

    void searchAndSendBestMove(int depth)
    {
        Move bestMove = engine.findBestMove(depth);
        cout << "bestmove " << uci::moveToUci(bestMove) << endl;
    }

    void handleStop()
    {
        return;
    }

    void handleQuit()
    {
        isRunning = false;
    }
};

// Game modes implementation
void engineVsHuman()
{
    ChessEngine engine;
    cout << "\n=== Engine vs Human ===" << endl;
    cout << "Choose your color (w/b): ";
    char colorChoice;
    cin >> colorChoice;
    cin.ignore(); // Clear the input buffer

    bool humanIsWhite = (colorChoice == 'w' || colorChoice == 'W');

    cout << "\nYou are playing as " << (humanIsWhite ? "White" : "Black") << endl;
    cout << "Enter moves in UCI format (e.g., e2e4, g1f3)" << endl;
    cout << "Type 'quit' to exit, 'help' for move suggestions\n"
         << endl;

    while (!engine.isGameOver())
    {
        engine.printBoard();

        if ((engine.getCurrentPlayer() == Color::WHITE && humanIsWhite) ||
            (engine.getCurrentPlayer() == Color::BLACK && !humanIsWhite))
        {
            // Human's turn
            cout << "Your move: ";
            string input;
            getline(cin, input);

            if (input == "quit")
            {
                break;
            }
            else if (input == "help")
            {
                Movelist moves = engine.getValidMoves();
                cout << "Legal moves: ";
                for (size_t i = 0; i < moves.size() && i < 10; i++)
                {
                    cout << uci::moveToUci(moves[i]) << " ";
                }
                cout << endl;
                continue;
            }

            Move move = engine.parseMove(input);
            if (engine.isValidMove(move))
            {
                engine.makeMove(move);
                cout << "You played: " << input << endl;
            }
            else
            {
                cout << "Invalid move! Try again." << endl;
                continue;
            }
        }
        else
        {
            // Engine's turn
            cout << "Engine is thinking..." << endl;
            auto start = std::chrono::steady_clock::now();
            Move bestMove = engine.findBestMove(5); // Depth 5 for reasonable speed
            auto end = std::chrono::steady_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

            engine.makeMove(bestMove);
            cout << "Engine played: " << uci::moveToUci(bestMove)
                 << " (took " << duration.count() << "ms)" << endl;
        }
    }

    // Game over
    auto [reason, result] = engine.getGameStatus();
    engine.printBoard();

    cout << "\nGame Over!" << endl;
    switch (result)
    {
    case GameResult::WIN:
        cout << "Checkmate! " << (engine.getCurrentPlayer() == Color::WHITE ? "Black" : "White") << " wins!" << endl;
        break;
    case GameResult::DRAW:
        cout << "Draw!" << endl;
        break;
    default:
        cout << "Game ended." << endl;
        break;
    }
}

void engineVsEngine()
{
    ChessEngine whiteEngine;
    ChessEngine blackEngine;

    cout << "\n=== Engine vs Engine ===" << endl;
    cout << "White Engine depth: ";
    int whiteDepth;
    cin >> whiteDepth;
    cout << "Black Engine depth: ";
    int blackDepth;
    cin >> blackDepth;
    cin.ignore(); // Clear input buffer

    cout << "\nPress Enter after each move to continue, or type 'auto' for automatic play: ";
    string input;
    getline(cin, input);
    bool autoPlay = (input == "auto");

    int moveCount = 0;
    const int maxMoves = 100; // Prevent infinite games

    while (!whiteEngine.isGameOver() && moveCount < maxMoves)
    {
        whiteEngine.printBoard();

        ChessEngine *currentEngine;
        int currentDepth;
        string engineName;

        if (whiteEngine.getCurrentPlayer() == Color::WHITE)
        {
            currentEngine = &whiteEngine;
            currentDepth = whiteDepth;
            engineName = "White Engine";
        }
        else
        {
            currentEngine = &blackEngine;
            currentDepth = blackDepth;
            engineName = "Black Engine";
            // Sync black engine with white engine's position
            blackEngine = whiteEngine;
        }

        cout << "\n"
             << engineName << " is thinking (depth " << currentDepth << ")..." << endl;
        auto start = std::chrono::steady_clock::now();
        Move bestMove = currentEngine->findBestMove(currentDepth);
        auto end = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

        // Apply move to both engines
        whiteEngine.makeMove(bestMove);
        if (currentEngine == &blackEngine)
        {
            blackEngine.makeMove(bestMove);
        }

        cout << engineName << " played: " << uci::moveToUci(bestMove)
             << " (took " << duration.count() << "ms)" << endl;

        moveCount++;

        if (!autoPlay && !whiteEngine.isGameOver())
        {
            cout << "Press Enter to continue...";
            cin.ignore();
        }
        else if (autoPlay)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(500)); // Brief pause for readability
        }
    }

    // Game over
    whiteEngine.printBoard();
    auto [reason, result] = whiteEngine.getGameStatus();

    cout << "\nGame Over after " << moveCount << " moves!" << endl;

    if (moveCount >= maxMoves)
    {
        cout << "Game ended due to move limit." << endl;
    }
    else
    {
        switch (result)
        {
        case GameResult::WIN:
            cout << "Checkmate! " << (whiteEngine.getCurrentPlayer() == Color::WHITE ? "Black" : "White") << " wins!" << endl;
            break;
        case GameResult::DRAW:
            cout << "Draw!" << endl;
            switch (reason)
            {
            case GameResultReason::STALEMATE:
                cout << "Reason: Stalemate" << endl;
                break;
            case GameResultReason::INSUFFICIENT_MATERIAL:
                cout << "Reason: Insufficient material" << endl;
                break;
            case GameResultReason::THREEFOLD_REPETITION:
                cout << "Reason: Threefold repetition" << endl;
                break;
            case GameResultReason::FIFTY_MOVE_RULE:
                cout << "Reason: Fifty move rule" << endl;
                break;
            default:
                cout << "Reason: Unknown" << endl;
                break;
            }
            break;
        default:
            cout << "Game ended." << endl;
            break;
        }
    }
}

void moveOrderingAnalysis()
{
    ChessEngine engine;
    cout << "\n=== Move Ordering Analysis ===" << endl;
    cout << "Enter a FEN position (or press Enter for starting position): ";
    string fen;
    getline(cin, fen);

    if (!fen.empty())
    {
        try
        {
            engine = ChessEngine(fen);
        }
        catch (...)
        {
            cout << "Invalid FEN, using starting position." << endl;
            engine = ChessEngine();
        }
    }

    engine.printBoard();
    engine.printMoveOrdering();

    cout << "\nEvaluation: " << engine.evaluatePosition() << endl;
}

int main()
{
    cout << "=== Chess Engine Test Suite ===" << endl;
    cout << "Choose test mode:" << endl;
    cout << "1. Engine vs Human" << endl;
    cout << "2. Engine vs Engine" << endl;
    cout << "3. Move Ordering Analysis" << endl;
    cout << "4. UCI Mode" << endl;
    cout << "Enter choice (1-4): ";

    int choice;
    cin >> choice;
    cin.ignore(); // Clear the input buffer

    switch (choice)
    {
    case 1:
        engineVsHuman();
        break;
    case 2:
        engineVsEngine();
        break;
    case 3:
        moveOrderingAnalysis();
        break;
    case 4:
    {
        UCIHandler uciHandler;
        uciHandler.run();
    }
    break;
    default:
        cout << "Invalid choice!" << endl;
        break;
    }

    return 0;
}
