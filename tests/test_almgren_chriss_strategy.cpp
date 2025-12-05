#include "almgren_chriss_strategy.hpp"
#include "execution_algorithm.hpp"
#include "execution_simulator.hpp"
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
 * @brief Tests Almgren-Chriss strategy basic functionality
 */
void test_almgren_chriss_basic() {
    std::cout << "Testing Almgren-Chriss basic functionality... ";

    // Create Almgren-Chriss: 10000 shares over 10 minutes (10 slices)
    AlmgrenChrissStrategy ac(10000, 10, 10, true);

    assert(ac.get_target_quantity() == 10000);
    assert(ac.get_num_slices() == 10);
    assert(ac.name() == "Almgren-Chriss");

    // Default parameters should be set
    assert(ac.get_risk_aversion() > 0);
    assert(ac.get_permanent_impact() > 0);
    assert(ac.get_temporary_impact() > 0);
    assert(ac.get_volatility() > 0);

    ac.print_config();

    std::cout << "PASSED\n";
}

/**
 * @brief Tests trajectory computation
 */
void test_trajectory_computation() {
    std::cout << "Testing trajectory computation... ";

    AlmgrenChrissStrategy ac(10000, 10, 10, true);
    ac.set_risk_aversion(1e-6);
    ac.set_volatility(0.02);
    ac.set_market_impact(0.1, 0.01, 1000000.0);

    // Compute trajectory explicitly
    ac.compute_trajectory();

    const auto& trajectory = ac.get_trajectory();
    const auto& sizes = ac.get_slice_sizes();

    // Trajectory should have num_slices + 1 points
    assert(trajectory.size() == 11);

    // Trajectory should be decreasing from 1.0 to 0.0
    assert(std::abs(trajectory[0] - 1.0) < 0.01);
    assert(std::abs(trajectory[10] - 0.0) < 0.01);

    for (size_t i = 1; i < trajectory.size(); ++i) {
        assert(trajectory[i] <= trajectory[i-1] + 0.001);  // Should be decreasing
    }

    // Slice sizes should sum to target quantity
    uint64_t total = std::accumulate(sizes.begin(), sizes.end(), 0ULL);
    assert(total == 10000);

    // Almgren-Chriss is typically front-loaded
    // First slice should be >= later slices on average
    assert(sizes[0] >= sizes[sizes.size() - 1]);

    std::cout << "PASSED\n";
}

/**
 * @brief Tests risk aversion parameter effects
 */
void test_risk_aversion() {
    std::cout << "Testing risk aversion effects... ";

    uint64_t target = 10000;
    int duration = 10;
    size_t slices = 10;

    // Low risk aversion (aggressive)
    AlmgrenChrissStrategy ac_aggressive(target, duration, slices, true);
    ac_aggressive.set_risk_aversion(1e-8);
    ac_aggressive.set_market_impact(0.1, 0.01, 1000000.0);
    ac_aggressive.compute_trajectory();

    // High risk aversion (conservative)
    AlmgrenChrissStrategy ac_conservative(target, duration, slices, true);
    ac_conservative.set_risk_aversion(1e-4);
    ac_conservative.set_market_impact(0.1, 0.01, 1000000.0);
    ac_conservative.compute_trajectory();

    const auto& sizes_agg = ac_aggressive.get_slice_sizes();
    const auto& sizes_cons = ac_conservative.get_slice_sizes();

    // Aggressive should be more front-loaded (first slice larger)
    assert(sizes_agg[0] >= sizes_cons[0]);

    std::cout << "PASSED\n";
}

/**
 * @brief Tests market impact parameter effects
 */
