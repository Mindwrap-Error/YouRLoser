#include "uci.h"
#include <iostream>
#include <thread>

UCIHandler::UCIHandler(ChessEngine &engine) : engine_(engine) {}

void UCIHandler::processCommand(const std::string &command)
{
    std::istringstream iss(command);
    std::string token;
    iss >> token;

    if (token == "uci")
    {
        handleUCI();
    }
    else if (token == "isready")
    {
        handleIsReady();
    }
    else if (token == "ucinewgame")
    {
        handleUCINewGame();
    }
    else if (token == "position")
    {
        handlePosition(iss);
    }
    else if (token == "go")
    {
        handleGo(iss);
    }
    else if (token == "stop")
    {
        handleStop();
    }
    else if (token == "quit")
    {
        handleQuit();
    }
    else if (token == "setoption")
    {
        handleSetOption(iss);
    }
    else if (token == "perft")
    {
        handlePerft(iss);
    }
    else if (token == "eval")
    {
        handleEval();
    }
    // Ignore unknown commands
}

void UCIHandler::handleUCI()
{
    std::cout << "id name ChessEngine 1.0" << std::endl;
    std::cout << "id author AI Assistant" << std::endl;

    // Options
    std::cout << "option name Hash type spin default 64 min 1 max 4096" << std::endl;
    std::cout << "option name Threads type spin default 1 min 1 max 128" << std::endl;
    std::cout << "option name BookPath type string default " << std::endl;
    std::cout << "option name SyzygyPath type string default " << std::endl;

    std::cout << "uciok" << std::endl;
}

void UCIHandler::handleIsReady()
{
    std::cout << "readyok" << std::endl;
}

void UCIHandler::handleUCINewGame()
{
    engine_.newGame();
}

void UCIHandler::handlePosition(std::istringstream &iss)
{
    std::string token;
    iss >> token;

    std::string fen;
    std::vector<std::string> moves;

    if (token == "startpos")
    {
        fen = "startpos";
        iss >> token; // consume "moves" if present
    }
    else if (token == "fen")
    {
        // Read FEN string
        std::string fen_part;
        while (iss >> fen_part && fen_part != "moves")
        {
            if (!fen.empty())
                fen += " ";
            fen += fen_part;
        }
        token = fen_part; // should be "moves" or empty
    }

    // Read moves
    if (token == "moves")
    {
        std::string move;
        while (iss >> move)
        {
            moves.push_back(move);
        }
    }

    engine_.setPosition(fen, moves);
}

void UCIHandler::handleGo(std::istringstream &iss)
{
    std::string token;

    int depth = 0;
    int movetime = 0;
    int wtime = 0, btime = 0;
    int winc = 0, binc = 0;
    bool infinite = false;

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

    // Start search in separate thread
    std::thread search_thread([&]()
                              {
        Move best_move = engine_.search(depth, movetime, wtime, btime, winc, binc, infinite);
        
        std::cout << "bestmove " << moveToString(best_move) << std::endl; });

    search_thread.detach();
}

void UCIHandler::handleStop()
{
    engine_.stopSearch();
}

void UCIHandler::handleQuit()
{
    engine_.stopSearch();
    std::exit(0);
}

void UCIHandler::handleSetOption(std::istringstream &iss)
{
    std::string token;
    iss >> token; // "name"

    if (token != "name")
        return;

    iss >> token; // option name
    std::string option_name = token;

    iss >> token; // "value"
    if (token != "value")
        return;

    if (option_name == "Hash")
    {
        int hash_size;
        iss >> hash_size;
        engine_.setHashSize(hash_size);
    }
    else if (option_name == "Threads")
    {
        int threads;
        iss >> threads;
        engine_.setThreads(threads);
    }
    else if (option_name == "BookPath")
    {
        std::string path;
        iss >> path;
        engine_.setBookPath(path);
    }
    else if (option_name == "SyzygyPath")
    {
        std::string path;
        iss >> path;
        engine_.setTablebases(path);
    }
}

void UCIHandler::handlePerft(std::istringstream &iss)
{
    int depth;
    if (iss >> depth)
    {
        // TODO: Implement perft for move generation testing
        std::cout << "Perft not implemented yet" << std::endl;
    }
}

void UCIHandler::handleEval()
{
    int eval = engine_.evaluate();
    std::cout << "eval: " << eval << " (from "
              << (engine_.getBoard().sideToMove() == Color::WHITE ? "white" : "black")
              << "'s perspective)" << std::endl;
    std::cout << engine_.getAnalysis() << std::endl;
}

std::vector<std::string> UCIHandler::split(const std::string &str, char delimiter)
{
    std::vector<std::string> tokens;
    std::stringstream ss(str);
    std::string token;
    while (std::getline(ss, token, delimiter))
    {
        tokens.push_back(token);
    }
    return tokens;
}

std::string UCIHandler::moveToString(const Move &move)
{
    if (move == Move::NO_MOVE)
    {
        return "0000";
    }

    std::string result = static_cast<std::string>(move.from()) +
                         static_cast<std::string>(move.to());

    // Handle promotion
    if (move.typeOf() == Move::PROMOTION)
    {
        switch (static_cast<int>(move.promotionType()))
        {
        case static_cast<int>(PieceType::QUEEN):
            result += 'q';
            break;
        case static_cast<int>(PieceType::ROOK):
            result += 'r';
            break;
        case static_cast<int>(PieceType::BISHOP):
            result += 'b';
            break;
        case static_cast<int>(PieceType::KNIGHT):
            result += 'n';
            break;
        default:
            result += 'q';
            break;
        }
    }

    return result;
}