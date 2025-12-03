# Microstructure Engine Makefile
# CSV Analyzer Extension
# Order Book Analytics
# Order Flow Tracking
# Market Impact Calibration
# TWAP Execution Strategy

CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -O3 -march=native -DNDEBUG
DEBUG_FLAGS = -std=c++17 -Wall -Wextra -g -O0 -DDEBUG

# Directories
INCLUDE_DIR = include
SRC_DIR = src
BUILD_DIR = build
TESTS_DIR = tests

# Matching-Engine paths (sibling directory)
MATCHING_ENGINE_DIR = ../Matching-Engine
MATCHING_ENGINE_INCLUDE = $(MATCHING_ENGINE_DIR)/include
MATCHING_ENGINE_SRC = $(MATCHING_ENGINE_DIR)/src

# Matching-Engine source files needed for linking
MATCHING_ENGINE_SRCS = \
	$(MATCHING_ENGINE_SRC)/order.cpp \
	$(MATCHING_ENGINE_SRC)/order_book.cpp \
	$(MATCHING_ENGINE_SRC)/order_book_matching.cpp \
	$(MATCHING_ENGINE_SRC)/order_book_reporting.cpp \
	$(MATCHING_ENGINE_SRC)/order_book_stops.cpp \
	$(MATCHING_ENGINE_SRC)/order_book_persistence.cpp \
	$(MATCHING_ENGINE_SRC)/fill_router.cpp \
	$(MATCHING_ENGINE_SRC)/fill.cpp \
	$(MATCHING_ENGINE_SRC)/snapshot.cpp \
	$(MATCHING_ENGINE_SRC)/event.cpp

# Source files
BACKTESTER_SRC = $(SRC_DIR)/backtester.cpp
ORDERBOOK_TEST_SRC = $(TESTS_DIR)/test_microstructure_order_book.cpp
FLOW_TRACKING_TEST_SRC = $(TESTS_DIR)/test_order_flow_tracking.cpp
CALIBRATION_TEST_SRC = $(TESTS_DIR)/test_market_impact_calibration.cpp
TWAP_TEST_SRC = $(TESTS_DIR)/test_twap_strategy.cpp

# Targets
BACKTESTER = $(BUILD_DIR)/backtester
BACKTESTER_DEBUG = $(BUILD_DIR)/backtester_debug
ORDERBOOK_TEST = $(BUILD_DIR)/test_order_book
FLOW_TRACKING_TEST = $(BUILD_DIR)/test_flow_tracking
CALIBRATION_TEST = $(BUILD_DIR)/test_calibration
TWAP_TEST = $(BUILD_DIR)/test_twap

# Default target
.PHONY: all
all: $(BACKTESTER) $(ORDERBOOK_TEST) $(FLOW_TRACKING_TEST) $(CALIBRATION_TEST) $(TWAP_TEST)

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

