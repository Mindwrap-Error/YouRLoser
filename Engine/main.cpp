#include <iostream>
#include <string>
#include <sstream>
#include <thread>
#include <atomic>
#include <chrono>
#include "chess.hpp"
#include "engine.h"
#include "uci.h"

int main() {
    // Initialize attack tables for chess.hpp
    chess::attacks::initAttacks();
    
    // Create the chess engine
    ChessEngine engine;
    UCIHandler uci(engine);
    
    std::cout.setf(std::ios::unitbuf); // Ensure immediate output
    
    std::string line;
    while (std::getline(std::cin, line)) {
        if (line == "quit") {
            break;
        }
        uci.processCommand(line);
    }
    
    return 0;
}