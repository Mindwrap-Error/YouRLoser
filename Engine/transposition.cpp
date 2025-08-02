#include "transposition.h"
#include <cstring>
#include <algorithm>

TranspositionTable::TranspositionTable(int size_mb) : age_(0) {
    // Calculate table size (power of 2)
    size_t size_bytes = static_cast<size_t>(size_mb) * 1024 * 1024;
    table_size_ = size_bytes / sizeof(TTEntry);
    
    // Round down to power of 2
    size_t power = 1;
    while (power <= table_size_) {
        power <<= 1;
    }
    table_size_ = power >> 1;
    table_mask_ = table_size_ - 1;
    
    table_ = std::make_unique<TTEntry[]>(table_size_);
    clear();
}

TranspositionTable::~TranspositionTable() = default;

void TranspositionTable::store(uint64_t hash, int depth, int score, TTFlag flag, Move move, int eval) {
    size_t index = getIndex(hash);
    TTEntry& entry = table_[index];
    
    // Always replace if hash matches or if this is a better entry
    if (entry.hash != hash || shouldReplace(entry, depth, age_)) {
        entry.hash = hash;
        entry.move = move;
        entry.score = static_cast<int16_t>(score);
        entry.eval = static_cast<int16_t>(eval);
        entry.depth = static_cast<uint8_t>(depth);
        entry.flag = static_cast<uint8_t>(flag);
        entry.age = age_;
    }
}

TTEntry* TranspositionTable::probe(uint64_t hash) {
    size_t index = getIndex(hash);
    TTEntry& entry = table_[index];
    
    return (entry.hash == hash) ? &entry : nullptr;
}

void TranspositionTable::clear() {
    std::memset(table_.get(), 0, table_size_ * sizeof(TTEntry));
    age_ = 0;
}

void TranspositionTable::prefetch(uint64_t hash) {
    // Prefetch the cache line containing the hash entry
    size_t index = getIndex(hash);
    __builtin_prefetch(&table_[index], 0, 3);
}

int TranspositionTable::getHashfull() const {
    // Sample 1000 entries to estimate how full the table is
    int count = 0;
    int sample_size = std::min(static_cast<size_t>(1000), table_size_);
    
    for (int i = 0; i < sample_size; ++i) {
        if (table_[i].hash != 0) {
            count++;
        }
    }
    
    return (count * 1000) / sample_size;
}

bool TranspositionTable::shouldReplace(const TTEntry& existing, int depth, uint8_t new_age) const {
    // Always replace if entry is from older search
    if (existing.age != new_age) {
        return true;
    }
    
    // Replace if new entry has greater depth
    return depth >= existing.depth;
}