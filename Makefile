# Microstructure Engine Makefile
# Week 1.1: CSV Analyzer Extension
# Week 1.2: Order Book Analytics
# Week 2.1: Order Flow Tracking
# Week 2.2: Market Impact Calibration

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

# Targets
BACKTESTER = $(BUILD_DIR)/backtester
BACKTESTER_DEBUG = $(BUILD_DIR)/backtester_debug
ORDERBOOK_TEST = $(BUILD_DIR)/test_order_book
FLOW_TRACKING_TEST = $(BUILD_DIR)/test_flow_tracking
CALIBRATION_TEST = $(BUILD_DIR)/test_calibration

# Default target
.PHONY: all
all: $(BACKTESTER) $(ORDERBOOK_TEST) $(FLOW_TRACKING_TEST) $(CALIBRATION_TEST)

# Create build directory
$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

# Build optimized backtester (Week 1.1)
$(BACKTESTER): $(BACKTESTER_SRC) $(INCLUDE_DIR)/*.hpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -I$(INCLUDE_DIR) -o $@ $(BACKTESTER_SRC)

# Build debug backtester
.PHONY: debug
debug: $(BACKTESTER_DEBUG)

$(BACKTESTER_DEBUG): $(BACKTESTER_SRC) $(INCLUDE_DIR)/*.hpp | $(BUILD_DIR)
	$(CXX) $(DEBUG_FLAGS) -I$(INCLUDE_DIR) -o $@ $(BACKTESTER_SRC)

# Build order book analytics test (Week 1.2)
$(ORDERBOOK_TEST): $(ORDERBOOK_TEST_SRC) $(INCLUDE_DIR)/*.hpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -I$(INCLUDE_DIR) -I$(MATCHING_ENGINE_INCLUDE) \
		-o $@ $(ORDERBOOK_TEST_SRC) $(MATCHING_ENGINE_SRCS)

# Build order flow tracking test (Week 2.1)
$(FLOW_TRACKING_TEST): $(FLOW_TRACKING_TEST_SRC) $(INCLUDE_DIR)/*.hpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -I$(INCLUDE_DIR) -I$(MATCHING_ENGINE_INCLUDE) \
		-o $@ $(FLOW_TRACKING_TEST_SRC) $(MATCHING_ENGINE_SRCS)

# Build market impact calibration test (Week 2.2)
$(CALIBRATION_TEST): $(CALIBRATION_TEST_SRC) $(INCLUDE_DIR)/*.hpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -I$(INCLUDE_DIR) -I$(MATCHING_ENGINE_INCLUDE) \
		-o $@ $(CALIBRATION_TEST_SRC) $(MATCHING_ENGINE_SRCS)

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

# Run backtester tests (Week 1.1)
.PHONY: test-backtester
test-backtester: $(BACKTESTER)
	@echo "=== Running Backtester Tests ==="
	$(BACKTESTER) --impact --stats $(TESTS_DIR)/data/test_data.csv
	@echo ""
	@echo "=== Running Symbol Filter Test ==="
	$(BACKTESTER) --symbol=AAPL --impact $(TESTS_DIR)/data/test_data.csv
	@echo ""

# Run order book tests (Week 1.2)
.PHONY: test-orderbook
test-orderbook: $(ORDERBOOK_TEST)
	@echo "=== Running Order Book Analytics Tests ==="
	$(ORDERBOOK_TEST)
	@echo ""

# Run flow tracking tests (Week 2.1)
.PHONY: test-flow
test-flow: $(FLOW_TRACKING_TEST)
	@echo "=== Running Order Flow Tracking Tests ==="
	$(FLOW_TRACKING_TEST)
	@echo ""

# Run calibration tests (Week 2.2)
.PHONY: test-calibration
test-calibration: $(CALIBRATION_TEST)
	@echo "=== Running Market Impact Calibration Tests ==="
	$(CALIBRATION_TEST)
	@echo ""

# Run all tests
.PHONY: test
test: test-backtester test-orderbook test-flow test-calibration
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
	@echo "  make test         - Run all tests"
	@echo "  make test-backtester - Run backtester tests only"
	@echo "  make test-orderbook  - Run order book tests only"
	@echo "  make test-flow    - Run flow tracking tests only"
	@echo "  make test-calibration - Run calibration tests only"
	@echo "  make clean        - Remove build artifacts"
	@echo "  make install      - Install to /usr/local/bin"
	@echo ""
	@echo "Week 1.1 - CSV Analyzer (Backtester):"
	@echo "  ./build/backtester [options] filename.csv"
	@echo ""
	@echo "Week 1.2 - Order Book Analytics:"
	@echo "  ./build/test_order_book"
	@echo ""
	@echo "Week 2.1 - Order Flow Tracking:"
	@echo "  ./build/test_flow_tracking"
	@echo ""
	@echo "Week 2.2 - Market Impact Calibration:"
	@echo "  ./build/test_calibration"
