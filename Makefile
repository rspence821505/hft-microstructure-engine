# Microstructure Engine Makefile
# CSV Analyzer Extension
# Order Book Analytics
# Order Flow Tracking
# Market Impact Calibration
# TWAP Execution Strategy
# Platform Integration

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

# TCP-Socket paths (sibling directory)
TCP_SOCKET_DIR = ../TCP-Socket
TCP_SOCKET_INCLUDE = $(TCP_SOCKET_DIR)/include

# All external include paths
EXTERNAL_INCLUDES = -I$(INCLUDE_DIR) -I$(MATCHING_ENGINE_INCLUDE) -I$(TCP_SOCKET_INCLUDE)

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
PLATFORM_DEMO_SRC = $(SRC_DIR)/platform_demo.cpp
ORDERBOOK_TEST_SRC = $(TESTS_DIR)/test_microstructure_order_book.cpp
FLOW_TRACKING_TEST_SRC = $(TESTS_DIR)/test_order_flow_tracking.cpp
CALIBRATION_TEST_SRC = $(TESTS_DIR)/test_market_impact_calibration.cpp
TWAP_TEST_SRC = $(TESTS_DIR)/test_twap_strategy.cpp
EXECUTION_COSTS_TEST_SRC = $(TESTS_DIR)/test_execution_costs.cpp
PLATFORM_TEST_SRC = $(TESTS_DIR)/test_platform_integration.cpp
PERF_BENCHMARK_SRC = $(TESTS_DIR)/test_performance_benchmarks.cpp

# Targets
BACKTESTER = $(BUILD_DIR)/backtester
BACKTESTER_DEBUG = $(BUILD_DIR)/backtester_debug
PLATFORM_DEMO = $(BUILD_DIR)/platform_demo
ORDERBOOK_TEST = $(BUILD_DIR)/test_order_book
FLOW_TRACKING_TEST = $(BUILD_DIR)/test_flow_tracking
CALIBRATION_TEST = $(BUILD_DIR)/test_calibration
TWAP_TEST = $(BUILD_DIR)/test_twap
EXECUTION_COSTS_TEST = $(BUILD_DIR)/test_execution_costs
PLATFORM_TEST = $(BUILD_DIR)/test_platform
PERF_BENCHMARK = $(BUILD_DIR)/test_performance_benchmarks

# Default target
.PHONY: all
all: $(BACKTESTER) $(PLATFORM_DEMO) $(ORDERBOOK_TEST) $(FLOW_TRACKING_TEST) $(CALIBRATION_TEST) $(TWAP_TEST) $(EXECUTION_COSTS_TEST) $(PERF_BENCHMARK)

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

# ============================================================
#  Platform Integration Build Targets
# ============================================================

# Alias targets for convenience
.PHONY: platform_demo
platform_demo: $(PLATFORM_DEMO)

.PHONY: test_platform
test_platform: $(PLATFORM_TEST)

