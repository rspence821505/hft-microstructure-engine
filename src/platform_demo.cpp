/**
 * @file platform_demo.cpp
 * @brief Week 4.1: Connect All Components - Demo Program
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
 * Usage:
 *   ./platform_demo [options] [historical_file.csv]
 *
 * Options:
 *   --historical-only    Run only historical analysis
 *   --realtime           Start real-time mode (requires feeds)
 *   --verbose            Enable verbose output
 *   --help               Show this help
 */

#include "microstructure_platform.hpp"

#include <iostream>
#include <string>

void print_usage(const char* program) {
    std::cout << "Usage: " << program << " [options] [historical_file.csv]\n\n";
    std::cout << "Week 4.1: Microstructure Analytics Platform Demo\n\n";
    std::cout << "Options:\n";
    std::cout << "  --historical-only    Run only historical analysis (default)\n";
    std::cout << "  --realtime           Start real-time mode (requires mock server)\n";
    std::cout << "  --verbose            Enable verbose output\n";
    std::cout << "  --help               Show this help\n\n";
    std::cout << "Examples:\n";
    std::cout << "  " << program << " tests/data/calibration_test.csv\n";
    std::cout << "  " << program << " --verbose tests/data/calibration_test.csv\n";
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

int main(int argc, char* argv[]) {
    bool historical_only = true;
    bool realtime = false;
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
        filename = "tests/data/calibration_test.csv";
    }

    std::cout << "\n";
    std::cout << "##############################################################\n";
    std::cout << "#                                                            #\n";
    std::cout << "#         MICROSTRUCTURE ANALYTICS PLATFORM                  #\n";
    std::cout << "#         Week 4.1: Connect All Components                   #\n";
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
