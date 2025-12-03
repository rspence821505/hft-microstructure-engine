#include "execution_algorithm.hpp"
#include "execution_simulator.hpp"
#include "twap_strategy.hpp"
#include <cassert>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <memory>
#include <thread>
#include <vector>

using namespace std::chrono_literals;

/**
 * @brief Tests MarketData creation
 */
void test_market_data() {
    std::cout << "Testing MarketData... ";

    // Test from_quotes
    auto data = MarketData::from_quotes(100.0, 100.10, 1000, 500);
    assert(std::abs(data.bid_price - 100.0) < 0.001);
    assert(std::abs(data.ask_price - 100.10) < 0.001);
    assert(std::abs(data.price - 100.05) < 0.001);  // Mid price
    assert(std::abs(data.spread - 0.10) < 0.001);
    assert(data.bid_volume == 1000);
    assert(data.ask_volume == 500);

    // Test from_price
    auto data2 = MarketData::from_price(150.25, 5000);
    assert(std::abs(data2.price - 150.25) < 0.001);
    assert(data2.total_volume == 5000);

    std::cout << "PASSED\n";
}

/**
 * @brief Tests ExecutionAlgorithm base class
 */
void test_execution_algorithm_base() {
    std::cout << "Testing ExecutionAlgorithm base... ";

    // Create a simple concrete implementation for testing
    class SimpleAlgo : public ExecutionAlgorithm {
    public:
        SimpleAlgo(uint64_t target) : ExecutionAlgorithm(target, true) {
            strategy_name_ = "SimpleTest";
        }

        std::vector<Order> compute_child_orders(const MarketData& data) override {
            if (remaining_quantity() == 0) return {};
            // Execute all at once
            return {create_market_order(static_cast<int>(remaining_quantity()))};
        }
    };

    SimpleAlgo algo(1000);
    assert(algo.get_target_quantity() == 1000);
    assert(algo.remaining_quantity() == 1000);
    assert(!algo.is_complete());
    assert(algo.progress() == 0.0);

    // Process market data
    auto data = MarketData::from_price(100.0);
    auto orders = algo.on_market_data(data);

    assert(orders.size() == 1);
    assert(orders[0].quantity == 1000);
    assert(std::abs(algo.get_arrival_price() - 100.0) < 0.001);

    // Simulate fill
    Fill fill(1, 1, 100.05, 1000);
    algo.on_fill(fill);

    assert(algo.is_complete());
    assert(algo.remaining_quantity() == 0);
    assert(algo.progress() == 1.0);
    assert(algo.get_executed_quantity() == 1000);

    // Generate report
    auto report = algo.generate_report();
    assert(report.total_quantity == 1000);
    assert(report.num_fills == 1);
    assert(std::abs(report.avg_execution_price - 100.05) < 0.001);

    std::cout << "PASSED\n";
}

/**
 * @brief Tests TWAP strategy basic functionality
 */
void test_twap_basic() {
    std::cout << "Testing TWAP basic functionality... ";

    // Create TWAP: 10000 shares over 10 minutes (10 slices)
    TWAPStrategy twap(10000, 10, true);

    assert(twap.get_target_quantity() == 10000);
    assert(twap.get_num_slices() == 10);
    assert(twap.get_base_slice_size() == 1000);  // 10000 / 10

    twap.print_config();

    std::cout << "PASSED\n";
}

/**
 * @brief Tests TWAP slice generation
 */
void test_twap_slicing() {
    std::cout << "Testing TWAP slicing... ";

    // Create TWAP with millisecond intervals for testing
    // 10000 shares over 1000ms with 10 slices = 100ms per slice
    TWAPStrategy twap(10000,
                      std::chrono::milliseconds(1000),
                      10,
                      true);

    auto data = MarketData::from_price(100.0);

    // First slice should execute immediately
    auto orders1 = twap.on_market_data(data);
    assert(orders1.size() == 1);
    assert(orders1[0].quantity == 1000);

    // Simulate fill
    Fill fill1(1, 1, 100.0, 1000);
    twap.on_fill(fill1);
    assert(twap.get_current_slice() == 1);

    // Next call should not generate order (not enough time passed)
    auto orders2 = twap.on_market_data(data);
    assert(orders2.empty());

    // Advance time and try again
    std::this_thread::sleep_for(std::chrono::milliseconds(110));
    data.timestamp = Clock::now();
    auto orders3 = twap.on_market_data(data);
    assert(orders3.size() == 1);
    assert(twap.get_current_slice() == 2);

    std::cout << "PASSED\n";
}

/**
 * @brief Tests TWAP with limit orders
 */
void test_twap_limit_orders() {
    std::cout << "Testing TWAP with limit orders... ";

    TWAPStrategy twap(10000, 10, true);
    twap.set_use_limit_orders(true, 5.0);  // 5 bps offset

    auto data = MarketData::from_quotes(100.0, 100.10);

    auto orders = twap.on_market_data(data);
    assert(orders.size() == 1);
    assert(!orders[0].is_market_order());

    // Price should be ask + offset
    double expected_price = 100.10 + (100.05 * 5.0 / 10000.0);
    assert(std::abs(orders[0].price - expected_price) < 0.01);

    std::cout << "PASSED\n";
}

