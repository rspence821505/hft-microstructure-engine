#include "execution_algorithm.hpp"
#include "execution_simulator.hpp"
#include "vwap_strategy.hpp"
#include <cassert>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <thread>
#include <vector>

using namespace std::chrono_literals;

/**
 * @brief Tests VWAP strategy basic functionality
 */
void test_vwap_basic() {
    std::cout << "Testing VWAP basic functionality... ";

    // Create VWAP: 10000 shares over 10 minutes (10 slices) with U-shaped profile
    VWAPStrategy vwap(10000, 10, 10, VWAPStrategy::VolumeProfile::U_SHAPED, true);

    assert(vwap.get_target_quantity() == 10000);
    assert(vwap.get_num_slices() == 10);
    assert(vwap.get_profile_type() == VWAPStrategy::VolumeProfile::U_SHAPED);

    // Check that weights sum to approximately 1.0
    const auto& weights = vwap.get_volume_weights();
    double sum = std::accumulate(weights.begin(), weights.end(), 0.0);
    assert(std::abs(sum - 1.0) < 0.001);

    // Check that slice sizes sum to target quantity
    const auto& sizes = vwap.get_slice_sizes();
    uint64_t total = std::accumulate(sizes.begin(), sizes.end(), 0ULL);
    assert(total == 10000);

    vwap.print_config();

    std::cout << "PASSED\n";
}

/**
 * @brief Tests VWAP uniform profile (should behave like TWAP)
 */
void test_vwap_uniform() {
    std::cout << "Testing VWAP uniform profile... ";

    VWAPStrategy vwap(10000, 10, 10, VWAPStrategy::VolumeProfile::UNIFORM, true);

    // Uniform should have equal weights
    const auto& weights = vwap.get_volume_weights();
    for (const auto& w : weights) {
        assert(std::abs(w - 0.1) < 0.01);  // Each should be ~0.1 (1/10)
    }

    // Slice sizes should all be equal
    const auto& sizes = vwap.get_slice_sizes();
    for (const auto& s : sizes) {
        assert(s == 1000);  // 10000 / 10
    }

    std::cout << "PASSED\n";
}

/**
 * @brief Tests VWAP U-shaped profile
 */
void test_vwap_u_shaped() {
    std::cout << "Testing VWAP U-shaped profile... ";

    VWAPStrategy vwap(10000, 10, 10, VWAPStrategy::VolumeProfile::U_SHAPED, true);

    const auto& weights = vwap.get_volume_weights();

    // First and last slices should have higher weights than middle
    double first_weight = weights.front();
    double middle_weight = weights[weights.size() / 2];
    double last_weight = weights.back();

    assert(first_weight > middle_weight);
    assert(last_weight > middle_weight);

    // First and last should be similar
    assert(std::abs(first_weight - last_weight) < 0.1);

    std::cout << "PASSED\n";
}

/**
 * @brief Tests VWAP morning profile
 */
void test_vwap_morning() {
    std::cout << "Testing VWAP morning profile... ";

    VWAPStrategy vwap(10000, 10, 10, VWAPStrategy::VolumeProfile::MORNING, true);

    const auto& weights = vwap.get_volume_weights();

    // Weights should be decreasing
    for (size_t i = 1; i < weights.size(); ++i) {
        assert(weights[i] <= weights[i-1] + 0.001);  // Allow small tolerance
    }

    // First should be significantly larger than last
    assert(weights.front() > 2.0 * weights.back());

    std::cout << "PASSED\n";
}

/**
 * @brief Tests VWAP afternoon profile
 */
void test_vwap_afternoon() {
    std::cout << "Testing VWAP afternoon profile... ";

    VWAPStrategy vwap(10000, 10, 10, VWAPStrategy::VolumeProfile::AFTERNOON, true);

    const auto& weights = vwap.get_volume_weights();

    // Weights should be increasing
    for (size_t i = 1; i < weights.size(); ++i) {
        assert(weights[i] >= weights[i-1] - 0.001);  // Allow small tolerance
    }

    // Last should be significantly larger than first
    assert(weights.back() > 2.0 * weights.front());

    std::cout << "PASSED\n";
}

/**
 * @brief Tests VWAP custom volume weights
 */
void test_vwap_custom_weights() {
    std::cout << "Testing VWAP custom weights... ";

    VWAPStrategy vwap(10000, 10, 5, VWAPStrategy::VolumeProfile::UNIFORM, true);

    // Set custom weights
    std::vector<double> custom_weights = {0.1, 0.2, 0.4, 0.2, 0.1};
    vwap.set_custom_volume_weights(custom_weights);

    assert(vwap.get_profile_type() == VWAPStrategy::VolumeProfile::CUSTOM);

    const auto& weights = vwap.get_volume_weights();
    assert(weights.size() == 5);

    // Weights should be normalized
    double sum = std::accumulate(weights.begin(), weights.end(), 0.0);
    assert(std::abs(sum - 1.0) < 0.001);

    // Middle slice should have highest weight
    assert(weights[2] > weights[0]);
    assert(weights[2] > weights[4]);

    std::cout << "PASSED\n";
}

