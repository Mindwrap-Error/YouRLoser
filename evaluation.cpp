#include "evaluation.h"
#include <algorithm>

Evaluation::Evaluation() {}

int Evaluation::evaluate(const Board &board)
{
    int mg_score = 0, eg_score = 0;
    int phase = getPhase(board);

    // Evaluate for both colors
    for (Color color : {Color::WHITE, Color::BLACK})
    {
        int color_mg = 0, color_eg = 0;

        // Material and piece-square tables
        for (PieceType piece_type : {PieceType::PAWN, PieceType::KNIGHT, PieceType::BISHOP,
                                     PieceType::ROOK, PieceType::QUEEN, PieceType::KING})
        {

            Bitboard pieces = board.pieces(piece_type, color);
            while (pieces)
            {
                Square square = pieces.pop();

                // Material value
                color_mg += PIECE_VALUES[static_cast<int>(piece_type)];
                color_eg += PIECE_VALUES[static_cast<int>(piece_type)];

                // Piece-square table value
                int pst_mg = getPieceSquareValue(piece_type, square, false, color == Color::WHITE);
                int pst_eg = getPieceSquareValue(piece_type, square, true, color == Color::WHITE);

                color_mg += pst_mg;
                color_eg += pst_eg;
            }
        }

        // Additional evaluation terms
        color_mg += evaluatePawns(board, color);
        color_eg += evaluatePawns(board, color);
        color_mg += evaluateKingSafety(board, color);
        color_mg += evaluateMobility(board, color);

        if (color == Color::WHITE)
        {
            mg_score += color_mg;
            eg_score += color_eg;
        }
        else
        {
            mg_score -= color_mg;
            eg_score -= color_eg;
        }
    }

    // Interpolate between middlegame and endgame scores
    int final_score = interpolate(mg_score, eg_score, phase);

    // Return from side to move perspective
    return board.sideToMove() == Color::WHITE ? final_score : -final_score;
}

int Evaluation::getMaterialBalance(const Board &board)
{
    int white_material = getMaterialValue(board, Color::WHITE);
    int black_material = getMaterialValue(board, Color::BLACK);
    return white_material - black_material;
}

bool Evaluation::isEndgame(const Board &board)
{
    return getPhase(board) <= 8; // Less than 1/3 of total phase
}

int Evaluation::getPieceSquareValue(PieceType piece, Square square, bool is_endgame, bool is_white)
{
    int sq_index = is_white ? square.index() : flipSquare(square).index();

    switch (static_cast<int>(piece))
    {
    case static_cast<int>(PieceType::PAWN):
        return is_endgame ? PST_PAWN_EG[sq_index] : PST_PAWN_MG[sq_index];
    case static_cast<int>(PieceType::KNIGHT):
        return is_endgame ? PST_KNIGHT_EG[sq_index] : PST_KNIGHT_MG[sq_index];
    case static_cast<int>(PieceType::BISHOP):
        return is_endgame ? PST_BISHOP_EG[sq_index] : PST_BISHOP_MG[sq_index];
    case static_cast<int>(PieceType::ROOK):
        return is_endgame ? PST_ROOK_EG[sq_index] : PST_ROOK_MG[sq_index];
    case static_cast<int>(PieceType::QUEEN):
        return is_endgame ? PST_QUEEN_EG[sq_index] : PST_QUEEN_MG[sq_index];
    case static_cast<int>(PieceType::KING):
        return is_endgame ? PST_KING_EG[sq_index] : PST_KING_MG[sq_index];
    default:
        return 0;
    }
}

int Evaluation::getMaterialValue(const Board &board, Color color)
{
    int material = 0;

    for (PieceType piece_type : {PieceType::PAWN, PieceType::KNIGHT, PieceType::BISHOP, PieceType::ROOK, PieceType::QUEEN, PieceType::KING})
    {
        int count = board.pieces(piece_type, color).count();
        material += count * PIECE_VALUES[static_cast<int>(piece_type)];
    }

    return material;
}

int Evaluation::getPhase(const Board &board)
{
    int phase = TOTAL_PHASE;

    for (Color color : {Color::WHITE, Color::BLACK})
    {
        for (PieceType piece_type : {PieceType::KNIGHT, PieceType::BISHOP, PieceType::ROOK, PieceType::QUEEN, PieceType::KING})
        { // Skip pawns
            int count = board.pieces(piece_type, color).count();
            phase -= count * PHASE_VALUES[static_cast<int>(piece_type)];
        }
    }

    return std::max(0, std::min(phase, TOTAL_PHASE));
}

