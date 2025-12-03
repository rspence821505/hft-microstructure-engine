/**
 * @file platform_demo.cpp
 * @brief Platform Demo with Performance Optimization
 *
 * This program demonstrates the full integration of the Microstructure
 * Analytics Platform, showcasing:
 *
 * 1. Historical Data Analysis
 *    - CSV parsing and event timeline construction
 *    - Market impact model calibration
 *    - Statistical analysis
 *
 * 2. Execution Strategy Testing
 *    - TWAP strategy backtesting
 *    - Implementation shortfall measurement
 *    - Algorithm comparison
 *
 * 3. Real-Time Mode (when feeds are available)
 *    - Multi-feed aggregation
 *    - Live analytics updates
 *    - Performance monitoring
 *
 * 4. Performance Optimization
 *    - Latency histogram benchmarking
 *    - Throughput measurement
 *    - Target verification
 *    - Component-level profiling
 *
 * Usage:
 *   ./platform_demo [options] [historical_file.csv]
 *
 * Options:
 *   --historical-only    Run only historical analysis
 *   --realtime           Start real-time mode (requires feeds)
 *   --benchmark          Run performance benchmarks
 *   --verbose            Enable verbose output
 *   --help               Show this help
 */

#include "memory_pool.hpp"
#include "microstructure_platform.hpp"

#include <iostream>
#include <random>
#include <string>

void print_usage(const char* program) {
    std::cout << "Usage: " << program << " [options] [historical_file.csv]\n\n";
    std::cout << "Microstructure Analytics Platform Demo\n\n";
    std::cout << "Options:\n";
    std::cout << "  --historical-only    Run only historical analysis (default)\n";
    std::cout << "  --realtime           Start real-time mode (requires mock server)\n";
    std::cout << "  --benchmark          Run performance benchmarks\n";
    std::cout << "  --verbose            Enable verbose output\n";
    std::cout << "  --help               Show this help\n\n";
    std::cout << "Examples:\n";
    std::cout << "  " << program << " data/calibration_test.csv\n";
    std::cout << "  " << program << " --verbose data/calibration_test.csv\n";
    std::cout << "  " << program << " --benchmark  (run performance benchmarks)\n";
    std::cout << "  " << program << " --realtime  (connects to localhost:9000)\n";
}

void demo_historical_analysis(MicrostructureAnalyticsPlatform& platform,
                              const std::string& filename) {
    std::cout << "\n";
    std::cout << "============================================================\n";
    std::cout << "           DEMO 1: HISTORICAL MICROSTRUCTURE ANALYSIS       \n";
    std::cout << "============================================================\n";

    // Load and analyze historical data
    platform.analyze_historical_data(filename);
}

void demo_execution_strategies(MicrostructureAnalyticsPlatform& platform,
                               const std::string& filename) {
    std::cout << "\n";
    std::cout << "============================================================\n";
    std::cout << "           DEMO 2: EXECUTION STRATEGY COMPARISON            \n";
    std::cout << "============================================================\n";

    // Test execution strategies
    auto comparison = platform.test_execution_strategies(filename);
    comparison.print();

    // Print individual strategy results
    std::cout << "\nDetailed Results:\n";
    for (const auto& result : comparison.results) {
        std::cout << "\n" << result.name << ":\n";
        std::cout << "  Average execution price: " << result.avg_price << "\n";
        std::cout << "  Implementation shortfall: " << result.implementation_shortfall_bps << " bps\n";
        std::cout << "  Fill rate: " << (result.fill_rate * 100) << "%\n";
        std::cout << "  Number of trades: " << result.num_trades << "\n";
    }
}

void demo_analytics_engine(MicrostructureAnalyticsPlatform& platform) {
    std::cout << "\n";
    std::cout << "============================================================\n";
    std::cout << "           DEMO 3: ANALYTICS ENGINE DEMONSTRATION           \n";
    std::cout << "============================================================\n";

    auto& analytics = platform.get_analytics();
    auto& order_book = platform.get_order_book();

    // Generate some synthetic trades for demonstration
    std::cout << "\nGenerating synthetic order flow...\n";

    int order_id = 1;
    for (int i = 0; i < 100; ++i) {
        int account_id = (i % 3) + 1;  // 3 accounts
        double price = 100.0 + (i % 10) * 0.01;
        int quantity = 100 + (i % 50) * 10;
        Side side = (i % 2 == 0) ? Side::BUY : Side::SELL;

        Order order(order_id++, account_id, side, price, quantity, TimeInForce::GTC);
        order_book.add_order(order);
    }

    // Print analytics
    order_book.print_analytics_summary();
    analytics.print_summary();

    // Print snapshot
    platform.print_analytics_snapshot();
}

