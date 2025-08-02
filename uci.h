#ifndef UCI_H
#define UCI_H

#include <string>
#include <vector>
#include <sstream>
#include "engine.h"

class UCIHandler {
public:
    UCIHandler(ChessEngine& engine);
    
    void processCommand(const std::string& command);
    
private:
    ChessEngine& engine_;
    
    // UCI command handlers
    void handleUCI();
    void handleIsReady();
    void handleUCINewGame();
    void handlePosition(std::istringstream& iss);
    void handleGo(std::istringstream& iss);
    void handleStop();
    void handleQuit();
    void handleSetOption(std::istringstream& iss);
    void handlePerft(std::istringstream& iss);
    void handleEval();
    
    // Utility functions
    std::vector<std::string> split(const std::string& str, char delimiter);
    std::string moveToString(const Move& move);
};

#endif // UCI_H