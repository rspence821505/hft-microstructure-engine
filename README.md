# HFT Microstructure Engine

[![C++17](https://img.shields.io/badge/C%2B%2B-17-blue.svg)](https://isocpp.org/std/the-standard)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![Build Status](https://img.shields.io/badge/build-passing-brightgreen.svg)]()
[![Platform](https://img.shields.io/badge/platform-macOS%20%7C%20Linux-lightgrey.svg)]()

A high-performance C++17 market microstructure analytics platform for systematic trading and algorithmic execution analysis. Features sub-microsecond latencies, lock-free data structures, and comprehensive market impact modeling.

## Features

- **High-Performance Order Book** - Sub-microsecond order insertion with >1M orders/sec throughput
- **Market Impact Modeling** - Square-root law calibration with permanent/temporary impact separation
- **Execution Algorithms** - TWAP strategy with backtesting and implementation shortfall measurement
- **Real-Time Analytics** - Lock-free queues for multi-feed aggregation at <100ns median latency
- **Performance Monitoring** - Nanosecond-precision latency tracking with percentile statistics

## Performance Targets

| Component             | p50 Latency | p99 Latency | Throughput    |
| --------------------- | ----------- | ----------- | ------------- |
| Order Book Update     | <1μs        | <3μs        | >1M ops/sec   |
| Analytics Computation | <500ns      | <1μs        | -             |
| Queue Handoff         | <100ns      | <500ns      | >10M ops/sec  |
| End-to-End Pipeline   | <10μs       | <50μs       | -             |
| CSV Parsing           | -           | -           | 417K rows/sec |

## Quick Start

### Build

```bash
make              # Build all components
make test         # Run all tests
make test-performance  # Run performance benchmarks
```

### Run Platform Demo

```bash
# Historical analysis
./build/platform_demo data/calibration_test.csv

# Performance benchmarks
./build/platform_demo --benchmark

# Verbose output
./build/platform_demo --verbose data/calibration_test.csv
```

### Run Backtester

```bash
./build/backtester --impact --stats data/calibration_test.csv
./build/backtester --symbol=AAPL data/calibration_test.csv
```

## Architecture

```
hft-microstructure-engine/
├── include/
│   ├── order_book/       # Core matching engine with microstructure tracking
│   ├── analytics/        # Real-time analytics and market impact modeling
│   ├── execution/        # TWAP and execution algorithm framework
│   ├── queues/           # Lock-free SPSC/SPMC queues and memory pools
│   ├── csv/              # Historical data parsing and backtesting
│   ├── networking/       # Multi-feed aggregation and protocols
│   └── platform/         # Main integration layer
├── src/                  # Implementation files
├── tests/                # Test suite
├── benchmarks/           # Performance benchmarks
├── examples/             # Demo applications
└── data/                 # Sample market data
```

## Components

### Order Book

High-performance matching engine with integrated microstructure analytics:

```cpp
#include "microstructure_order_book.hpp"

MicrostructureOrderBook book("AAPL");

// Add orders
Order buy_order(1, Side::BUY, 150.00, 100, TimeInForce::GTC);
book.add_order(buy_order);

// Get analytics
double avg_spread = book.get_average_spread();
double imbalance = book.get_current_imbalance();
double volume_ratio = book.get_volume_ratio();
```

**Features:**

- Multiple order types: LIMIT, MARKET
- Time-in-force: GTC, IOC, FOK, DAY
- Iceberg orders and stop orders (stop-market, stop-limit)
- Self-trade prevention
- Maker/taker fee schedules

### Market Impact Model

Square-root law implementation for execution cost estimation:

```cpp
#include "market_impact_calibration.hpp"

MarketImpactModel model;
model.calibrate(historical_fills, adv);

// Estimate impact for 100,000 share order
double total_impact = model.estimate_total_impact(100000, adv);
double permanent = model.estimate_permanent_impact(100000, adv);
double temporary = model.estimate_temporary_impact(100000, adv);
```

**Impact Formula:** `Impact = coefficient × (volume/ADV)^0.5`

### Execution Algorithms

TWAP strategy with backtesting framework:

```cpp
#include "twap_strategy.hpp"
#include "execution_simulator.hpp"

TWAPConfig config;
config.total_quantity = 10000;
config.num_slices = 10;
config.use_limit_orders = true;

TWAPStrategy twap(config);
ExecutionSimulator simulator(impact_model);

auto result = simulator.run(twap, market_events);
std::cout << "Implementation Shortfall: " << result.shortfall_bps << " bps\n";
```

### Lock-Free Queues

Sub-microsecond inter-thread communication:

```cpp
#include "spsc_queue.hpp"

SPSCQueue<MarketTick, 65536> queue;

// Producer thread
queue.push(tick);

// Consumer thread
MarketTick tick;
if (queue.pop(tick)) {
    process(tick);
}
```

### Performance Monitor

Nanosecond-precision latency tracking:

```cpp
#include "performance_monitor.hpp"

ComponentLatencyTracker tracker;

auto start = std::chrono::steady_clock::now();
// ... operation ...
auto end = std::chrono::steady_clock::now();
tracker.order_book().record(end - start);

// Get statistics
tracker.order_book().print_statistics();
tracker.verify_performance_targets();
```

## Build Targets

### Core Builds

```bash
make                    # Build all components
make clean              # Remove build artifacts
make debug              # Build in debug mode
```

### Test Targets

```bash
make test               # Run all standard tests
make test-all           # Run all tests including platform
make test-orderbook     # Order book analytics
make test-flow          # Order flow tracking
make test-calibration   # Market impact calibration
make test-twap          # TWAP strategy
make test-execution-costs  # Execution cost measurement
make test-performance   # Performance benchmarks
make test-platform      # Platform integration
```

### Platform

```bash
make platform_demo      # Build platform demo
make run-platform       # Run platform demo
make run-benchmark      # Run performance benchmarks
```

## Data Format

CSV input format with nanosecond timestamp support:

```csv
timestamp,symbol,price,volume
2024-01-15 09:30:00.123456789,AAPL,150.25,1000
2024-01-15 09:30:00.234567890,AAPL,150.26,500
```

**Supported Timestamp Precision:**

- Second: `2024-01-15 09:30:00`
- Millisecond: `2024-01-15 09:30:00.123`
- Microsecond: `2024-01-15 09:30:00.123456`
- Nanosecond: `2024-01-15 09:30:00.123456789`

## Platform Demo

The platform demo showcases integrated functionality:

```bash
./build/platform_demo --help

Options:
  --historical-only    Run only historical analysis (default)
  --realtime           Start real-time mode (requires feed server)
  --benchmark          Run performance benchmarks
  --verbose            Enable verbose output
  --help               Show help
```

### Example Output

```
##############################################################
#                                                            #
#      PERFORMANCE OPTIMIZATION BENCHMARKS                   #
#                                                            #
##############################################################

=== Test 1: Order Book Update Latency ===
Target: p50 < 1us, p99 < 2us, throughput > 1M/sec
  Results:
    p50: 550 ns
    p99: 2450 ns
    Throughput: 1318529 ops/sec
  [PASS] Order book p50 latency < 1us
  [PASS] Order book throughput > 1M ops/sec

========================================
      PERFORMANCE BENCHMARK SUMMARY
========================================
  Tests passed: 14
  Tests failed: 0
  Total:        14
========================================
  ALL PERFORMANCE TARGETS MET!
========================================
```

## Requirements

- **Compiler:** g++ or clang with C++17 support
- **Platform:** macOS or Linux
- **Build Tool:** Make

No external library dependencies - all data structures and algorithms are self-contained.

## License

MIT License - see [LICENSE](LICENSE) for details.

<!-- ## Project Structure

| Directory | Description |
|-----------|-------------|
| `include/order_book/` | Order matching engine and fill routing |
| `include/analytics/` | Market impact, flow tracking, statistics |
| `include/execution/` | TWAP and execution algorithm framework |
| `include/queues/` | Lock-free SPSC/SPMC queues, memory pools |
| `include/csv/` | CSV parsing and backtesting |
| `include/networking/` | Multi-feed aggregation, protocols |
| `include/platform/` | Main platform integration |
| `tests/` | Unit and integration tests |
| `benchmarks/` | Performance benchmark suite |
| `examples/` | Demo applications |
| `data/` | Sample market data files | -->