/**
 * @brief Tests VWAP slice generation
 */
void test_vwap_slicing() {
    std::cout << "Testing VWAP slicing... ";

    // Create VWAP with millisecond intervals for testing
    VWAPStrategy vwap(10000,
                      std::chrono::milliseconds(1000),
                      10,
                      VWAPStrategy::VolumeProfile::UNIFORM,
                      true);

    auto data = MarketData::from_price(100.0);

    // First slice should execute immediately
    auto orders1 = vwap.on_market_data(data);
    assert(orders1.size() == 1);
    assert(orders1[0].quantity == 1000);  // Uniform so equal slices

    // Simulate fill
    Fill fill1(1, 1, 100.0, 1000);
    vwap.on_fill(fill1);
    assert(vwap.get_current_slice() == 1);

    // Next call should not generate order (not enough time passed)
    auto orders2 = vwap.on_market_data(data);
    assert(orders2.empty());

    // Advance time and try again
    std::this_thread::sleep_for(std::chrono::milliseconds(110));
    data.timestamp = Clock::now();
    auto orders3 = vwap.on_market_data(data);
    assert(orders3.size() == 1);
    assert(vwap.get_current_slice() == 2);

    std::cout << "PASSED\n";
}

/**
 * @brief Tests VWAP with limit orders
 */
void test_vwap_limit_orders() {
    std::cout << "Testing VWAP with limit orders... ";

    VWAPStrategy vwap(10000, 10, 10, VWAPStrategy::VolumeProfile::U_SHAPED, true);
    vwap.set_use_limit_orders(true, 5.0);  // 5 bps offset

    auto data = MarketData::from_quotes(100.0, 100.10);

    auto orders = vwap.on_market_data(data);
    assert(orders.size() == 1);
    assert(!orders[0].is_market_order());

    // Price should be ask + offset
    double expected_price = 100.10 + (100.05 * 5.0 / 10000.0);
    assert(std::abs(orders[0].price - expected_price) < 0.01);

    std::cout << "PASSED\n";
}

/**
 * @brief Tests VWAP execution report
 */
void test_vwap_report() {
    std::cout << "Testing VWAP execution report... ";

    VWAPStrategy vwap(1000,
                      std::chrono::milliseconds(500),
                      5,
                      VWAPStrategy::VolumeProfile::U_SHAPED,
                      true);
    auto data = MarketData::from_price(100.0);

    // Execute all slices
    for (int i = 0; i < 5; ++i) {
        // Advance time
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        data.timestamp = Clock::now();

        auto orders = vwap.on_market_data(data);
        if (!orders.empty()) {
            // Simulate fill with slightly increasing prices
            Fill fill(i+1, i+1, 100.0 + i * 0.02, orders[0].quantity);
            fill.timestamp = Clock::now();
            vwap.on_fill(fill);
        }
    }

    auto report = vwap.generate_report();
    assert(report.algorithm_name == "VWAP");
    assert(report.total_quantity == 1000);
    assert(report.num_fills == 5);
    assert(report.fill_rate > 0.99);

    report.print();

    std::cout << "PASSED\n";
}

/**
 * @brief Tests VWAP with real-time volume adaptation
 */
void test_vwap_real_time_volume() {
    std::cout << "Testing VWAP real-time volume... ";

    VWAPStrategy vwap(10000, 10, 10, VWAPStrategy::VolumeProfile::UNIFORM, true);
    vwap.set_real_time_volume(true, 0.1);  // Target 10% participation

    auto data = MarketData::from_price(100.0);
    data.total_volume = 10000;

    // First slice
    auto orders1 = vwap.on_market_data(data);
    assert(!orders1.empty());

    Fill fill1(1, 1, 100.0, orders1[0].quantity);
    vwap.on_fill(fill1);

    // Advance time with increased market volume
    std::this_thread::sleep_for(std::chrono::milliseconds(110));
    data.timestamp = Clock::now();
    data.total_volume = 20000;  // 10K more volume

    // Next slice should adapt to volume (target 10% of 10K interval = 1000)
    auto orders2 = vwap.on_market_data(data);
    assert(!orders2.empty());

    std::cout << "PASSED\n";
}

/**
 * @brief Tests VWAP simulation
 */
void test_vwap_simulation() {
    std::cout << "Testing VWAP simulation... ";

    SimulationConfig config;
    config.initial_price = 100.0;
    config.spread_bps = 10.0;
    config.volatility = 0.02;
    config.fill_probability = 1.0;
    config.ticks_per_second = 10;

    ExecutionSimulator sim(config);

    // VWAP: 1000 shares over 1 second with 5 slices, U-shaped
    VWAPStrategy vwap(1000,
                      std::chrono::milliseconds(1000),
                      5,
                      VWAPStrategy::VolumeProfile::U_SHAPED,
                      true);

    auto result = sim.run_simulation(vwap, std::chrono::milliseconds(2000));

    result.print();

    assert(result.completed);
    assert(result.report.total_quantity == 1000);
    assert(result.report.num_fills == 5);
    assert(result.report.avg_execution_price > 0);

    std::cout << "PASSED\n";
}

