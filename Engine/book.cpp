#include "book.h"
#include <algorithm>
#include <random>
#include <iostream>

// PolyGlot random numbers for key generation
static const uint64_t PolyGlotRandoms[781] = {
    0x9D39247E33776D41ULL, 0x2AF7398005AAA5C7ULL, 0x44DB015024623547ULL, 0x9C15F73E62A76AE2ULL,
    // ... (truncated for brevity - in real implementation, include all 781 values)
    // These would be the complete PolyGlot random numbers
};

OpeningBook::OpeningBook() : loaded_(false) {}

OpeningBook::~OpeningBook() = default;

bool OpeningBook::loadFromFile(const std::string &filename)
{
    std::ifstream file(filename, std::ios::binary);
    if (!file.is_open())
    {
        std::cerr << "Could not open book file: " << filename << std::endl;
        return false;
    }

    entries_.clear();

    // Read PolyGlot entries
    PolyGlotEntry entry;
    while (file.read(reinterpret_cast<char *>(&entry), sizeof(PolyGlotEntry)))
    {
        // Convert from big-endian to host byte order
        entry.key = __builtin_bswap64(entry.key);
        entry.move = __builtin_bswap16(entry.move);
        entry.weight = __builtin_bswap16(entry.weight);
        entry.learn = __builtin_bswap32(entry.learn);

        entries_.push_back(entry);
    }

    file.close();

    // Sort entries by key for binary search
    std::sort(entries_.begin(), entries_.end(),
              [](const PolyGlotEntry &a, const PolyGlotEntry &b)
              {
                  return a.key < b.key;
              });

    loaded_ = !entries_.empty();

    if (loaded_)
    {
        std::cout << "Loaded " << entries_.size() << " book entries from " << filename << std::endl;
    }

    return loaded_;
}

Move OpeningBook::getMove(const Board &board)
{
    if (!loaded_)
    {
        return Move::NO_MOVE;
    }

    uint64_t key = getPolyGlotKey(board);
    auto book_entries = findEntries(key);

    if (book_entries.empty())
    {
        return Move::NO_MOVE;
    }

    return selectMove(book_entries, board);
}

std::vector<PolyGlotEntry> OpeningBook::findEntries(uint64_t key)
{
    std::vector<PolyGlotEntry> result;

    // Binary search for first occurrence
    auto it = std::lower_bound(entries_.begin(), entries_.end(), key,
                               [](const PolyGlotEntry &entry, uint64_t search_key)
                               {
                                   return entry.key < search_key;
                               });

    // Collect all entries with matching key
    while (it != entries_.end() && it->key == key)
    {
        result.push_back(*it);
        ++it;
    }

    return result;
}

Move OpeningBook::selectMove(const std::vector<PolyGlotEntry> &entries, const Board &board)
{
    if (entries.empty())
    {
        return Move::NO_MOVE;
    }

    // Calculate total weight
    int total_weight = 0;
    for (const auto &entry : entries)
    {
        total_weight += entry.weight;
    }

    if (total_weight == 0)
    {
        return Move::NO_MOVE;
    }

    // Random selection based on weights
    static std::random_device rd;
    static std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, total_weight - 1);
    int random_weight = dis(gen);

    int current_weight = 0;
    for (const auto &entry : entries)
    {
        current_weight += entry.weight;
        if (random_weight < current_weight)
        {
            Move move = polyGlotMoveToMove(entry.move, board);

            // Verify the move is legal
            chess::Movelist legal_moves;
            chess::movegen::legalmoves(legal_moves, board);

            for (const auto &legal_move : legal_moves)
            {
                if (legal_move == move)
                {
                    return move;
                }
            }
            break;
        }
    }

    return Move::NO_MOVE;
}