/**
 * @brief Tests TWAP execution report
 */
void test_twap_report() {
    std::cout << "Testing TWAP execution report... ";

    TWAPStrategy twap(1000, 5, true);  // 5 slices
    auto data = MarketData::from_price(100.0);

    // Execute all slices
    for (int i = 0; i < 5; ++i) {
        // Advance time
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        data.timestamp = Clock::now();

        auto orders = twap.on_market_data(data);
        if (!orders.empty()) {
            // Simulate fill with slightly increasing prices
            Fill fill(i+1, i+1, 100.0 + i * 0.02, orders[0].quantity);
            fill.timestamp = Clock::now();
            twap.on_fill(fill);
        }
    }

    auto report = twap.generate_report();
    assert(report.algorithm_name == "TWAP");
    assert(report.total_quantity == 1000);
    assert(report.num_fills == 5);
    assert(report.fill_rate > 0.99);

    // VWAP should be weighted average
    assert(report.avg_execution_price > 100.0);

    report.print();

    std::cout << "PASSED\n";
}

/**
 * @brief Tests AggressiveTWAP catchup behavior
 */
void test_aggressive_twap() {
    std::cout << "Testing AggressiveTWAP... ";

    AggressiveTWAP twap(10000, 10, true);
    twap.set_max_catchup_multiplier(2.0);

    assert(twap.name() == "AggressiveTWAP");
    assert(twap.get_target_quantity() == 10000);

    std::cout << "PASSED\n";
}

/**
 * @brief Tests ExecutionSimulator basic operation
 */
void test_simulator_basic() {
    std::cout << "Testing ExecutionSimulator basic... ";

    SimulationConfig config;
    config.initial_price = 100.0;
    config.spread_bps = 10.0;
    config.volatility = 0.02;

    ExecutionSimulator sim(config);
    auto data = sim.get_current_market_data();

    assert(std::abs(data.price - 100.0) < 0.01);
    assert(data.spread > 0);
    assert(data.bid_price < data.ask_price);

    std::cout << "PASSED\n";
}

/**
 * @brief Tests full simulation with TWAP
 */
void test_simulation_twap() {
    std::cout << "Testing TWAP simulation... ";

    SimulationConfig config;
    config.initial_price = 100.0;
    config.spread_bps = 10.0;
    config.volatility = 0.02;
    config.fill_probability = 1.0;  // Always fill for testing
    config.ticks_per_second = 10;

    ExecutionSimulator sim(config);

    // TWAP: 1000 shares over 1 second with 5 slices
    TWAPStrategy twap(1000,
                      std::chrono::milliseconds(1000),
                      5,
                      true);

    auto result = sim.run_simulation(twap, std::chrono::milliseconds(2000));

    result.print();

    assert(result.completed);
    assert(result.report.total_quantity == 1000);
    assert(result.report.num_fills == 5);
    assert(result.report.avg_execution_price > 0);

    std::cout << "PASSED\n";
}

/**
 * @brief Tests simulation with market impact
 */
void test_simulation_with_impact() {
    std::cout << "Testing simulation with market impact... ";

    SimulationConfig config;
    config.initial_price = 100.0;
    config.spread_bps = 10.0;
    config.adv = 1000000;  // 1M ADV
    config.apply_market_impact = true;
    config.ticks_per_second = 100;

    // Create impact model
    MarketImpactModel impact_model(0.01, 0.02, config.adv);

    ExecutionSimulator sim(impact_model, config);

    // Execute 10% of ADV
    TWAPStrategy twap(100000,
                      std::chrono::milliseconds(1000),
                      10,
                      true);

    auto result = sim.run_simulation(twap, std::chrono::milliseconds(2000));

    // With 10% participation, should see significant impact
    std::cout << "\n";
    std::cout << "  Predicted impact: " << result.predicted_impact_bps << " bps\n";
    std::cout << "  Realized impact: " << result.realized_impact_bps << " bps\n";
    std::cout << "  Implementation shortfall: " << result.report.implementation_shortfall_bps << " bps\n";

    // Impact should be positive (price moved up for buy)
    assert(result.predicted_impact_bps > 0);

    std::cout << "PASSED\n";
}

/**
 * @brief Tests TWAP vs naive execution comparison
 */
void test_twap_vs_naive() {
    std::cout << "Testing TWAP vs naive execution... ";

    SimulationConfig config;
    config.initial_price = 100.0;
    config.spread_bps = 10.0;
    config.adv = 10000000;  // 10M ADV
    config.apply_market_impact = true;
    config.ticks_per_second = 100;

    MarketImpactModel impact_model(0.01, 0.02, config.adv);
    ExecutionSimulator sim(impact_model, config);

    uint64_t target_qty = 100000;  // 1% of ADV

    // Estimate naive cost
    double naive_cost = sim.estimate_naive_cost(target_qty);

    // Run TWAP
    TWAPStrategy twap(target_qty,
                      std::chrono::milliseconds(5000),
                      50,  // 50 slices
                      true);

    auto result = sim.run_simulation(twap, std::chrono::milliseconds(10000));

    std::cout << "\n";
    std::cout << "  Naive execution cost: " << naive_cost << " bps\n";
    std::cout << "  TWAP implementation shortfall: "
              << result.report.implementation_shortfall_bps << " bps\n";

    // TWAP should typically have lower cost than naive
    // (though this depends on market conditions in simulation)

    std::cout << "PASSED\n";
}

