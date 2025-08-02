#ifndef TYPES_H
#define TYPES_H

#include <cstdint>
#include <chrono>
#include "chess.hpp"

// Type aliases for convenience
using Bitboard = chess::Bitboard;
using Square = chess::Square;
using Move = chess::Move;
using Board = chess::Board;
using Color = chess::Color;
using PieceType = chess::PieceType;
using Piece = chess::Piece;

// Search constants
constexpr int MAX_DEPTH = 64;
constexpr int MAX_PLY = 128;
constexpr int MATE_VALUE = 30000;
constexpr int MATE_IN_MAX_PLY = MATE_VALUE - MAX_PLY;

// Hash table constants
constexpr int DEFAULT_HASH_SIZE_MB = 64;
constexpr uint64_t HASH_SIZE_BYTES = DEFAULT_HASH_SIZE_MB * 1024 * 1024;

// Time management
using TimePoint = std::chrono::steady_clock::time_point;
using Duration = std::chrono::milliseconds;

// Hash table entry flags
enum TTFlag : uint8_t {
    TT_NONE = 0,
    TT_EXACT = 1,
    TT_LOWER = 2,  // Beta cutoff
    TT_UPPER = 3   // Alpha cutoff
};

// Move ordering scores
constexpr int MVV_LVA[6][6] = {
    {15, 14, 13, 12, 11, 10}, // victim P, attacker P, N, B, R, Q, K
    {25, 24, 23, 22, 21, 20}, // victim N
    {35, 34, 33, 32, 31, 30}, // victim B
    {45, 44, 43, 42, 41, 40}, // victim R
    {55, 54, 53, 52, 51, 50}, // victim Q
    {0,  0,  0,  0,  0,  0}   // victim K
};

// Principal variation
struct PVLine {
    int count = 0;
    Move moves[MAX_DEPTH];
    
    void clear() { count = 0; }
    void push(Move move) { 
        if (count < MAX_DEPTH) {
            moves[count++] = move;
        }
    }
};

// Search information
struct SearchInfo {
    int depth = 0;
    int seldepth = 0;
    int nodes = 0;
    int time_ms = 0;
    bool stopped = false;
    TimePoint start_time;
    Duration time_limit{0};
    
    void reset() {
        depth = seldepth = nodes = time_ms = 0;
        stopped = false;
        start_time = std::chrono::steady_clock::now();
    }
    
    bool should_stop() const {
        if (stopped) return true;
        if (time_limit.count() > 0) {
            auto elapsed = std::chrono::duration_cast<Duration>(
                std::chrono::steady_clock::now() - start_time);
            return elapsed >= time_limit;
        }
        return false;
    }
};

#endif // TYPES_H