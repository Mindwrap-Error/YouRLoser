#include <iostream>
#include <sstream>
#include <climits>
#include <chrono>
#include <thread>
#include <algorithm>
#include <memory>
#include <unordered_map>
#include "./lib/chess.hpp"
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
    uint64_t zobristKey;
    int value;
    int depth;
    int flag; // 0 = EXACT, 1 = LOWER_BOUND, 2 = UPPER_BOUND
    Move bestMove;

    TTEntry() : zobristKey(0), value(0), depth(0), flag(0), bestMove() {}
};

class TranspositionTable
{
private:
    std::vector<TTEntry> table;
    size_t size;

public:
    static constexpr int EXACT = 0;
    static constexpr int LOWER_BOUND = 1;
    static constexpr int UPPER_BOUND = 2;
    TranspositionTable(size_t sizeInMB)
    {
        size = (sizeInMB * 1024 * 1024) / sizeof(TTEntry);
        table.resize(size);
    }

    bool probe(uint64_t key, int depth, int alpha, int beta, int &value, Move &bestMove)
    {
        TTEntry &entry = table[key % size];

        if (entry.zobristKey == key && entry.depth >= depth)
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

    void store(uint64_t key, int value, int depth, int flag, const Move &bestMove)
    {
        TTEntry &entry = table[key % size];
        if (entry.zobristKey != key || entry.depth <= depth)
        {
            entry.zobristKey = key;
            entry.value = value;
            entry.depth = depth;
            entry.flag = flag;
            entry.bestMove = bestMove;
        }
    }
};

class ChessEngine
{
private:
    Board board;
    TranspositionTable tt{64};

    int evaluateKingSafety(Color color) const
    {
        Square kingSquare = board.kingSq(color);
        int safety = 0;

        // Use Square's built-in methods
        File kingFile = kingSquare.file();
        Rank kingRank = kingSquare.rank();

        // Pawn shield evaluation
        for (int fileOffset = -1; fileOffset <= 1; ++fileOffset)
        {
            // Calculate shield square properly
            int newFile = static_cast<int>(kingFile) + fileOffset;
            int newRank = static_cast<int>(kingRank) + (color == Color::WHITE ? 1 : -1);

            // Bounds checking
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

    // Here is the the piece-squre table for positional evaluation
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
                        tableIndex = 63 - tableIndex; // Flip for black pawns
                    }
                    pieceValue += PAWN_TABLE[tableIndex];
                }
                else if (piece.type() == PieceType::KNIGHT)
                {
                    int tableIndex = sq.index();
                    if (piece.color() == Color::BLACK)
                    {
                        tableIndex = 63 - tableIndex; // Flip for black knights
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

        // 3. Checks - USE BOARD COPY instead of const_cast
        Board tempBoard = board; // Create a copy
        tempBoard.makeMove(move);
        if (tempBoard.inCheck())
        {
            score += 800;
        }
        // No need to unmake - tempBoard goes out of scope

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

        // Get captured piece value
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
            return 0; // No capture
        }

        // Get attacking piece value
        int attackerValue = getPieceValue(board.at(from).type());

        // Simple SEE: if we capture something more valuable, it's likely good
        if (capturedValue >= attackerValue)
        {
            return capturedValue - attackerValue;
        }

        // For complex exchanges, use board copy
        Board tempBoard = board;
        tempBoard.makeMove(move);

        // Check if the square is defended
        // This is a simplified check - you can make it more sophisticated
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
            return capturedValue - attackerValue; // Assume we lose our piece
        }
        else
        {
            return capturedValue; // Safe capture
        }
    }

    // Recursive helper for SEE calculation
    int seeRecursive(Board &board, Square target, Color sideToMove, int threshold) const
    {
        // Find the least valuable attacker
        Move bestCapture = findLeastValuableAttacker(board, target, sideToMove);

        if (bestCapture == Move())
        {
            return 0; // No more attackers
        }

        // Get the value of the capturing piece
        int captureValue = getPieceValue(board.at(bestCapture.from()).type());

        // Make the capture
        board.makeMove(bestCapture);

        // Recursively calculate the opponent's best response
        int gain = threshold - seeRecursive(board, target, !sideToMove, captureValue);

        // Undo the move
        board.unmakeMove(bestCapture);

        // Return the maximum of 0 and the gain (can choose not to recapture)
        return std::max(0, gain);
    }