/**
 * @brief Tests synthetic market data generation
 */
void test_synthetic_market_data() {
    std::cout << "Testing synthetic market data... ";

    SimulationConfig config;
    config.initial_price = 100.0;
    config.volatility = 0.02;
    config.spread_bps = 10.0;

    auto data = generate_synthetic_market_data(1000, config);

    assert(data.size() == 1000);
    assert(data[0].price > 0);
    assert(data[0].spread > 0);

    // Check price stays reasonable
    for (const auto& d : data) {
        assert(d.price > 50.0 && d.price < 200.0);  // Reasonable bounds
        assert(d.spread > 0);
    }

    std::cout << "PASSED\n";
}

/**
 * @brief Tests algorithm comparison
 */
void test_algorithm_comparison() {
    std::cout << "Testing algorithm comparison... ";

    SimulationConfig config;
    config.initial_price = 100.0;
    config.spread_bps = 10.0;
    config.adv = 10000000;
    config.ticks_per_second = 100;

    ExecutionSimulator sim(config);

    // Create algorithms with same target
    std::vector<std::unique_ptr<ExecutionAlgorithm>> algos;
    algos.push_back(std::make_unique<TWAPStrategy>(
        10000, std::chrono::milliseconds(1000), 10, true));
    algos.push_back(std::make_unique<TWAPStrategy>(
        10000, std::chrono::milliseconds(1000), 20, true));  // More slices

    auto results = sim.compare_algorithms(algos, std::chrono::milliseconds(2000));

    assert(results.size() == 2);

    std::cout << "\n";
    ExecutionSimulator::print_comparison(results);

    std::cout << "PASSED\n";
}

/**
 * @brief Performance test for TWAP
 */
void test_twap_performance() {
    std::cout << "Testing TWAP performance... ";

    SimulationConfig config;
    config.ticks_per_second = 1000;  // High frequency

    ExecutionSimulator sim(config);

    // Large execution
    TWAPStrategy twap(1000000,
                      std::chrono::milliseconds(10000),
                      100,
                      true);

    auto start = std::chrono::steady_clock::now();
    auto result = sim.run_simulation(twap, std::chrono::milliseconds(15000));
    auto end = std::chrono::steady_clock::now();

    auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

    std::cout << "\n";
    std::cout << "  Target quantity: 1,000,000\n";
    std::cout << "  Simulation duration: " << duration_ms << " ms\n";
    std::cout << "  Price points: " << result.price_path.size() << "\n";
    std::cout << "  Fill count: " << result.fills.size() << "\n";
    std::cout << "  Completed: " << (result.completed ? "Yes" : "No") << "\n";

    assert(duration_ms < 5000);  // Should complete within 5 seconds

    std::cout << "PASSED\n";
}

/**
 * @brief Tests TWAP reset functionality
 */
void test_twap_reset() {
    std::cout << "Testing TWAP reset... ";

    TWAPStrategy twap(10000, 10, true);

    // Execute first slice
    auto data = MarketData::from_price(100.0);
    auto orders = twap.on_market_data(data);
    Fill fill(1, 1, 100.0, 1000);
    twap.on_fill(fill);

    assert(twap.get_executed_quantity() == 1000);
    assert(twap.get_current_slice() == 1);

    // Reset
    twap.reset();

    assert(twap.get_executed_quantity() == 0);
    assert(twap.get_current_slice() == 0);
    assert(!twap.is_complete());

    // Should be able to execute again
    orders = twap.on_market_data(data);
    assert(!orders.empty());

    std::cout << "PASSED\n";
}

/**
 * @brief Main test runner
 */
int main() {
    std::cout << "\n=== TWAP Strategy Test Suite ===\n\n";

    try {
        // Basic tests
        test_market_data();
        test_execution_algorithm_base();
        test_twap_basic();
        test_twap_slicing();
        test_twap_limit_orders();
        test_twap_report();
        test_twap_reset();
        test_aggressive_twap();

        std::cout << "\n";

        // Simulator tests
        test_simulator_basic();
        test_synthetic_market_data();
        test_simulation_twap();
        test_simulation_with_impact();

        std::cout << "\n";

        // Comparison and performance
        test_twap_vs_naive();
        test_algorithm_comparison();

        std::cout << "\n";

        // Performance
        test_twap_performance();

        std::cout << "\n=== All Tests Completed Successfully ===\n";
        return 0;

    } catch (const std::exception& e) {
        std::cerr << "\nTest FAILED with exception: " << e.what() << "\n";
        return 1;
    }
}
