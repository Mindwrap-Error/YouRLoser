#ifndef ENGINE_H
#define ENGINE_H

#include <memory>
#include <atomic>
#include <thread>
#include "types.h"
#include "search.h"
#include "evaluation.h"
#include "transposition.h"
#include "book.h"

class ChessEngine {
public:
    ChessEngine();
    ~ChessEngine();
    
    // Main engine interface
    void newGame();
    void setPosition(const std::string& fen, const std::vector<std::string>& moves);
    Move search(int depth = 0, int movetime = 0, int wtime = 0, int btime = 0, 
                int winc = 0, int binc = 0, bool infinite = false);
    void stopSearch();
    
    // Engine configuration
    void setHashSize(int mb);
    void setBookPath(const std::string& path);
    void setTablebases(const std::string& path);
    void setThreads(int threads);
    
    // Analysis
    int evaluate();
    std::string getAnalysis();
    
    // Position management
    Board& getBoard() { return board_; }
    const Board& getBoard() const { return board_; }
    
    // Search info
    const SearchInfo& getSearchInfo() const { return search_info_; }
    
private:
    Board board_;
    std::unique_ptr<Search> search_;
    std::unique_ptr<Evaluation> evaluation_;
    std::unique_ptr<TranspositionTable> tt_;
    std::unique_ptr<OpeningBook> book_;
    
    SearchInfo search_info_;
    std::atomic<bool> searching_;
    std::thread search_thread_;
    
    // Configuration
    int hash_size_mb_;
    int threads_;
    std::string book_path_;
    std::string tb_path_;
    
    void initializeComponents();
};

#endif // ENGINE_H