#ifndef SEARCH_H
#define SEARCH_H

#include "types.h"
#include "transposition.h"
#include "evaluation.h"
#include <vector>

struct SearchResult {
    Move best_move;
    int score;
    int nodes;
    PVLine pv;
};

class Search {
public:
    Search(TranspositionTable& tt, Evaluation& eval);
    
    SearchResult searchRoot(const Board& board, int depth, SearchInfo& info);
    
    void setTranspositionTable(TranspositionTable& tt) { tt_ = &tt; }
    
private:
    TranspositionTable* tt_;
    Evaluation* eval_;
    
    // History heuristic tables
    int history_[2][64][64];  // [color][from][to]
    int killer_moves_[MAX_PLY][2];
    
    // Search methods
    int search(Board& board, int depth, int ply, int alpha, int beta, 
               PVLine& pv, SearchInfo& info, bool null_move_allowed = true);
    int quiescence(Board& board, int ply, int alpha, int beta, SearchInfo& info);
    
    // Move ordering
    void orderMoves(chess::Movelist& moves, const Board& board, Move hash_move, int ply);
    int getMoveScore(const Move& move, const Board& board, Move hash_move, int ply);
    
    // Search extensions and reductions
    int getExtension(const Move& move, const Board& board, bool in_check);
    int getReduction(const Move& move, int depth, int move_count, bool pv_node);
    
    // Null move pruning
    bool canDoNullMove(const Board& board);
    
    // Principal Variation Search
    int pvs(Board& board, int depth, int ply, int alpha, int beta, 
            PVLine& pv, SearchInfo& info, bool pv_node);
    
    // Mate distance pruning
    void mateDistancePruning(int& alpha, int& beta, int ply);
    
    // Update search statistics
    void updateHistory(const Move& move, Color color, int depth);
    void updateKillers(const Move& move, int ply);
    
    void clearHistory();
};

#endif // SEARCH_H