void demo_realtime_mode(MicrostructureAnalyticsPlatform& platform) {
    std::cout << "\n";
    std::cout << "============================================================\n";
    std::cout << "           DEMO 4: REAL-TIME MODE                           \n";
    std::cout << "============================================================\n";

    std::cout << "\nNote: Real-time mode requires a running mock server.\n";
    std::cout << "Start the mock server with:\n";
    std::cout << "  cd ../TCP-Socket && make text_mock_server && ./text_mock_server 9000\n\n";

    // Add a feed
    platform.add_feed("Exchange1", "localhost", 9000);

    std::cout << "Attempting to connect to localhost:9000...\n";

    if (platform.start_real_time_mode()) {
        std::cout << "Connected! Receiving market data...\n";
        std::cout << "Press Ctrl+C to stop.\n\n";

        // Run for a short demo period or until stopped
        std::this_thread::sleep_for(std::chrono::seconds(5));

        platform.stop();

        std::cout << "\nReal-time mode stopped.\n";
        platform.print_analytics_snapshot();
    } else {
        std::cout << "Failed to connect. Make sure the mock server is running.\n";
        std::cout << "Skipping real-time demo.\n";
    }
}

void demo_performance_report(MicrostructureAnalyticsPlatform& platform) {
    std::cout << "\n";
    std::cout << "============================================================\n";
    std::cout << "           DEMO 5: FULL PLATFORM REPORT                     \n";
    std::cout << "============================================================\n";

    platform.print_full_report();
}

/**
 * @brief Performance Optimization Benchmarks
 *
 * This demo runs comprehensive performance benchmarks measuring:
 * - Order book update latency
 * - Analytics computation latency
 * - Queue handoff latency
 * - Memory pool allocation speed
 * - End-to-end pipeline latency
 */
void demo_performance_benchmarks(MicrostructureAnalyticsPlatform& platform) {
    std::cout << "\n";
    std::cout << "============================================================\n";
    std::cout << "           WEEK 4.2: PERFORMANCE OPTIMIZATION BENCHMARKS    \n";
    std::cout << "============================================================\n";

    ComponentLatencyTracker tracker;
    std::mt19937 rng(42);
    std::uniform_real_distribution<double> price_dist(99.0, 101.0);
    std::uniform_int_distribution<int> qty_dist(100, 1000);

    auto& order_book = platform.get_order_book();

    // ============================================================
    // Benchmark 1: Order Book Update Latency
    // Target: <1us per event
    // ============================================================
    std::cout << "\n--- Benchmark 1: Order Book Updates ---\n";
    std::cout << "Target: <1us per event, >1M ops/sec\n";
    std::cout << "Running 100,000 order insertions...\n";

    for (int i = 0; i < 100000; ++i) {
        auto start = std::chrono::steady_clock::now();

        double price = price_dist(rng);
        int quantity = qty_dist(rng);
        Side side = (i % 2 == 0) ? Side::BUY : Side::SELL;
        Order order(i + 1, 1, side, price, quantity, TimeInForce::GTC);
        order_book.add_order(order);

        auto end = std::chrono::steady_clock::now();
        auto latency = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
        tracker.order_book_update().record_event_latency(latency);
    }

    tracker.order_book_update().print_statistics();

    // ============================================================
    // Benchmark 2: Analytics Computation Latency
    // Target: <500ns per metric update
    // ============================================================
    std::cout << "\n--- Benchmark 2: Analytics Computation ---\n";
    std::cout << "Target: <500ns per metric update, >500K ops/sec\n";
    std::cout << "Running 100,000 analytics updates...\n";

    auto& analytics = platform.get_analytics();

    for (int i = 0; i < 100000; ++i) {
        auto start = std::chrono::steady_clock::now();

        // Simulate analytics computation (access various metrics)
        [[maybe_unused]] double imbalance = order_book.get_current_imbalance();
        [[maybe_unused]] double avg_spread = order_book.get_average_spread();
        [[maybe_unused]] double flow_imbalance = analytics.get_flow_imbalance();

        auto end = std::chrono::steady_clock::now();
        auto latency = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
        tracker.analytics_compute().record_event_latency(latency);
    }

    tracker.analytics_compute().print_statistics();

    // ============================================================
    // Benchmark 3: Lock-Free Queue Handoff
    // Target: <100ns median latency
    // ============================================================
    std::cout << "\n--- Benchmark 3: Lock-Free Queue Handoff ---\n";
    std::cout << "Target: <100ns median latency, >10M ops/sec\n";
    std::cout << "Running 1,000,000 queue operations...\n";

    // Use SPSC queue from TCP-Socket (included via multi_feed_aggregator)
    SPSCQueue<int> queue(4096);

    for (int i = 0; i < 1000000; ++i) {
        auto start = std::chrono::steady_clock::now();

        queue.push(i);
        auto val = queue.pop();
        (void)val;

        auto end = std::chrono::steady_clock::now();
        auto latency = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
        tracker.queue_handoff().record_event_latency(latency);
    }

    tracker.queue_handoff().print_statistics();

    // ============================================================
    // Benchmark 4: Memory Pool Allocation
    // ============================================================
    std::cout << "\n--- Benchmark 4: Memory Pool Allocation ---\n";
    std::cout << "Comparing arena allocator vs malloc...\n";

    // Arena allocator benchmark
    {
        ArenaAllocator arena;
        auto start = std::chrono::steady_clock::now();

        for (int i = 0; i < 100000; ++i) {
            void* ptr = arena.allocate(64);
            (void)ptr;
        }

        auto end = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
        double ns_per_alloc = static_cast<double>(duration.count()) / 100000.0;

        std::cout << "  Arena allocator: " << std::fixed << std::setprecision(1)
                  << ns_per_alloc << " ns/alloc\n";
        std::cout << "    Memory usage: " << arena.memory_usage() << " bytes\n";
        std::cout << "    Utilization: " << arena.utilization() << "%\n";
    }

    // Malloc benchmark
    {
        std::vector<void*> ptrs;
        ptrs.reserve(100000);

        auto start = std::chrono::steady_clock::now();

        for (int i = 0; i < 100000; ++i) {
            void* ptr = malloc(64);
            ptrs.push_back(ptr);
        }

        auto end = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
        double ns_per_alloc = static_cast<double>(duration.count()) / 100000.0;

        std::cout << "  malloc: " << std::fixed << std::setprecision(1)
                  << ns_per_alloc << " ns/alloc\n";

        for (void* ptr : ptrs) {
            free(ptr);
        }
    }

    // ============================================================
    // Benchmark 5: End-to-End Pipeline Latency
    // Target: <10us from market data to analytics result
    // ============================================================
    std::cout << "\n--- Benchmark 5: End-to-End Pipeline Latency ---\n";
    std::cout << "Target: <10us end-to-end, >100K events/sec\n";
    std::cout << "Running 50,000 complete pipeline cycles...\n";

    for (int i = 0; i < 50000; ++i) {
        auto start = std::chrono::steady_clock::now();

        // Simulate complete pipeline:
        // 1. Create order
        double price = price_dist(rng);
        int quantity = qty_dist(rng);
        Side side = (i % 2 == 0) ? Side::BUY : Side::SELL;
        Order order(200000 + i, 1, side, price, quantity, TimeInForce::GTC);

        // 2. Insert into order book (triggers matching)
        order_book.add_order(order);

        // 3. Read analytics results
        [[maybe_unused]] double imbalance = order_book.get_current_imbalance();
        [[maybe_unused]] double spread = order_book.get_average_spread();

        auto end = std::chrono::steady_clock::now();
        auto latency = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
        tracker.end_to_end().record_event_latency(latency);
    }

    tracker.end_to_end().print_statistics();

    // ============================================================
    // Summary and Target Verification
    // ============================================================
    std::cout << "\n";
    tracker.print_summary_table();
    tracker.verify_performance_targets();
}