void test_market_impact_params() {
    std::cout << "Testing market impact parameters... ";

    uint64_t target = 10000;
    int duration = 10;
    size_t slices = 10;

    // High market impact
    AlmgrenChrissStrategy ac_high_impact(target, duration, slices, true);
    ac_high_impact.set_risk_aversion(1e-6);
    ac_high_impact.set_market_impact(1.0, 0.1, 1000000.0);  // High impact
    ac_high_impact.compute_trajectory();

    // Low market impact
    AlmgrenChrissStrategy ac_low_impact(target, duration, slices, true);
    ac_low_impact.set_risk_aversion(1e-6);
    ac_low_impact.set_market_impact(0.01, 0.001, 1000000.0);  // Low impact
    ac_low_impact.compute_trajectory();

    // High impact should lead to more distributed execution
    const auto& sizes_high = ac_high_impact.get_slice_sizes();
    const auto& sizes_low = ac_low_impact.get_slice_sizes();

    // Both should sum to target
    assert(std::accumulate(sizes_high.begin(), sizes_high.end(), 0ULL) == target);
    assert(std::accumulate(sizes_low.begin(), sizes_low.end(), 0ULL) == target);

    std::cout << "PASSED\n";
}

/**
 * @brief Tests construction with impact model
 */
void test_with_impact_model() {
    std::cout << "Testing construction with impact model... ";

    // Create impact model
    MarketImpactModel impact_model(0.1, 0.05, 5000000.0);

    // Create strategy with impact model
    AlmgrenChrissStrategy ac(10000, 10, 10, impact_model, true);

    // Parameters should match impact model
    assert(std::abs(ac.get_permanent_impact() - 0.1) < 0.001);
    assert(std::abs(ac.get_temporary_impact() - 0.05) < 0.001);

    ac.compute_trajectory();
    const auto& sizes = ac.get_slice_sizes();
    uint64_t total = std::accumulate(sizes.begin(), sizes.end(), 0ULL);
    assert(total == 10000);

    std::cout << "PASSED\n";
}

/**
 * @brief Tests slice generation
 */
void test_slice_generation() {
    std::cout << "Testing slice generation... ";

    // Create Almgren-Chriss with millisecond intervals for testing
    AlmgrenChrissStrategy ac(10000,
                             std::chrono::milliseconds(1000),
                             10,
                             true);
    ac.set_risk_aversion(1e-6);
    ac.set_market_impact(0.1, 0.01, 1000000.0);

    auto data = MarketData::from_price(100.0);

    // First slice should execute immediately
    auto orders1 = ac.on_market_data(data);
    assert(orders1.size() == 1);
    assert(orders1[0].quantity > 0);

    // Simulate fill
    Fill fill1(1, 1, 100.0, orders1[0].quantity);
    ac.on_fill(fill1);
    assert(ac.get_current_slice() == 1);

    // Next call should not generate order (not enough time passed)
    auto orders2 = ac.on_market_data(data);
    assert(orders2.empty());

    // Advance time and try again
    std::this_thread::sleep_for(std::chrono::milliseconds(110));
    data.timestamp = Clock::now();
    auto orders3 = ac.on_market_data(data);
    assert(orders3.size() == 1);
    assert(ac.get_current_slice() == 2);

    std::cout << "PASSED\n";
}

/**
 * @brief Tests Almgren-Chriss with limit orders
 */
void test_limit_orders() {
    std::cout << "Testing Almgren-Chriss with limit orders... ";

    AlmgrenChrissStrategy ac(10000, 10, 10, true);
    ac.set_use_limit_orders(true, 5.0);  // 5 bps offset

    auto data = MarketData::from_quotes(100.0, 100.10);

    auto orders = ac.on_market_data(data);
    assert(orders.size() == 1);
    assert(!orders[0].is_market_order());

    // Price should be ask + offset
    double expected_price = 100.10 + (100.05 * 5.0 / 10000.0);
    assert(std::abs(orders[0].price - expected_price) < 0.01);

    std::cout << "PASSED\n";
}

/**
 * @brief Tests execution report
 */
