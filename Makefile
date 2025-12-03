# Microstructure Backtester Makefile
# Week 1.1: CSV Analyzer Extension

CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -O3 -march=native -DNDEBUG
DEBUG_FLAGS = -std=c++17 -Wall -Wextra -g -O0 -DDEBUG

# Directories
INCLUDE_DIR = include
SRC_DIR = src
BUILD_DIR = build
TESTS_DIR = tests

# Source files
BACKTESTER_SRC = $(SRC_DIR)/backtester.cpp

# Targets
BACKTESTER = $(BUILD_DIR)/backtester
BACKTESTER_DEBUG = $(BUILD_DIR)/backtester_debug

# Default target
.PHONY: all
all: $(BACKTESTER)

# Create build directory
$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

# Build optimized backtester
$(BACKTESTER): $(BACKTESTER_SRC) $(INCLUDE_DIR)/*.hpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -I$(INCLUDE_DIR) -o $@ $(BACKTESTER_SRC)

# Build debug backtester
.PHONY: debug
debug: $(BACKTESTER_DEBUG)

$(BACKTESTER_DEBUG): $(BACKTESTER_SRC) $(INCLUDE_DIR)/*.hpp | $(BUILD_DIR)
	$(CXX) $(DEBUG_FLAGS) -I$(INCLUDE_DIR) -o $@ $(BACKTESTER_SRC)

# Run tests
.PHONY: test
test: $(BACKTESTER)
	@echo "=== Running Basic Test ==="
	$(BACKTESTER) --impact --stats $(TESTS_DIR)/data/test_data.csv
	@echo ""
	@echo "=== Running Symbol Filter Test ==="
	$(BACKTESTER) --symbol=AAPL --impact $(TESTS_DIR)/data/test_data.csv
	@echo ""
	@echo "=== Test Complete ==="

# Clean build artifacts
.PHONY: clean
clean:
	rm -rf $(BUILD_DIR)

# Install (copy to /usr/local/bin)
.PHONY: install
install: $(BACKTESTER)
	cp $(BACKTESTER) /usr/local/bin/microstructure-backtester

# Help
.PHONY: help
help:
	@echo "Microstructure Backtester - Build Targets"
	@echo ""
	@echo "  make          - Build optimized backtester"
	@echo "  make debug    - Build debug version"
	@echo "  make test     - Run tests"
	@echo "  make clean    - Remove build artifacts"
	@echo "  make install  - Install to /usr/local/bin"
	@echo ""
	@echo "Usage:"
	@echo "  ./build/backtester [options] filename.csv"
	@echo ""
	@echo "Options:"
	@echo "  --symbol=SYM       Filter to specific symbol"
	@echo "  --impact           Include impact estimates"
	@echo "  --stats            Print timeline statistics"
	@echo "  --adv=N            Assumed ADV (default: 10000000)"
	@echo "  --impact-coeff=X   Impact coefficient (default: 0.01)"