/**
 * @brief Tests VWAP vs TWAP comparison
 */
void test_vwap_vs_twap() {
    std::cout << "Testing VWAP vs TWAP comparison... ";

    SimulationConfig config;
    config.initial_price = 100.0;
    config.spread_bps = 10.0;
    config.adv = 10000000;  // 10M ADV
    config.apply_market_impact = true;
    config.ticks_per_second = 100;

    MarketImpactModel impact_model(0.01, 0.02, config.adv);
    ExecutionSimulator sim(impact_model, config);

    uint64_t target_qty = 100000;  // 1% of ADV

    // Run VWAP with U-shaped profile
    VWAPStrategy vwap(target_qty,
                      std::chrono::milliseconds(5000),
                      50,
                      VWAPStrategy::VolumeProfile::U_SHAPED,
                      true);

    auto vwap_result = sim.run_simulation(vwap, std::chrono::milliseconds(10000));

    std::cout << "\n";
    std::cout << "  VWAP implementation shortfall: "
              << vwap_result.report.implementation_shortfall_bps << " bps\n";

    // VWAP should complete successfully
    assert(vwap_result.completed);

    std::cout << "PASSED\n";
}

/**
 * @brief Tests VWAP reset functionality
 */
void test_vwap_reset() {
    std::cout << "Testing VWAP reset... ";

    VWAPStrategy vwap(10000, 10, 10, VWAPStrategy::VolumeProfile::U_SHAPED, true);

    // Execute first slice
    auto data = MarketData::from_price(100.0);
    auto orders = vwap.on_market_data(data);
    Fill fill(1, 1, 100.0, orders[0].quantity);
    vwap.on_fill(fill);

    assert(vwap.get_executed_quantity() > 0);
    assert(vwap.get_current_slice() == 1);

    // Reset
    vwap.reset();

    assert(vwap.get_executed_quantity() == 0);
    assert(vwap.get_current_slice() == 0);
    assert(!vwap.is_complete());

    // Should be able to execute again
    orders = vwap.on_market_data(data);
    assert(!orders.empty());

    std::cout << "PASSED\n";
}

/**
 * @brief Tests profile comparisons
 */
void test_vwap_profile_comparison() {
    std::cout << "Testing VWAP profile comparison... ";

    SimulationConfig config;
    config.initial_price = 100.0;
    config.spread_bps = 10.0;
    config.ticks_per_second = 50;

    ExecutionSimulator sim(config);

    uint64_t target_qty = 10000;
    auto duration = std::chrono::milliseconds(2000);
    size_t num_slices = 20;

    std::vector<std::pair<std::string, VWAPStrategy::VolumeProfile>> profiles = {
        {"Uniform", VWAPStrategy::VolumeProfile::UNIFORM},
        {"U-Shaped", VWAPStrategy::VolumeProfile::U_SHAPED},
        {"Morning", VWAPStrategy::VolumeProfile::MORNING},
        {"Afternoon", VWAPStrategy::VolumeProfile::AFTERNOON}
    };

    std::cout << "\n";
    std::cout << "  Profile Comparison:\n";
    std::cout << "  " << std::string(60, '-') << "\n";

    for (const auto& [name, profile] : profiles) {
        VWAPStrategy vwap(target_qty, duration, num_slices, profile, true);
        auto result = sim.run_simulation(vwap, std::chrono::milliseconds(5000));

        std::cout << "  " << std::left << std::setw(15) << name
                  << " IS: " << std::setw(8) << std::fixed << std::setprecision(2)
                  << result.report.implementation_shortfall_bps << " bps"
                  << " Fills: " << result.report.num_fills
                  << "\n";

        assert(result.completed);

        // Reset simulator for fair comparison
        sim.reset();
    }

    std::cout << "PASSED\n";
}

/**
 * @brief Main test runner
 */
int main() {
    std::cout << "\n=== VWAP Strategy Test Suite ===\n\n";

    try {
        // Basic tests
        test_vwap_basic();
        test_vwap_uniform();
        test_vwap_u_shaped();
        test_vwap_morning();
        test_vwap_afternoon();
        test_vwap_custom_weights();
        test_vwap_slicing();
        test_vwap_limit_orders();
        test_vwap_report();
        test_vwap_reset();

        std::cout << "\n";

        // Advanced tests
        test_vwap_real_time_volume();
        test_vwap_simulation();
        test_vwap_vs_twap();

        std::cout << "\n";

        // Comparison tests
        test_vwap_profile_comparison();

        std::cout << "\n=== All Tests Completed Successfully ===\n";
        return 0;

    } catch (const std::exception& e) {
        std::cerr << "\nTest FAILED with exception: " << e.what() << "\n";
        return 1;
    }
}