# Build platform demo 
$(PLATFORM_DEMO): $(PLATFORM_DEMO_SRC) $(INCLUDE_DIR)/*.hpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(EXTERNAL_INCLUDES) \
		-o $@ $(PLATFORM_DEMO_SRC) $(MATCHING_ENGINE_SRCS)

# Build platform demo in debug mode
.PHONY: debug-platform
debug-platform: | $(BUILD_DIR)
	$(CXX) $(DEBUG_FLAGS) $(EXTERNAL_INCLUDES) \
		-o $(BUILD_DIR)/platform_demo_debug $(PLATFORM_DEMO_SRC) $(MATCHING_ENGINE_SRCS)

# Build platform integration test
$(PLATFORM_TEST): $(PLATFORM_TEST_SRC) $(INCLUDE_DIR)/*.hpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(EXTERNAL_INCLUDES) \
		-o $@ $(PLATFORM_TEST_SRC) $(MATCHING_ENGINE_SRCS)

# Run platform demo
.PHONY: run-platform
run-platform: $(PLATFORM_DEMO)
	@echo "=== Running Platform Demo ==="
	$(PLATFORM_DEMO) $(TESTS_DIR)/data/calibration_test.csv
	@echo ""

# Run platform demo with verbose output
.PHONY: run-platform-verbose
run-platform-verbose: $(PLATFORM_DEMO)
	@echo "=== Running Platform Demo (Verbose) ==="
	$(PLATFORM_DEMO) --verbose $(TESTS_DIR)/data/calibration_test.csv
	@echo ""

# ============================================================
# Existing Build Targets
# ============================================================

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

# Build execution costs test
$(EXECUTION_COSTS_TEST): $(EXECUTION_COSTS_TEST_SRC) $(INCLUDE_DIR)/*.hpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -I$(INCLUDE_DIR) -I$(MATCHING_ENGINE_INCLUDE) \
		-o $@ $(EXECUTION_COSTS_TEST_SRC) $(MATCHING_ENGINE_SRCS)

# Build performance benchmarks test (Week 4.2)
$(PERF_BENCHMARK): $(PERF_BENCHMARK_SRC) $(INCLUDE_DIR)/*.hpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(EXTERNAL_INCLUDES) \
		-o $@ $(PERF_BENCHMARK_SRC) $(MATCHING_ENGINE_SRCS)

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

# Build execution costs test in debug mode 
.PHONY: debug-execution-costs
debug-execution-costs: | $(BUILD_DIR)
	$(CXX) $(DEBUG_FLAGS) -I$(INCLUDE_DIR) -I$(MATCHING_ENGINE_INCLUDE) \
		-o $(BUILD_DIR)/test_execution_costs_debug $(EXECUTION_COSTS_TEST_SRC) $(MATCHING_ENGINE_SRCS)

# ============================================================
# Test Targets
# ============================================================

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

# Run execution costs tests
.PHONY: test-execution-costs
test-execution-costs: $(EXECUTION_COSTS_TEST)
	@echo "=== Running Execution Costs Tests  ==="
	$(EXECUTION_COSTS_TEST)
	@echo ""

# Run performance benchmarks (Week 4.2)
.PHONY: test-performance
test-performance: $(PERF_BENCHMARK)
	@echo "=== Running Performance Benchmarks (Week 4.2) ==="
	$(PERF_BENCHMARK)
	@echo ""

# Run platform demo with benchmarks (Week 4.2)
.PHONY: run-benchmark
run-benchmark: $(PLATFORM_DEMO)
	@echo "=== Running Performance Benchmark Demo (Week 4.2) ==="
	$(PLATFORM_DEMO) --benchmark
	@echo ""

# Run platform integration tests
.PHONY: test-platform
test-platform: $(PLATFORM_TEST)
	@echo "=== Running Platform Integration Tests  ==="
	$(PLATFORM_TEST)
	@echo ""

# Run all tests
.PHONY: test
test: test-backtester test-orderbook test-flow test-calibration test-twap test-execution-costs test-performance
	@echo "=== All Tests Complete ==="

# Run all tests including platform integration
.PHONY: test-all
test-all: test test-platform
	@echo "=== All Tests Including Platform Complete ==="

# Clean build artifacts
.PHONY: clean
clean:
	rm -rf $(BUILD_DIR)

# Install (copy to /usr/local/bin)
.PHONY: install
install: $(BACKTESTER) $(PLATFORM_DEMO)
	cp $(BACKTESTER) /usr/local/bin/microstructure-backtester
	cp $(PLATFORM_DEMO) /usr/local/bin/microstructure-platform

# ============================================================
# Help
# ============================================================

.PHONY: help
help:
	@echo "Microstructure Engine - Build Targets"
	@echo ""
	@echo "Core Builds:"
	@echo "  make              - Build all components"
	@echo "  make debug        - Build backtester in debug mode"
	@echo "  make clean        - Remove build artifacts"
	@echo "  make install      - Install to /usr/local/bin"
	@echo ""
	@echo "Platform Integration:"
	@echo "  make platform_demo      - Build platform demo"
	@echo "  make run-platform       - Run platform demo"
	@echo "  make run-platform-verbose - Run platform demo with verbose output"
	@echo "  make debug-platform     - Build platform demo in debug mode"
	@echo "  make test-platform      - Run platform integration tests"
	@echo ""
	@echo "Week 4.2: Performance Optimization:"
	@echo "  make run-benchmark      - Run performance benchmark demo"
	@echo "  make test-performance   - Run performance benchmark tests"
	@echo ""
	@echo "Debug Builds:"
	@echo "  make debug-orderbook    - Build order book test in debug mode"
	@echo "  make debug-flow         - Build flow tracking test in debug mode"
	@echo "  make debug-calibration  - Build calibration test in debug mode"
	@echo "  make debug-twap         - Build TWAP test in debug mode"
	@echo "  make debug-execution-costs - Build execution costs test in debug mode"
	@echo ""
	@echo "Test Targets:"
	@echo "  make test               - Run all standard tests"
	@echo "  make test-all           - Run all tests including platform"
	@echo "  make test-backtester    - Run backtester tests only"
	@echo "  make test-orderbook     - Run order book tests only"
	@echo "  make test-flow          - Run flow tracking tests only"
	@echo "  make test-calibration   - Run calibration tests only"
	@echo "  make test-twap          - Run TWAP strategy tests only"
	@echo "  make test-execution-costs - Run execution costs tests"
	@echo "  make test-performance   - Run performance benchmarks"
	@echo ""
	@echo "Executables:"
	@echo "  ./build/backtester [options] filename.csv"
	@echo "  ./build/platform_demo [options] filename.csv"
	@echo "  ./build/platform_demo --benchmark  (Week 4.2 benchmarks)"
	@echo "  ./build/test_order_book"
	@echo "  ./build/test_flow_tracking"
	@echo "  ./build/test_calibration"
	@echo "  ./build/test_twap"
	@echo "  ./build/test_execution_costs"
	@echo "  ./build/test_performance_benchmarks"
	@echo "  ./build/test_platform"