int main(int argc, char* argv[]) {
    bool historical_only = true;
    bool realtime = false;
    bool benchmark = false;
    bool verbose = false;
    std::string filename = "";

    // Parse arguments
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "--help" || arg == "-h") {
            print_usage(argv[0]);
            return 0;
        } else if (arg == "--historical-only") {
            historical_only = true;
            realtime = false;
        } else if (arg == "--realtime") {
            realtime = true;
        } else if (arg == "--benchmark") {
            benchmark = true;
        } else if (arg == "--verbose") {
            verbose = true;
        } else if (arg[0] != '-') {
            filename = arg;
        } else {
            std::cerr << "Unknown option: " << arg << "\n";
            print_usage(argv[0]);
            return 1;
        }
    }

    // Default file if not specified
    if (filename.empty()) {
        filename = "data/calibration_test.csv";
    }

    std::cout << "\n";
    std::cout << "##############################################################\n";
    std::cout << "#                                                            #\n";
    std::cout << "#         MICROSTRUCTURE ANALYTICS PLATFORM                  #\n";
    std::cout << "#         Integration + Optimization                          #\n";
    std::cout << "#                                                            #\n";
    std::cout << "##############################################################\n";

    // Configure platform
    PlatformConfig config;
    config.historical_data_file = filename;
    config.verbose = verbose;
    config.assumed_adv = 10000000;
    config.flow_window_seconds = 60;
    config.enable_performance_monitoring = true;

    // Create and initialize platform
    MicrostructureAnalyticsPlatform platform(config);

    try {
        platform.initialize();

        // Run performance benchmarks if requested
        if (benchmark) {
            demo_performance_benchmarks(platform);
        } else {
            // Demo 1: Historical Analysis
            demo_historical_analysis(platform, filename);

            // Demo 2: Execution Strategy Testing
            demo_execution_strategies(platform, filename);

            // Demo 3: Analytics Engine
            demo_analytics_engine(platform);

            // Demo 4: Real-Time Mode (optional)
            if (realtime) {
                demo_realtime_mode(platform);
            }

            // Demo 5: Full Report
            demo_performance_report(platform);
        }

        std::cout << "\n";
        std::cout << "##############################################################\n";
        std::cout << "#                                                            #\n";
        std::cout << "#         PLATFORM DEMO COMPLETE                             #\n";
        std::cout << "#                                                            #\n";
        std::cout << "##############################################################\n";

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
