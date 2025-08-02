# Makefile for ChessEngine

CXX = g++
CXXFLAGS = -std=c++17 -O3 -march=native -DNDEBUG -flto -Wall -Wextra
DEBUGFLAGS = -std=c++17 -O0 -g -Wall -Wextra -DDEBUG
LDFLAGS = -pthread

TARGET = chess_engine
SOURCES = main.cpp engine.cpp search.cpp evaluation.cpp transposition.cpp uci.cpp book.cpp
OBJECTS = $(SOURCES:.cpp=.o)

# Default target
all: $(TARGET)

# Debug target
debug: CXXFLAGS = $(DEBUGFLAGS)
debug: $(TARGET)

# Build the main executable
$(TARGET): $(OBJECTS) chess.hpp
	$(CXX) $(OBJECTS) -o $(TARGET) $(LDFLAGS)

# Build object files
%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Dependencies
main.o: main.cpp chess.hpp engine.h uci.h
engine.o: engine.cpp engine.h types.h search.h evaluation.h transposition.h book.h
search.o: search.cpp search.h types.h transposition.h evaluation.h
evaluation.o: evaluation.cpp evaluation.h types.h
transposition.o: transposition.cpp transposition.h types.h
uci.o: uci.cpp uci.h engine.h
book.o: book.cpp book.h types.h

# Clean up build files
clean:
	rm -f $(OBJECTS) $(TARGET)

# Install (copy to /usr/local/bin)
install: $(TARGET)
	sudo cp $(TARGET) /usr/local/bin/

# Uninstall
uninstall:
	sudo rm -f /usr/local/bin/$(TARGET)

# Testing with cutechess-cli (if available)
test-cutechess:
	@echo "Testing with cutechess-cli..."
	cutechess-cli -engine cmd=./$(TARGET) proto=uci -engine cmd=./$(TARGET) proto=uci -each tc=10+0.1 -games 10 -repeat -pgnout test_games.pgn

# Profile build
profile: CXXFLAGS += -pg
profile: $(TARGET)

# Run with UCI interface
run:
	./$(TARGET)

# Help
help:
	@echo "Available targets:"
	@echo "  all         - Build release version (default)"
	@echo "  debug       - Build debug version"
	@echo "  clean       - Remove build files"
	@echo "  install     - Install to /usr/local/bin"
	@echo "  uninstall   - Remove from /usr/local/bin"
	@echo "  test-cutechess - Test with cutechess-cli"
	@echo "  profile     - Build with profiling enabled"
	@echo "  run         - Run the engine"
	@echo "  help        - Show this help"

.PHONY: all debug clean install uninstall test-cutechess profile run help