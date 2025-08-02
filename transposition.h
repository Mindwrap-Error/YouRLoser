#ifndef TRANSPOSITION_H
#define TRANSPOSITION_H

#include "types.h"
#include <vector>
#include <memory>

struct TTEntry {
    uint64_t hash;
    Move move;
    int16_t score;
    int16_t eval;
    uint8_t depth;
    uint8_t flag;
    uint8_t age;
    
    TTEntry() : hash(0), move(Move::NO_MOVE), score(0), eval(0), depth(0), flag(TT_NONE), age(0) {}
};

class TranspositionTable {
public:
    TranspositionTable(int size_mb);
    ~TranspositionTable();
    
    void store(uint64_t hash, int depth, int score, TTFlag flag, Move move, int eval = 0);
    TTEntry* probe(uint64_t hash);
    
    void clear();
    void prefetch(uint64_t hash);
    
    int getHashfull() const;
    size_t size() const { return table_size_; }
    
    void newSearch() { age_++; }
    
private:
    std::unique_ptr<TTEntry[]> table_;
    size_t table_size_;
    uint64_t table_mask_;
    uint8_t age_;
    
    size_t getIndex(uint64_t hash) const {
        return hash & table_mask_;
    }
    
    bool shouldReplace(const TTEntry& existing, int depth, uint8_t new_age) const;
};

#endif // TRANSPOSITION_H