void test_execution_report() {
    std::cout << "Testing execution report... ";

    AlmgrenChrissStrategy ac(1000,
                             std::chrono::milliseconds(500),
                             5,
                             true);
    ac.set_risk_aversion(1e-6);
    ac.set_market_impact(0.1, 0.01, 1000000.0);

    auto data = MarketData::from_price(100.0);

    // Execute all slices
    for (int i = 0; i < 5; ++i) {
        // Advance time
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        data.timestamp = Clock::now();

        auto orders = ac.on_market_data(data);
        if (!orders.empty()) {
            // Simulate fill with slightly increasing prices
            Fill fill(i+1, i+1, 100.0 + i * 0.02, orders[0].quantity);
            fill.timestamp = Clock::now();
            ac.on_fill(fill);
        }
    }

    auto report = ac.generate_report();
    assert(report.algorithm_name == "Almgren-Chriss");
    assert(report.total_quantity == 1000);
    assert(report.num_fills == 5);
    assert(report.fill_rate > 0.99);

    report.print();

    std::cout << "PASSED\n";
}

/**
 * @brief Tests simulation with Almgren-Chriss
 */
void test_simulation() {
    std::cout << "Testing Almgren-Chriss simulation... ";

    SimulationConfig config;
    config.initial_price = 100.0;
    config.spread_bps = 10.0;
    config.volatility = 0.02;
    config.fill_probability = 1.0;
    config.ticks_per_second = 10;

    ExecutionSimulator sim(config);

    // Almgren-Chriss: 1000 shares over 1 second with 5 slices
    AlmgrenChrissStrategy ac(1000,
                             std::chrono::milliseconds(1000),
                             5,
                             true);
    ac.set_risk_aversion(1e-6);
    ac.set_market_impact(0.1, 0.01, 1000000.0);

    auto result = sim.run_simulation(ac, std::chrono::milliseconds(2000));

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
    AlmgrenChrissStrategy ac(100000,
                             std::chrono::milliseconds(1000),
                             10,
                             impact_model,
                             true);
    ac.set_risk_aversion(1e-6);

    auto result = sim.run_simulation(ac, std::chrono::milliseconds(2000));

    std::cout << "\n";
    std::cout << "  Predicted impact: " << result.predicted_impact_bps << " bps\n";
    std::cout << "  Realized impact: " << result.realized_impact_bps << " bps\n";
    std::cout << "  Implementation shortfall: " << result.report.implementation_shortfall_bps << " bps\n";
    std::cout << "  Estimated cost: " << ac.estimate_expected_cost() << " bps\n";

    // Impact should be positive (price moved up for buy)
    assert(result.predicted_impact_bps > 0);

    std::cout << "PASSED\n";
}

/**
 * @brief Tests reset functionality
 */
void test_reset() {
    std::cout << "Testing reset functionality... ";

    AlmgrenChrissStrategy ac(10000, 10, 10, true);
    ac.set_risk_aversion(1e-6);
    ac.set_market_impact(0.1, 0.01, 1000000.0);

    // Execute first slice
    auto data = MarketData::from_price(100.0);
    auto orders = ac.on_market_data(data);
    Fill fill(1, 1, 100.0, orders[0].quantity);
    ac.on_fill(fill);

    assert(ac.get_executed_quantity() > 0);
    assert(ac.get_current_slice() == 1);

    // Reset
    ac.reset();

    assert(ac.get_executed_quantity() == 0);
    assert(ac.get_current_slice() == 0);
    assert(!ac.is_complete());

    // Should be able to execute again
    orders = ac.on_market_data(data);
    assert(!orders.empty());

    std::cout << "PASSED\n";
}

/**
 * @brief Tests different risk aversion levels in simulation
 */