    // Find the least valuable piece that can capture on the target square
    Move findLeastValuableAttacker(const Board &board, Square target, Color color) const
    {
        Move bestMove;
        int lowestValue = INT_MAX;

        // Generate all legal moves for the side to move
        Movelist moves;
        movegen::legalmoves(moves, board);

        for (const auto &move : moves)
        {
            // Check if this move captures on the target square
            if (move.to() == target)
            {
                int pieceValue = getPieceValue(board.at(move.from()).type());

                // Find the least valuable attacker
                if (pieceValue < lowestValue)
                {
                    lowestValue = pieceValue;
                    bestMove = move;
                }
            }
        }

        return bestMove;
    }

public:
    ChessEngine() : board() {}

    ChessEngine(const string &fen) : board(fen) {}

    Move findBestMoveUCI(int depth = 14)
    {
        Move bestMove;
        int bestValue = INT_MIN;
        bool maximizing = (board.sideToMove() == Color::WHITE);

        Movelist moves = getValidMoves();

        for (size_t i = 0; i < moves.size(); i++)
        {
            const auto &move = moves[i];
            board.makeMove(move);

            int moveValue;
            if (maximizing)
            {
                moveValue = minimax(depth - 1, INT_MIN, INT_MAX, false);
            }
            else
            {
                moveValue = minimax(depth - 1, INT_MIN, INT_MAX, true);
                moveValue = -moveValue;
            }

            board.unmakeMove(move);

            cout << "info depth " << depth
                 << " seldepth " << depth
                 << " score cp " << moveValue
                 << " nodes " << (i + 1) * 1000 // An approximation
                 << " pv " << uci::moveToUci(move) << endl;

            if (moveValue > bestValue)
            {
                bestValue = moveValue;
                bestMove = move;
            }
        }
        return bestMove;
    }

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

    // **CORE MINIMAX WITH ALPHA-BETA PRUNING IMPLEMENTATION**
    int minimax(int depth, int alpha, int beta, bool maximizingPlayer)
    {
        uint64_t positionKey = board.hash();
        int ttValue;
        Move ttMove;

        // Probe transposition table
        if (tt.probe(positionKey, depth, alpha, beta, ttValue, ttMove))
        {
            return ttValue;
        }

        // Null move pruning (your existing code)
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
            tt.store(positionKey, eval, depth, TranspositionTable::EXACT, Move());
            return eval;
        }

        Movelist moves;
        getValidMoves(moves);

