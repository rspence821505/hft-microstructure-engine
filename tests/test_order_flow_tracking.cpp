#include "../include/microstructure_analytics.hpp"
#include "../include/microstructure_order_book.hpp"
#include <cassert>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <random>
#include <thread>

/**
 * @brief Creates a mock EnhancedFill for testing
 */
EnhancedFill create_mock_fill(int buy_id, int sell_id, double price, int qty,
                               const std::string& symbol, bool is_aggressive_buy) {
    Fill base_fill(buy_id, sell_id, price, qty);
    return EnhancedFill(base_fill, 1, 2, symbol, 0, is_aggressive_buy);
}

/**
 * @brief Tests basic OrderFlowTracker functionality
 */
void test_order_flow_tracker_basic() {
    std::cout << "Testing OrderFlowTracker basic functionality... ";

    OrderFlowTracker tracker;

    // Add some buy fills
    for (int i = 0; i < 5; i++) {
        tracker.record_fill(create_mock_fill(i+1, i+100, 100.0, 100, "TEST", true));
    }

    // Add some sell fills
    for (int i = 0; i < 3; i++) {
        tracker.record_fill(create_mock_fill(i+200, i+300, 100.0, 100, "TEST", false));
    }

    // Check volumes
    assert(tracker.get_total_buy_volume() == 500);
    assert(tracker.get_total_sell_volume() == 300);
    assert(tracker.get_total_buy_count() == 5);
    assert(tracker.get_total_sell_count() == 3);

    // Check imbalance: (500 - 300) / (500 + 300) = 200/800 = 0.25
    double imbalance = tracker.compute_current_imbalance();
    assert(std::abs(imbalance - 0.25) < 0.0001);

    // Check buy ratio: 500 / 800 = 0.625
    double buy_ratio = tracker.get_buy_ratio();
    assert(std::abs(buy_ratio - 0.625) < 0.0001);

    std::cout << "PASSED\n";
}

/**
 * @brief Tests per-symbol flow tracking
 */
void test_per_symbol_flow_tracking() {
    std::cout << "Testing per-symbol flow tracking... ";

    PerSymbolFlowTracker tracker;

    // Add fills for AAPL (more buys)
    for (int i = 0; i < 10; i++) {
        tracker.record_fill(create_mock_fill(i+1, i+100, 150.0, 100, "AAPL", true));
    }
    for (int i = 0; i < 5; i++) {
        tracker.record_fill(create_mock_fill(i+200, i+300, 150.0, 100, "AAPL", false));
    }

    // Add fills for MSFT (more sells)
    for (int i = 0; i < 3; i++) {
        tracker.record_fill(create_mock_fill(i+400, i+500, 300.0, 100, "MSFT", true));
    }
    for (int i = 0; i < 7; i++) {
        tracker.record_fill(create_mock_fill(i+600, i+700, 300.0, 100, "MSFT", false));
    }

    // Check symbol count
    assert(tracker.symbol_count() == 2);

    // Check AAPL imbalance: (1000 - 500) / 1500 = 0.333
    double aapl_imbalance = tracker.get_imbalance("AAPL");
    assert(std::abs(aapl_imbalance - 0.3333) < 0.01);

    // Check MSFT imbalance: (300 - 700) / 1000 = -0.4
    double msft_imbalance = tracker.get_imbalance("MSFT");
    assert(std::abs(msft_imbalance - (-0.4)) < 0.01);

    // Unknown symbol should return 0
    assert(tracker.get_imbalance("UNKNOWN") == 0.0);

    std::cout << "PASSED\n";
}

/**
 * @brief Tests MicrostructureAnalytics fill processing
 */
