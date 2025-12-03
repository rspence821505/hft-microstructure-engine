/**
 * @file test_platform_integration.cpp
 * @brief Week 4.1: Platform Integration Tests
 *
 * Tests the full integration of the Microstructure Analytics Platform,
 * verifying that all components work together correctly.
 */

#include "microstructure_platform.hpp"

#include <cassert>
#include <cmath>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

// Test utilities
static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) \
    void name(); \
    struct name##_registrar { \
        name##_registrar() { \
            std::cout << "Running " << #name << "... "; \
            tests_run++; \
            try { \
                name(); \
                tests_passed++; \
                std::cout << "PASSED\n"; \
            } catch (const std::exception& e) { \
                std::cout << "FAILED: " << e.what() << "\n"; \
            } catch (...) { \
                std::cout << "FAILED: Unknown exception\n"; \
            } \
        } \
    } name##_instance; \
    void name()

#define ASSERT_TRUE(x) \
    if (!(x)) throw std::runtime_error("Assertion failed: " #x)

#define ASSERT_FALSE(x) \
    if (x) throw std::runtime_error("Assertion failed: !" #x)

#define ASSERT_EQ(a, b) \
    if ((a) != (b)) { \
        std::ostringstream oss; \
        oss << "Assertion failed: " << #a << " == " << #b << " (" << (a) << " != " << (b) << ")"; \
        throw std::runtime_error(oss.str()); \
    }

#define ASSERT_NEAR(a, b, tol) \
    if (std::abs((a) - (b)) > (tol)) { \
        std::ostringstream oss; \
        oss << "Assertion failed: " << #a << " ~= " << #b << " (diff=" << std::abs((a) - (b)) << ")"; \
        throw std::runtime_error(oss.str()); \
    }

#define ASSERT_GT(a, b) \
    if (!((a) > (b))) { \
        std::ostringstream oss; \
        oss << "Assertion failed: " << #a << " > " << #b << " (" << (a) << " <= " << (b) << ")"; \
        throw std::runtime_error(oss.str()); \
    }

// Helper to create test CSV file
std::string create_test_csv() {
    std::string path = "/tmp/test_platform_data.csv";
    std::ofstream file(path);
    file << "timestamp,symbol,price,volume\n";

    // Generate 100 test trades
    double price = 100.0;
    for (int i = 0; i < 100; ++i) {
        // Add some price variation
        price += (i % 2 == 0) ? 0.01 : -0.005;
        int volume = 100 + (i % 10) * 50;

        file << "2024-01-15 09:30:" << std::setfill('0') << std::setw(2) << (i % 60)
             << ",AAPL," << std::fixed << std::setprecision(2) << price
             << "," << volume << "\n";
    }

    file.close();
    return path;
}

// ============================================================
// Platform Initialization Tests
// ============================================================

TEST(test_platform_construction) {
    PlatformConfig config;
    MicrostructureAnalyticsPlatform platform(config);
    // Should not throw
}

TEST(test_platform_initialization) {
    PlatformConfig config;
    config.verbose = false;
    config.assumed_adv = 10000000;

    MicrostructureAnalyticsPlatform platform(config);
    platform.initialize();

    // Verify components are accessible
    auto& order_book = platform.get_order_book();
    auto& analytics = platform.get_analytics();
    auto& simulator = platform.get_simulator();

    // Basic checks
    ASSERT_EQ(order_book.get_order_count(), 0u);
    ASSERT_EQ(analytics.get_total_fills_processed(), 0u);
}

TEST(test_platform_config_propagation) {
    PlatformConfig config;
    config.flow_window_seconds = 120;
    config.track_per_symbol = true;
    config.assumed_adv = 5000000;
    config.verbose = false;

    MicrostructureAnalyticsPlatform platform(config);
    platform.initialize();

    // ADV should propagate to impact calculations
    // (Verify by running a calibration later)
}

// ============================================================
// Historical Analysis Tests
// ============================================================

TEST(test_historical_data_loading) {
    std::string test_file = create_test_csv();

    PlatformConfig config;
    config.verbose = false;

    MicrostructureAnalyticsPlatform platform(config);
    platform.initialize();
    platform.load_historical_data(test_file);

    auto& backtester = platform.get_backtester();
    ASSERT_GT(backtester.timeline_size(), 0u);
    ASSERT_EQ(backtester.timeline_size(), 100u);  // We created 100 events
}

TEST(test_impact_model_calibration) {
    std::string test_file = create_test_csv();

    PlatformConfig config;
    config.assumed_adv = 10000000;
    config.verbose = false;

    MicrostructureAnalyticsPlatform platform(config);
    platform.initialize();
    platform.load_historical_data(test_file);

    auto impact_model = platform.calibrate_impact_model("AAPL");

    // Model should have valid parameters
    const auto& params = impact_model.get_parameters();
    ASSERT_GT(params.permanent_impact_coeff, 0.0);
}

// ============================================================
// Analytics Engine Tests
// ============================================================

TEST(test_analytics_order_processing) {
    PlatformConfig config;
    config.verbose = false;

    MicrostructureAnalyticsPlatform platform(config);
    platform.initialize();

    auto& order_book = platform.get_order_book();
    auto& analytics = platform.get_analytics();

    // Add some orders
    for (int i = 1; i <= 10; ++i) {
        Side side = (i % 2 == 0) ? Side::BUY : Side::SELL;
        double price = 100.0 + i * 0.01;
        Order order(i, 1, side, price, 100, TimeInForce::GTC);
        order_book.add_order(order);
    }

    // Verify analytics tracked the orders
    ASSERT_EQ(order_book.get_order_count(), 10u);
    ASSERT_GT(order_book.get_total_buy_volume(), 0u);
    ASSERT_GT(order_book.get_total_sell_volume(), 0u);
}

TEST(test_spread_analytics) {
    PlatformConfig config;
    config.verbose = false;

    MicrostructureAnalyticsPlatform platform(config);
    platform.initialize();

    auto& order_book = platform.get_order_book();

    // Add bid and ask
    Order bid(1, 1, Side::BUY, 99.95, 100, TimeInForce::GTC);
    Order ask(2, 2, Side::SELL, 100.05, 100, TimeInForce::GTC);

    order_book.add_order(bid);
    order_book.add_order(ask);

    // Check spread
    auto spread = order_book.get_current_spread();
    ASSERT_TRUE(spread.has_value());
    ASSERT_NEAR(*spread, 0.10, 0.001);
}

TEST(test_imbalance_analytics) {
    PlatformConfig config;
    config.verbose = false;

    MicrostructureAnalyticsPlatform platform(config);
    platform.initialize();

    auto& order_book = platform.get_order_book();

    // Add asymmetric book (more bids)
    Order bid1(1, 1, Side::BUY, 99.95, 200, TimeInForce::GTC);
    Order bid2(2, 2, Side::BUY, 99.90, 100, TimeInForce::GTC);
    Order ask(3, 3, Side::SELL, 100.05, 100, TimeInForce::GTC);

    order_book.add_order(bid1);
    order_book.add_order(bid2);
    order_book.add_order(ask);

    // Imbalance should be positive (more bids)
    double imbalance = order_book.get_current_imbalance();
    ASSERT_GT(imbalance, 0.0);
}

// ============================================================
// Execution Strategy Tests
// ============================================================

TEST(test_execution_strategy_comparison) {
    std::string test_file = create_test_csv();

    PlatformConfig config;
    config.assumed_adv = 10000000;
    config.verbose = false;

    MicrostructureAnalyticsPlatform platform(config);
    platform.initialize();

    auto comparison = platform.test_execution_strategies(test_file);

    // Should have results for multiple strategies
    ASSERT_GT(comparison.results.size(), 0u);

    // Each result should have valid data
    for (const auto& result : comparison.results) {
        ASSERT_FALSE(result.name.empty());
        // Avg price should be in reasonable range
        ASSERT_GT(result.avg_price, 0.0);
    }
}

// ============================================================
// Performance Monitor Tests
// ============================================================

TEST(test_performance_monitor_basic) {
    PlatformConfig config;
    config.enable_performance_monitoring = true;
    config.verbose = false;

    MicrostructureAnalyticsPlatform platform(config);
    platform.initialize();

    auto& monitor = platform.get_performance_monitor();

    // Record some events
    for (int i = 0; i < 1000; ++i) {
        monitor.record_event_latency(100 + (i % 200));  // 100-300 ns
    }

    ASSERT_EQ(monitor.events_processed(), 1000u);
    ASSERT_GT(monitor.throughput(), 0.0);

    // Percentiles should be reasonable
    uint64_t p50 = monitor.latency_percentile(50);
    ASSERT_GT(p50, 0u);
}

TEST(test_performance_monitor_component_timing) {
    PerformanceMonitor monitor;

    // Simulate component timing
    monitor.record_component_time("parsing", 500);
    monitor.record_component_time("parsing", 600);
    monitor.record_component_time("matching", 200);

    // Just verify it doesn't crash
    monitor.print_statistics();
}

// ============================================================
// Snapshot Tests
// ============================================================

TEST(test_analytics_snapshot) {
    std::string test_file = create_test_csv();

    PlatformConfig config;
    config.verbose = false;

    MicrostructureAnalyticsPlatform platform(config);
    platform.initialize();

    // Add some orders
    auto& order_book = platform.get_order_book();

    Order bid(1, 1, Side::BUY, 99.95, 100, TimeInForce::GTC);
    Order ask(2, 2, Side::SELL, 100.05, 100, TimeInForce::GTC);

    order_book.add_order(bid);
    order_book.add_order(ask);

    // Get snapshot
    auto snapshot = platform.get_snapshot();

    ASSERT_NEAR(snapshot.spread, 0.10, 0.001);
    ASSERT_NEAR(snapshot.best_bid, 99.95, 0.001);
    ASSERT_NEAR(snapshot.best_ask, 100.05, 0.001);
}

// ============================================================
// Multi-Feed Aggregator Tests
// ============================================================

TEST(test_feed_aggregator_construction) {
    MultiFeedAggregator aggregator;
    ASSERT_EQ(aggregator.feed_count(), 0u);
}

TEST(test_feed_aggregator_add_feeds) {
    MultiFeedAggregator aggregator;

    aggregator.add_feed("Exchange1", "localhost", 9000);
    aggregator.add_feed("Exchange2", "localhost", 9001);

    ASSERT_EQ(aggregator.feed_count(), 2u);
}

TEST(test_feed_aggregator_stats) {
    MultiFeedAggregator aggregator;
    aggregator.add_feed("TestFeed", "localhost", 9000);

    auto& stats = aggregator.get_feed_stats(0);
    ASSERT_EQ(stats.name, "TestFeed");
    ASSERT_FALSE(stats.connected);  // Not started yet
}

// ============================================================
// End-to-End Integration Tests
// ============================================================

TEST(test_full_historical_workflow) {
    std::string test_file = create_test_csv();

    PlatformConfig config;
    config.historical_data_file = test_file;
    config.assumed_adv = 10000000;
    config.verbose = false;

    MicrostructureAnalyticsPlatform platform(config);
    platform.initialize();

    // 1. Load historical data
    platform.load_historical_data(test_file);
    ASSERT_EQ(platform.get_backtester().timeline_size(), 100u);

    // 2. Calibrate impact model
    auto impact = platform.calibrate_impact_model("AAPL");
    ASSERT_GT(impact.get_parameters().permanent_impact_coeff, 0.0);

    // 3. Test strategies
    auto comparison = platform.test_execution_strategies(test_file);
    ASSERT_GT(comparison.results.size(), 0u);

    // 4. Generate synthetic order flow
    auto& order_book = platform.get_order_book();
    for (int i = 1; i <= 20; ++i) {
        Side side = (i % 2 == 0) ? Side::BUY : Side::SELL;
        double price = 100.0 + (i % 10) * 0.01;
        Order order(i, 1, side, price, 100, TimeInForce::GTC);
        order_book.add_order(order);
    }

    // 5. Get analytics snapshot
    auto snapshot = platform.get_snapshot();
    // Note: events_processed counts feed ticks, not direct order book operations
    // Verify order book has orders instead
    ASSERT_EQ(order_book.get_order_count(), 20u);

    // 6. Print full report (should not crash)
    // platform.print_full_report();  // Commented to reduce test output
}

// ============================================================
// Main
// ============================================================

int main() {
    std::cout << "\n";
    std::cout << "============================================================\n";
    std::cout << "  WEEK 4.1: PLATFORM INTEGRATION TESTS\n";
    std::cout << "============================================================\n\n";

    // Tests are auto-registered and run via static initialization

    std::cout << "\n============================================================\n";
    std::cout << "  RESULTS: " << tests_passed << "/" << tests_run << " tests passed\n";
    std::cout << "============================================================\n\n";

    return (tests_passed == tests_run) ? 0 : 1;
}