# Build order book analytics test 
$(ORDERBOOK_TEST): $(ORDERBOOK_TEST_SRC) $(INCLUDE_DIR)/*.hpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -I$(INCLUDE_DIR) -I$(MATCHING_ENGINE_INCLUDE) \
		-o $@ $(ORDERBOOK_TEST_SRC) $(MATCHING_ENGINE_SRCS)

# Build order flow tracking test 
$(FLOW_TRACKING_TEST): $(FLOW_TRACKING_TEST_SRC) $(INCLUDE_DIR)/*.hpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -I$(INCLUDE_DIR) -I$(MATCHING_ENGINE_INCLUDE) \
		-o $@ $(FLOW_TRACKING_TEST_SRC) $(MATCHING_ENGINE_SRCS)

# Build market impact calibration test
$(CALIBRATION_TEST): $(CALIBRATION_TEST_SRC) $(INCLUDE_DIR)/*.hpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -I$(INCLUDE_DIR) -I$(MATCHING_ENGINE_INCLUDE) \
		-o $@ $(CALIBRATION_TEST_SRC) $(MATCHING_ENGINE_SRCS)

# Build TWAP strategy test
$(TWAP_TEST): $(TWAP_TEST_SRC) $(INCLUDE_DIR)/*.hpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -I$(INCLUDE_DIR) -I$(MATCHING_ENGINE_INCLUDE) \
		-o $@ $(TWAP_TEST_SRC) $(MATCHING_ENGINE_SRCS)

# Build order book test in debug mode
.PHONY: debug-orderbook
debug-orderbook: | $(BUILD_DIR)
	$(CXX) $(DEBUG_FLAGS) -I$(INCLUDE_DIR) -I$(MATCHING_ENGINE_INCLUDE) \
		-o $(BUILD_DIR)/test_order_book_debug $(ORDERBOOK_TEST_SRC) $(MATCHING_ENGINE_SRCS)

# Build flow tracking test in debug mode
.PHONY: debug-flow
debug-flow: | $(BUILD_DIR)
	$(CXX) $(DEBUG_FLAGS) -I$(INCLUDE_DIR) -I$(MATCHING_ENGINE_INCLUDE) \
		-o $(BUILD_DIR)/test_flow_tracking_debug $(FLOW_TRACKING_TEST_SRC) $(MATCHING_ENGINE_SRCS)

# Build calibration test in debug mode
.PHONY: debug-calibration
debug-calibration: | $(BUILD_DIR)
	$(CXX) $(DEBUG_FLAGS) -I$(INCLUDE_DIR) -I$(MATCHING_ENGINE_INCLUDE) \
		-o $(BUILD_DIR)/test_calibration_debug $(CALIBRATION_TEST_SRC) $(MATCHING_ENGINE_SRCS)

# Build TWAP test in debug mode
.PHONY: debug-twap
debug-twap: | $(BUILD_DIR)
	$(CXX) $(DEBUG_FLAGS) -I$(INCLUDE_DIR) -I$(MATCHING_ENGINE_INCLUDE) \
		-o $(BUILD_DIR)/test_twap_debug $(TWAP_TEST_SRC) $(MATCHING_ENGINE_SRCS)

# Run backtester tests 
.PHONY: test-backtester
test-backtester: $(BACKTESTER)
	@echo "=== Running Backtester Tests ==="
	$(BACKTESTER) --impact --stats $(TESTS_DIR)/data/test_data.csv
	@echo ""
	@echo "=== Running Symbol Filter Test ==="
	$(BACKTESTER) --symbol=AAPL --impact $(TESTS_DIR)/data/test_data.csv
	@echo ""

# Run order book tests 
.PHONY: test-orderbook
test-orderbook: $(ORDERBOOK_TEST)
	@echo "=== Running Order Book Analytics Tests ==="
	$(ORDERBOOK_TEST)
	@echo ""

# Run flow tracking tests 
.PHONY: test-flow
test-flow: $(FLOW_TRACKING_TEST)
	@echo "=== Running Order Flow Tracking Tests ==="
	$(FLOW_TRACKING_TEST)
	@echo ""

# Run calibration tests
.PHONY: test-calibration
test-calibration: $(CALIBRATION_TEST)
	@echo "=== Running Market Impact Calibration Tests ==="
	$(CALIBRATION_TEST)
	@echo ""

# Run TWAP strategy tests
.PHONY: test-twap
test-twap: $(TWAP_TEST)
	@echo "=== Running TWAP Strategy Tests ==="
	$(TWAP_TEST)
	@echo ""

# Run all tests
.PHONY: test
test: test-backtester test-orderbook test-flow test-calibration test-twap
	@echo "=== All Tests Complete ==="

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
	@echo "Microstructure Engine - Build Targets"
	@echo ""
	@echo "  make              - Build all components"
	@echo "  make debug        - Build backtester in debug mode"
	@echo "  make debug-orderbook - Build order book test in debug mode"
	@echo "  make debug-flow   - Build flow tracking test in debug mode"
	@echo "  make debug-calibration - Build calibration test in debug mode"
	@echo "  make debug-twap   - Build TWAP test in debug mode"
	@echo "  make test         - Run all tests"
	@echo "  make test-backtester - Run backtester tests only"
	@echo "  make test-orderbook  - Run order book tests only"
	@echo "  make test-flow    - Run flow tracking tests only"
	@echo "  make test-calibration - Run calibration tests only"
	@echo "  make test-twap    - Run TWAP strategy tests only"
	@echo "  make clean        - Remove build artifacts"
	@echo "  make install      - Install to /usr/local/bin"
	@echo ""
	@echo "CSV Analyzer (Backtester):"
	@echo "  ./build/backtester [options] filename.csv"
	@echo ""
	@echo "Order Book Analytics:"
	@echo "  ./build/test_order_book"
	@echo ""
	@echo "Order Flow Tracking:"
	@echo "  ./build/test_flow_tracking"
	@echo ""
	@echo "Market Impact Calibration:"
	@echo "  ./build/test_calibration"
	@echo ""
	@echo "TWAP Execution Strategy:"
	@echo "  ./build/test_twap"