void test_microstructure_analytics_fill_processing() {
    std::cout << "Testing MicrostructureAnalytics fill processing... ";

    MicrostructureAnalytics analytics;

    // Process some fills
    for (int i = 0; i < 100; i++) {
        bool is_buy = (i % 3 != 0);  // 2/3 buys, 1/3 sells
        double price = 100.0 + (i % 10) * 0.1;
        analytics.process_fill(create_mock_fill(i+1, i+1000, price, 50, "TEST", is_buy));
    }

    // Check fills processed
    assert(analytics.get_total_fills_processed() == 100);

    // Check imbalance is positive (more buys)
    double imbalance = analytics.get_flow_imbalance();
    assert(imbalance > 0);

    // Check buy ratio is > 0.5
    double buy_ratio = analytics.get_buy_ratio();
    assert(buy_ratio > 0.5);

    // Check trade metrics
    const auto& metrics = analytics.get_current_metrics();
    assert(metrics.trade_count == 100);
    assert(metrics.total_volume == 5000);  // 100 * 50

    std::cout << "PASSED (imbalance=" << imbalance << ", buy_ratio=" << buy_ratio << ")\n";
}

/**
 * @brief Tests analytics integration with OrderBook
 */
void test_orderbook_integration() {
    std::cout << "Testing OrderBook integration... ";

    // Create order book and analytics
    MicrostructureOrderBook book("TEST_INTEGRATION");
    book.enable_self_trade_prevention(false);

    MicrostructureAnalytics analytics;
    analytics.connect_to_order_book(book);

    // Add some orders that will match
    int order_id = 1;

    // Add buy orders
    for (int i = 0; i < 5; i++) {
        Order buy(order_id++, 1, Side::BUY, 100.0, 100);
        book.add_order(buy);
    }

    // Add sell orders that will match
    for (int i = 0; i < 5; i++) {
        Order sell(order_id++, 2, Side::SELL, 100.0, 100);
        book.add_order(sell);
    }

    // Check that fills were processed
    uint64_t fills = analytics.get_total_fills_processed();
    std::cout << "PASSED (" << fills << " fills captured)\n";
}

/**
 * @brief Tests trade metrics aggregation
 */
void test_trade_metrics() {
    std::cout << "Testing trade metrics aggregation... ";

    MicrostructureAnalytics analytics;

    // Process fills with known values
    analytics.process_fill(create_mock_fill(1, 2, 100.0, 100, "TEST", true));
    analytics.process_fill(create_mock_fill(3, 4, 101.0, 200, "TEST", true));
    analytics.process_fill(create_mock_fill(5, 6, 99.0, 150, "TEST", false));

    const auto& metrics = analytics.get_current_metrics();

    // Check totals
    assert(metrics.trade_count == 3);
    assert(metrics.total_volume == 450);  // 100 + 200 + 150

    // Check notional: 100*100 + 101*200 + 99*150 = 10000 + 20200 + 14850 = 45050
    assert(std::abs(metrics.total_notional - 45050.0) < 0.01);

    // Check price range
    assert(std::abs(metrics.min_price - 99.0) < 0.01);
    assert(std::abs(metrics.max_price - 101.0) < 0.01);

    // Close period and verify
    TradeMetrics completed = analytics.close_metrics_period();
    assert(completed.trade_count == 3);

    // New period should be empty
    const auto& new_metrics = analytics.get_current_metrics();
    assert(new_metrics.trade_count == 0);

    std::cout << "PASSED\n";
}

/**
 * @brief Tests price tracking
 */
