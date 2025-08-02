#ifndef BOOK_H
#define BOOK_H

#include "types.h"
#include <string>
#include <vector>
#include <fstream>
#include <map>

// PolyGlot book entry structure
struct PolyGlotEntry {
    uint64_t key;      // Position hash
    uint16_t move;     // Move in PolyGlot format
    uint16_t weight;   // Move weight
    uint32_t learn;    // Learning data
    
    PolyGlotEntry() : key(0), move(0), weight(0), learn(0) {}
};

class OpeningBook {
public:
    OpeningBook();
    ~OpeningBook();
    
    bool loadFromFile(const std::string& filename);
    Move getMove(const Board& board);
    bool isLoaded() const { return loaded_; }
    
    // Statistics
    size_t size() const { return entries_.size(); }
    int getTotalWeight(uint64_t key) const;
    
private:
    std::vector<PolyGlotEntry> entries_;
    bool loaded_;
    
    // PolyGlot key generation
    uint64_t getPolyGlotKey(const Board& board);
    
    // PolyGlot move conversion
    Move polyGlotMoveToMove(uint16_t poly_move, const Board& board);
    uint16_t moveToPolyGlotMove(const Move& move);
    
    // Binary search for book entries
    std::vector<PolyGlotEntry> findEntries(uint64_t key);
    
    // Random move selection based on weights
    Move selectMove(const std::vector<PolyGlotEntry>& entries, const Board& board);
};

#endif // BOOK_H