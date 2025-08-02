#include "engine.h"
#include <iostream>
#include <sstream>

ChessEngine::ChessEngine()
    : board_(chess::constants::STARTPOS),
      hash_size_mb_(DEFAULT_HASH_SIZE_MB),
      threads_(1),
      searching_(false)
{
    initializeComponents();
}

ChessEngine::~ChessEngine()
{
    stopSearch();
}

void ChessEngine::initializeComponents()
{
    tt_ = std::make_unique<TranspositionTable>(hash_size_mb_);
    evaluation_ = std::make_unique<Evaluation>();
    search_ = std::make_unique<Search>(*tt_, *evaluation_);
    book_ = std::make_unique<OpeningBook>();

    if (!book_path_.empty())
    {
        book_->loadFromFile(book_path_);
    }
}

void ChessEngine::newGame()
{
    board_.setFen(chess::constants::STARTPOS);
    tt_->clear();
    search_info_.reset();
}

void ChessEngine::setPosition(const std::string &fen, const std::vector<std::string> &moves)
{
    if (fen.empty() || fen == "startpos")
    {
        board_.setFen(chess::constants::STARTPOS);
    }
    else
    {
        board_.setFen(fen);
    }

    // Apply moves
    for (const auto &moveStr : moves)
    {
        chess::Movelist legal_moves;
        chess::movegen::legalmoves(legal_moves, board_);

        bool move_found = false;
        for (const auto &move : legal_moves)
        {
            std::string move_string = static_cast<std::string>(move.from()) +
                                      static_cast<std::string>(move.to());

            // Handle promotion
            if (move.typeOf() == Move::PROMOTION)
            {
                char promo_char;
                switch (static_cast<int>(move.promotionType()))
                {
                case static_cast<int>(PieceType::QUEEN):
                    promo_char = 'q';
                    break;
                case static_cast<int>(PieceType::ROOK):
                    promo_char = 'r';
                    break;
                case static_cast<int>(PieceType::BISHOP):
                    promo_char = 'b';
                    break;
                case static_cast<int>(PieceType::KNIGHT):
                    promo_char = 'n';
                    break;
                default:
                    promo_char = 'q';
                    break;
                }
                move_string += promo_char;
            }

            if (move_string == moveStr)
            {
                board_.makeMove(move);
                move_found = true;
                break;
            }
        }

        if (!move_found)
        {
            std::cerr << "Invalid move: " << moveStr << std::endl;
            break;
        }
    }
}

Move ChessEngine::search(int depth, int movetime, int wtime, int btime,
                         int winc, int binc, bool infinite)
{
    // Check opening book first
    if (book_ && book_->isLoaded())
    {
        auto book_move = book_->getMove(board_);
        if (book_move != Move::NO_MOVE)
        {
            return book_move;
        }
    }

    search_info_.reset();

    // Calculate time limit
    if (movetime > 0)
    {
        search_info_.time_limit = Duration(movetime);
    }
    else if (!infinite && (wtime > 0 || btime > 0))
    {
        int our_time = (board_.sideToMove() == Color::WHITE) ? wtime : btime;
        int our_inc = (board_.sideToMove() == Color::WHITE) ? winc : binc;

        // Simple time management: use 1/30 of remaining time + increment
        int time_to_use = our_time / 30 + our_inc / 2;
        search_info_.time_limit = Duration(std::max(100, time_to_use));
    }

    searching_ = true;

    // Perform iterative deepening search
    Move best_move = Move::NO_MOVE;
    int max_depth = (depth > 0) ? depth : MAX_DEPTH;

    for (int d = 1; d <= max_depth && !search_info_.should_stop(); ++d)
    {
        auto result = search_->searchRoot(board_, d, search_info_);

        if (!search_info_.should_stop())
        {
            best_move = result.best_move;
            search_info_.depth = d;

            // Print search info
            auto elapsed = std::chrono::duration_cast<Duration>(
                std::chrono::steady_clock::now() - search_info_.start_time);

            std::cout << "info depth " << d
                      << " score cp " << result.score
                      << " nodes " << result.nodes
                      << " time " << elapsed.count()
                      << " pv";

            for (int i = 0; i < result.pv.count; ++i)
            {
                std::cout << " " << static_cast<std::string>(result.pv.moves[i].from())
                          << static_cast<std::string>(result.pv.moves[i].to());
                if (result.pv.moves[i].typeOf() == Move::PROMOTION)
                {
                    char promo_char;
                    switch (static_cast<int>(result.pv.moves[i].promotionType()))
                    {
                    case static_cast<int>(PieceType::QUEEN):
                        promo_char = 'q';
                        break;
                    case static_cast<int>(PieceType::ROOK):
                        promo_char = 'r';
                        break;
                    case static_cast<int>(PieceType::BISHOP):
                        promo_char = 'b';
                        break;
                    case static_cast<int>(PieceType::KNIGHT):
                        promo_char = 'n';
                        break;
                    default:
                        promo_char = 'q';
                        break;
                    }
                    std::cout << promo_char;
                }
            }
            std::cout << std::endl;
        }

        // Stop if we found a mate
        if (std::abs(result.score) > MATE_IN_MAX_PLY)
        {
            break;
        }
    }

    searching_ = false;
    return best_move;
}

void ChessEngine::stopSearch()
{
    search_info_.stopped = true;
    searching_ = false;
    if (search_thread_.joinable())
    {
        search_thread_.join();
    }
}

void ChessEngine::setHashSize(int mb)
{
    hash_size_mb_ = mb;
    tt_ = std::make_unique<TranspositionTable>(mb);
    search_->setTranspositionTable(*tt_);
}

void ChessEngine::setBookPath(const std::string &path)
{
    book_path_ = path;
    if (book_)
    {
        book_->loadFromFile(path);
    }
}

void ChessEngine::setTablebases(const std::string &path)
{
    tb_path_ = path;
    // TODO: Initialize Syzygy tablebases
}

void ChessEngine::setThreads(int threads)
{
    threads_ = threads;
    // TODO: Implement multi-threading
}

int ChessEngine::evaluate()
{
    return evaluation_->evaluate(board_);
}

std::string ChessEngine::getAnalysis()
{
    std::ostringstream oss;
    oss << "Position evaluation: " << evaluate() << " centipawns\n";
    oss << "Material: " << evaluation_->getMaterialBalance(board_) << "\n";
    oss << "Phase: " << (evaluation_->isEndgame(board_) ? "Endgame" : "Middle game") << "\n";
    return oss.str();
}