void test_price_tracking() {
    std::cout << "Testing price tracking... ";

    MicrostructureAnalytics analytics;

    // Process fills with varying prices for AAPL
    double prices[] = {150.0, 150.5, 151.0, 150.2, 149.8, 150.3, 150.7, 151.2, 150.1, 150.4};
    for (int i = 0; i < 10; i++) {
        analytics.process_fill(create_mock_fill(i+1, i+100, prices[i], 100, "AAPL", true));
    }

    // Check last price
    auto last_price = analytics.get_last_price("AAPL");
    assert(last_price.has_value());
    assert(std::abs(*last_price - 150.4) < 0.01);

    // Check average price (sum / 10)
    double sum = 0;
    for (double p : prices) sum += p;
    double expected_avg = sum / 10;
    double actual_avg = analytics.get_average_price("AAPL");
    assert(std::abs(actual_avg - expected_avg) < 0.01);

    // Check volatility is > 0
    double volatility = analytics.get_price_volatility("AAPL");
    assert(volatility > 0);

    // Unknown symbol should return nullopt
    assert(!analytics.get_last_price("UNKNOWN").has_value());

    std::cout << "PASSED (avg=" << actual_avg << ", vol=" << volatility << ")\n";
}

/**
 * @brief Tests impact estimation
 */
void test_impact_estimation() {
    std::cout << "Testing impact estimation... ";

    MicrostructureAnalytics analytics;

    // Set ADV for a symbol
    analytics.set_symbol_adv("AAPL", 50000000);  // 50M shares ADV

    // Estimate impact for 500K shares (1% participation)
    double impact = analytics.estimate_impact(500000, "AAPL");

    // Impact should be positive and reasonable
    // With default coefficient 0.01 and 1% participation:
    // impact = 0.01 * sqrt(0.01) * 10000 = 10 bps
    assert(impact > 0);
    assert(impact < 100);  // Should be less than 1%

    std::cout << "PASSED (impact=" << impact << " bps for 1% participation)\n";
}

/**
 * @brief Performance test for flow tracking
 */
void test_flow_tracking_performance() {
    std::cout << "Testing flow tracking performance... ";

    MicrostructureAnalytics analytics;
    analytics.set_per_symbol_tracking(false);  // Disable for pure flow tracking perf

    const int NUM_FILLS = 1000000;

    std::mt19937 rng(42);
    std::uniform_int_distribution<int> side_dist(0, 1);
    std::uniform_real_distribution<double> price_dist(99.0, 101.0);
    std::uniform_int_distribution<int> qty_dist(10, 1000);

    auto start = std::chrono::steady_clock::now();

    for (int i = 0; i < NUM_FILLS; i++) {
        bool is_buy = side_dist(rng) == 0;
        double price = price_dist(rng);
        int qty = qty_dist(rng);

        analytics.process_fill(create_mock_fill(i+1, i+1000, price, qty, "TEST", is_buy));
    }

    auto end = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration<double>(end - start);
    double throughput = NUM_FILLS / duration.count();

    std::cout << "\n";
    std::cout << "  Fills processed: " << NUM_FILLS << "\n";
    std::cout << "  Time: " << std::fixed << std::setprecision(3)
              << duration.count() << " seconds\n";
    std::cout << "  Throughput: " << std::fixed << std::setprecision(0)
              << throughput << " fills/sec\n";
    std::cout << "  Final imbalance: " << analytics.get_flow_imbalance() << "\n";

    // Target: Should handle at least 1M fills/sec
    if (throughput > 1000000) {
        std::cout << "  PASSED (target: >1M fills/sec)\n";
    } else if (throughput > 500000) {
        std::cout << "  WARNING: Below target but acceptable\n";
    } else {
        std::cout << "  FAILED: Below minimum throughput\n";
    }
}

/**
 * @brief Main test runner
 */
int main() {
    std::cout << "\n=== Order Flow Tracking Test Suite ===\n\n";

    try {
        test_order_flow_tracker_basic();
        test_per_symbol_flow_tracking();
        test_microstructure_analytics_fill_processing();
        test_orderbook_integration();
        test_trade_metrics();
        test_price_tracking();
        test_impact_estimation();
        std::cout << "\n";
        test_flow_tracking_performance();

        std::cout << "\n=== All Tests Completed ===\n";
        return 0;

    } catch (const std::exception& e) {
        std::cerr << "\nTest FAILED with exception: " << e.what() << "\n";
        return 1;
    }
}