uint64_t OpeningBook::getPolyGlotKey(const Board &board)
{
    uint64_t key = 0;

    // Pieces
    for (int square = 0; square < 64; ++square)
    {
        Piece piece = board.at(Square(square));
        if (piece != Piece::NONE)
        {
            int piece_index = -1;

            // Convert piece to PolyGlot piece index
            if (piece.color() == Color::WHITE)
            {
                switch (static_cast<int>(piece.type()))
                {
                case static_cast<int>(PieceType::PAWN):
                    piece_index = 0;
                    break;
                case static_cast<int>(PieceType::KNIGHT):
                    piece_index = 1;
                    break;
                case static_cast<int>(PieceType::BISHOP):
                    piece_index = 2;
                    break;
                case static_cast<int>(PieceType::ROOK):
                    piece_index = 3;
                    break;
                case static_cast<int>(PieceType::QUEEN):
                    piece_index = 4;
                    break;
                case static_cast<int>(PieceType::KING):
                    piece_index = 5;
                    break;
                default:
                    break;
                }
            }
            else
            {
                switch (static_cast<int>(piece.type()))
                {
                case static_cast<int>(PieceType::PAWN):
                    piece_index = 6;
                    break;
                case static_cast<int>(PieceType::KNIGHT):
                    piece_index = 7;
                    break;
                case static_cast<int>(PieceType::BISHOP):
                    piece_index = 8;
                    break;
                case static_cast<int>(PieceType::ROOK):
                    piece_index = 9;
                    break;
                case static_cast<int>(PieceType::QUEEN):
                    piece_index = 10;
                    break;
                case static_cast<int>(PieceType::KING):
                    piece_index = 11;
                    break;
                default:
                    break;
                }
            }

            if (piece_index >= 0)
            {
                key ^= PolyGlotRandoms[64 * piece_index + square];
            }
        }
    }

    // Castling rights
    auto castling = board.castlingRights();
    if (castling.has(Color::WHITE, chess::Board::CastlingRights::Side::KING_SIDE))
    {
        key ^= PolyGlotRandoms[768];
    }
    if (castling.has(Color::WHITE, chess::Board::CastlingRights::Side::QUEEN_SIDE))
    {
        key ^= PolyGlotRandoms[769];
    }
    if (castling.has(Color::BLACK, chess::Board::CastlingRights::Side::KING_SIDE))
    {
        key ^= PolyGlotRandoms[770];
    }
    if (castling.has(Color::BLACK, chess::Board::CastlingRights::Side::QUEEN_SIDE))
    {
        key ^= PolyGlotRandoms[771];
    }

    // En passant
    Square ep_sq = board.enpassantSq();
    if (ep_sq != Square::NO_SQ)
    {
        key ^= PolyGlotRandoms[772 + static_cast<int>(ep_sq.file())];
    }

    // Side to move
    if (board.sideToMove() == Color::WHITE)
    {
        key ^= PolyGlotRandoms[780];
    }

    return key;
}

Move OpeningBook::polyGlotMoveToMove(uint16_t poly_move, const Board &board)
{
    int from_sq = (poly_move >> 6) & 0x3F;
    int to_sq = poly_move & 0x3F;
    int promotion = (poly_move >> 12) & 0x7;

    Square from(from_sq);
    Square to(to_sq);

    // Handle promotions
    if (promotion > 0)
    {
        PieceType promo_type;
        switch (promotion)
        {
        case 1:
            promo_type = PieceType::KNIGHT;
            break;
        case 2:
            promo_type = PieceType::BISHOP;
            break;
        case 3:
            promo_type = PieceType::ROOK;
            break;
        case 4:
            promo_type = PieceType::QUEEN;
            break;
        default:
            promo_type = PieceType::QUEEN;
            break;
        }
        return Move::make<Move::PROMOTION>(from, to, promo_type);
    }

    // Check for special moves
    Piece piece = board.at(from);

    // Castling
    if (piece.type() == PieceType::KING)
    {
        if (from == Square::SQ_E1 && to == Square::SQ_G1)
        {
            return Move::make<Move::CASTLING>(from, Square::SQ_H1);
        }
        if (from == Square::SQ_E1 && to == Square::SQ_C1)
        {
            return Move::make<Move::CASTLING>(from, Square::SQ_A1);
        }
        if (from == Square::SQ_E8 && to == Square::SQ_G8)
        {
            return Move::make<Move::CASTLING>(from, Square::SQ_H8);
        }
        if (from == Square::SQ_E8 && to == Square::SQ_C8)
        {
            return Move::make<Move::CASTLING>(from, Square::SQ_A8);
        }
    }

    // En passant
    if (piece.type() == PieceType::PAWN && to == board.enpassantSq())
    {
        return Move::make<Move::ENPASSANT>(from, to);
    }

    // Normal move
    return Move::make<Move::NORMAL>(from, to);
}

uint16_t OpeningBook::moveToPolyGlotMove(const Move &move)
{
    uint16_t poly_move = 0;

    poly_move |= move.from().index() << 6;
    poly_move |= move.to().index();

    if (move.typeOf() == Move::PROMOTION)
    {
        int promotion = 0;
        switch (move.promotionType())
        {
        case static_cast<int>(PieceType::KNIGHT):
            promotion = 1;
            break;
        case static_cast<int>(PieceType::BISHOP):
            promotion = 2;
            break;
        case static_cast<int>(PieceType::ROOK):
            promotion = 3;
            break;
        case static_cast<int>(PieceType::QUEEN):
            promotion = 4;
            break;
        default:
            break;
        }
        poly_move |= promotion << 12;
    }

    return poly_move;
}

int OpeningBook::getTotalWeight(uint64_t key) const
{
    int total = 0;
    auto entries = const_cast<OpeningBook *>(this)->findEntries(key);
    for (const auto &entry : entries)
    {
        total += entry.weight;
    }
    return total;
}