void test_risk_aversion_comparison() {
    std::cout << "Testing risk aversion comparison... ";

    SimulationConfig config;
    config.initial_price = 100.0;
    config.spread_bps = 10.0;
    config.adv = 10000000;  // 10M ADV
    config.apply_market_impact = true;
    config.ticks_per_second = 100;

    MarketImpactModel impact_model(0.01, 0.02, config.adv);

    uint64_t target_qty = 100000;  // 1% of ADV
    auto duration = std::chrono::milliseconds(5000);
    size_t num_slices = 50;

    std::vector<std::pair<std::string, double>> risk_levels = {
        {"Aggressive", 1e-8},
        {"Moderate", 1e-6},
        {"Conservative", 1e-4}
    };

    std::cout << "\n";
    std::cout << "  Risk Aversion Comparison:\n";
    std::cout << "  " << std::string(70, '-') << "\n";

    for (const auto& [name, lambda] : risk_levels) {
        ExecutionSimulator sim(impact_model, config);

        AlmgrenChrissStrategy ac(target_qty, duration, num_slices, impact_model, true);
        ac.set_risk_aversion(lambda);
        ac.set_volatility(0.02);

        auto result = sim.run_simulation(ac, std::chrono::milliseconds(10000));

        std::cout << "  " << std::left << std::setw(15) << name
                  << " (λ=" << std::scientific << std::setprecision(0) << lambda << ")"
                  << " IS: " << std::fixed << std::setprecision(2)
                  << result.report.implementation_shortfall_bps << " bps"
                  << " Est: " << ac.estimate_expected_cost() << " bps"
                  << "\n";

        assert(result.completed);
    }

    std::cout << "PASSED\n";
}

/**
 * @brief Tests expected cost estimation
 */
void test_expected_cost() {
    std::cout << "Testing expected cost estimation... ";

    AlmgrenChrissStrategy ac(100000, 10, 20, true);
    ac.set_risk_aversion(1e-6);
    ac.set_volatility(0.02);
    ac.set_market_impact(0.1, 0.01, 1000000.0);

    ac.compute_trajectory();

    double cost = ac.estimate_expected_cost();

    // Cost should be positive and reasonable
    assert(cost > 0);
    assert(cost < 1000.0);  // Should be less than 1000 bps (10%)

    std::cout << "Expected cost: " << std::fixed << std::setprecision(2)
              << cost << " bps... ";

    std::cout << "PASSED\n";
}

/**
 * @brief Tests that trajectory is front-loaded
 */
void test_front_loading() {
    std::cout << "Testing front-loading behavior... ";

    AlmgrenChrissStrategy ac(10000, 10, 10, true);
    ac.set_risk_aversion(1e-6);
    ac.set_market_impact(0.1, 0.01, 1000000.0);
    ac.compute_trajectory();

    const auto& sizes = ac.get_slice_sizes();

    // Calculate average of first half vs second half
    double first_half_avg = 0;
    double second_half_avg = 0;

    for (size_t i = 0; i < sizes.size() / 2; ++i) {
        first_half_avg += sizes[i];
    }
    for (size_t i = sizes.size() / 2; i < sizes.size(); ++i) {
        second_half_avg += sizes[i];
    }

    first_half_avg /= (sizes.size() / 2);
    second_half_avg /= (sizes.size() - sizes.size() / 2);

    // First half should have larger average size (front-loaded)
    assert(first_half_avg >= second_half_avg);

    std::cout << "PASSED\n";
}

/**
 * @brief Main test runner
 */
int main() {
    std::cout << "\n=== Almgren-Chriss Strategy Test Suite ===\n\n";

    try {
        // Basic tests
        test_almgren_chriss_basic();
        test_trajectory_computation();
        test_risk_aversion();
        test_market_impact_params();
        test_with_impact_model();
        test_slice_generation();
        test_limit_orders();
        test_execution_report();
        test_reset();
        test_expected_cost();
        test_front_loading();

        std::cout << "\n";

        // Simulation tests
        test_simulation();
        test_simulation_with_impact();

        std::cout << "\n";

        // Comparison tests
        test_risk_aversion_comparison();

        std::cout << "\n=== All Tests Completed Successfully ===\n";
        return 0;

    } catch (const std::exception& e) {
        std::cerr << "\nTest FAILED with exception: " << e.what() << "\n";
        return 1;
    }
}