        // Try TT move first for better ordering
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
                    break;
            }

            // Store in transposition table
            int flag = (maxEval <= originalAlpha) ? TranspositionTable::UPPER_BOUND : (maxEval >= beta) ? TranspositionTable::LOWER_BOUND
                                                                                                        : TranspositionTable::EXACT;

            tt.store(positionKey, maxEval, depth, flag, bestMove);
            return maxEval;
        }
        else
        {
            // Similar implementation for minimizing player
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
                    break;
            }

            int flag = (minEval >= originalAlpha) ? TranspositionTable::LOWER_BOUND : (minEval <= beta) ? TranspositionTable::UPPER_BOUND
                                                                                                        : TranspositionTable::EXACT;

            tt.store(positionKey, minEval, depth, flag, bestMove);
            return minEval;
        }
    }

    Move findBestMove(int depth = 4)
    {
        Move bestMove;
        int bestValue = INT_MIN;
        bool maximizingPlayer = (board.sideToMove() == Color::WHITE);

        Movelist moves;
        getValidMoves(moves); // Get ordered moves

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
                moveValue = -moveValue; // Negate for minimizing player
            }

            board.unmakeMove(move);

            if (moveValue > bestValue)
            {
                bestValue = moveValue;
                bestMove = move;
            }
        }
        // cout << "Best move: " << uci::moveToUci(bestMove) << " with value: " << bestValue << endl;
        return bestMove;
    }

    int quiescenceSearch(int alpha, int beta)
    {
        int standPat = evaluatePosition();

        if (standPat >= beta)
            return beta;
        if (standPat > alpha)
            alpha = standPat;

        // We will use only captures and checks
        Movelist captures;
        movegen::legalmoves<movegen::MoveGenType::CAPTURE>(captures, board);

        for (const auto &move : captures)
        {
            // Only search good captures(SEE >= 0)
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

    // Implement finding best move with iterative deepening for better time management
    Move findBestMoveIterativeDeepening(int maxDepth = 10, int timeLimitMs = 5000)
    {
        Move bestMove;
        auto startTime = std::chrono::steady_clock::now();

        for (int depth = 0; depth <= maxDepth; depth++)
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

    Movelist getOrderedMoves() const
    {
        Movelist moves;
        movegen::legalmoves(moves, board);

        // Separate moves by category for clearer ordering
        vector<Move> captures, checks, castling, enpassant, normal;

        for (const auto &move : moves)
        {
            // Temporarily make move to check for checks
            Board tempBoard = board;
            tempBoard.makeMove(move);
            bool givesCheck = tempBoard.inCheck();

            if (board.at(move.to()) != Piece::NONE)
            {
                captures.push_back(move);
            }
            else if (givesCheck)
            {
                checks.push_back(move);
            }
            else if (move.typeOf() == Move::CASTLING)
            {
                castling.push_back(move);
            }
            else if (move.typeOf() == Move::ENPASSANT)
            {
                enpassant.push_back(move);
            }
            else
            {
                normal.push_back(move);
            }
        }

        // Sort captures by MVV-LVA
        std::sort(captures.begin(), captures.end(), [this](const Move &a, const Move &b)
                  {
        PieceType victimA = board.at(a.to()).type();
        PieceType victimB = board.at(b.to()).type();
        PieceType attackerA = board.at(a.from()).type();
        PieceType attackerB = board.at(b.from()).type();
        
        int scoreA = getPieceValue(victimA) - getPieceValue(attackerA);
        int scoreB = getPieceValue(victimB) - getPieceValue(attackerB);
        
        return scoreA > scoreB; });

        // Combine in order: captures, checks, castling, en passant, normal
        Movelist orderedMoves;
        for (const auto &move : captures)
            orderedMoves.add(move);
        for (const auto &move : checks)
            orderedMoves.add(move);
        for (const auto &move : castling)
            orderedMoves.add(move);
        for (const auto &move : enpassant)
            orderedMoves.add(move);
        for (const auto &move : normal)
            orderedMoves.add(move);

        return orderedMoves;
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

    // For debugging purposes
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
        cout << "id name YouRLoser_M1" << endl;
        cout << "id author Raffolk" << endl;

        cout << "option name Hash type spin default 64 min 1 max 1024" << endl;
        cout << "option name Threads type spin default 1 min 1 max 16" << endl;
        cout << "option name Move Overhead type spin default 10 min 0 max 5000" << endl;
        cout << "option name Skill Level type spin default 20 min 0 max 20" << endl;
        cout << "option name UCI_LimitStrength type check default false" << endl;
        cout << "option name UCI_Elo type spin default 1500 min 1000 max 3000" << endl;
        cout << "option name UCI_ShowWDL type string default" << endl;
        // Syzygy tablebase options (REQUIRED)
        cout << "option name SyzygyPath type string default" << endl;
        // cout << "option name SyzygyProbeDepth type spin default 1 min 1 max 100" << endl;
        // cout << "option name Syzygy50MoveRule type check default true" << endl;
        // cout << "option name SyzygyProbeLimit type spin default 7 min 0 max 7" << endl;

        cout << "uciok" << endl;
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
                    engine.makeMove(move); // catch
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

    void handleSetOption(std::istringstream &iss)
    {
        string name, value;
        string token;

        while (iss >> token)
        {
            if (token == "name")
            {
                iss >> name;
            }
            else if (token == "value")
            {
                iss >> value;
            }
        }
        if (name == "Hash")
        {
            // Set hash table size
        }
        else if (name == "Threads")
        {
            // Set number of threads;
        }
    }
};

int main()
{
    // Check if the UCI Mode is running
    // if (argc > 1 && string(argv[1]) == "uci")
    //{
    //    UCIHandler uciHandler;
    //    uciHandler.run();
    //    return 0;
    //}
    ChessEngine engine;

    cout << "=== Chess Engine Test Suite ===" << endl;
    cout << "Choose test mode:" << endl;
    cout << "1. Engine vs Human" << endl;
    cout << "2. Engine vs Engine" << endl;
    cout << "3. Move Ordering Analysis" << endl;
    cout << "Enter choice (1-3): ";

    int choice;
    cin >> choice;

    return 0;
}