int Evaluation::evaluatePawns(const Board &board, Color color)
{
    int score = 0;
    Bitboard pawns = board.pieces(PieceType::PAWN, color);

    while (pawns)
    {
        Square square = pawns.pop();
        chess::File file = square.file();

        // Doubled pawns penalty
        Bitboard file_pawns = board.pieces(PieceType::PAWN, color) & (Bitboard(0x0101010101010101ULL) << static_cast<int>(file));
        if (file_pawns.count() > 1)
        {
            score -= 20;
        }

        // Isolated pawn penalty
        Bitboard adjacent_files = Bitboard(0);
        if (file != chess::File::FILE_A)
        {
            adjacent_files |= Bitboard(chess::File(static_cast<int>(file) - 1));
        }
        if (file != chess::File::FILE_H)
        {
            adjacent_files |= Bitboard(chess::File(static_cast<int>(file) + 1));
        }

        if ((board.pieces(PieceType::PAWN, color) & adjacent_files).empty())
        {
            score -= 15; // Isolated pawn
        }

        // Passed pawn bonus
        bool is_passed = true;
        chess::Direction forward = (color == Color::WHITE) ? chess::Direction::NORTH : chess::Direction::SOUTH;

        for (int i = 0; i < 3; ++i)
        { // Check files: left, center, right
            int check_file_idx = static_cast<int>(file) - 1 + i;
            if (check_file_idx < static_cast<int>(chess::File::FILE_A) || check_file_idx > static_cast<int>(chess::File::FILE_H))
                continue;

            Bitboard enemy_pawns_on_file = board.pieces(PieceType::PAWN, ~color) & Bitboard(chess::File(check_file_idx));

            // Check if enemy pawn blocks or can capture
            while (enemy_pawns_on_file)
            {
                Square enemy_sq = enemy_pawns_on_file.pop();
                if ((color == Color::WHITE && enemy_sq.rank() > square.rank()) ||
                    (color == Color::BLACK && enemy_sq.rank() < square.rank()))
                {
                    is_passed = false;
                    break;
                }
            }
            if (!is_passed)
                break;
        }

        if (is_passed)
        {
            int rank_bonus = (color == Color::WHITE) ? static_cast<int>(square.rank()) : (7 - static_cast<int>(square.rank()));
            score += 10 + rank_bonus * rank_bonus;
        }
    }

    return score;
}

int Evaluation::evaluateKingSafety(const Board &board, Color color)
{
    int score = 0;
    Square king_sq = board.kingSq(color);

    // King on back rank bonus in middlegame
    if (!isEndgame(board))
    {
        bool on_back_rank = (color == Color::WHITE && king_sq.rank() == chess::Rank::RANK_1) ||
                            (color == Color::BLACK && king_sq.rank() == chess::Rank::RANK_8);
        if (on_back_rank)
        {
            score += 10;
        }

        // Pawn shield bonus
        Bitboard pawn_shield = chess::attacks::king(king_sq) &
                               board.pieces(PieceType::PAWN, color);
        score += pawn_shield.count() * 5;
    }

    return score;
}

int Evaluation::evaluateMobility(const Board &board, Color color)
{
    int mobility = 0;

    // Knight mobility
    Bitboard knights = board.pieces(PieceType::KNIGHT, color);
    while (knights)
    {
        Square sq = knights.pop();
        Bitboard moves = chess::attacks::knight(sq) & ~board.us(color);
        mobility += moves.count() * 2;
    }

    // Bishop mobility
    Bitboard bishops = board.pieces(PieceType::BISHOP, color);
    while (bishops)
    {
        Square sq = bishops.pop();
        Bitboard moves = chess::attacks::bishop(sq, board.occ()) & ~board.us(color);
        mobility += moves.count() * 3;
    }

    // Rook mobility
    Bitboard rooks = board.pieces(PieceType::ROOK, color);
    while (rooks)
    {
        Square sq = rooks.pop();
        Bitboard moves = chess::attacks::rook(sq, board.occ()) & ~board.us(color);
        mobility += moves.count() * 2;
    }

    // Queen mobility (reduced weight to avoid over-development)
    Bitboard queens = board.pieces(PieceType::QUEEN, color);
    while (queens)
    {
        Square sq = queens.pop();
        Bitboard moves = chess::attacks::queen(sq, board.occ()) & ~board.us(color);
        mobility += moves.count() * 1;
    }

    return mobility;
}

Square Evaluation::flipSquare(Square square)
{
    return Square(square.file(), 7 - static_cast<int>(square.rank()));
}

int Evaluation::interpolate(int mg_score, int eg_score, int phase)
{
    return (mg_score * phase + eg_score * (TOTAL_PHASE - phase)) / TOTAL_PHASE;
}