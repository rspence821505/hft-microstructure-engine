/**
 * @file execution_testing.cpp
 * @brief Example: Execution strategy testing and comparison
 *
 * Demonstrates:
 * - Testing TWAP, VWAP, and Almgren-Chriss execution strategies
 * - Simulating strategy execution on historical data
 * - Measuring implementation shortfall and execution costs
 * - Comparing strategy performance under different market conditions
 * - Analyzing trade-off between speed and impact
 *
 * Usage:
 *   ./build/execution_testing <csv_file> <symbol> <quantity>
 *
 * Example:
 *   ./build/execution_testing data/AAPL_2024-01.csv AAPL 100000
 */

#include "backtester.hpp"
#include "market_impact_calibration.hpp"
#include "twap_strategy.hpp"
#include "vwap_strategy.hpp"
#include "almgren_chriss_strategy.hpp"

#include <iostream>
#include <iomanip>
#include <vector>
#include <memory>
#include <cmath>

void print_usage(const char* program_name) {
    std::cout << "Usage: " << program_name << " <csv_file> <symbol> <quantity>\n";
    std::cout << "\nArguments:\n";
    std::cout << "  csv_file  - Path to CSV file with historical market data\n";
    std::cout << "  symbol    - Trading symbol (e.g., AAPL, GOOGL)\n";
    std::cout << "  quantity  - Target order quantity in shares\n";
    std::cout << "\nCSV Format:\n";
    std::cout << "  timestamp,symbol,price,volume\n";
    std::cout << "  2024-01-15 09:30:00.123456789,AAPL,150.25,100\n";
    std::cout << "\nExample:\n";
    std::cout << "  " << program_name << " data/AAPL_2024-01.csv AAPL 100000\n";
}

void print_section_header(const std::string& title) {
    std::cout << "\n";
    std::cout << "========================================\n";
    std::cout << "  " << title << "\n";
    std::cout << "========================================\n";
}

/**
 * @struct ExecutionResult
 * @brief Results from simulating an execution strategy
 */
struct ExecutionResult {
    std::string strategy_name;
    uint64_t target_quantity;
    uint64_t executed_quantity;
    double arrival_price;
    double avg_execution_price;
    double implementation_shortfall_bps;
    uint32_t num_child_orders;
    uint64_t execution_time_ms;
    double total_cost_bps;
};

/**
 * @brief Simulate TWAP execution on historical data
 */
ExecutionResult simulate_twap_execution(
    const MicrostructureBacktester& backtester,
    const std::string& /* symbol */,
    uint64_t target_quantity,
    uint32_t duration_minutes
) {
    ExecutionResult result;
    result.strategy_name = "TWAP";
    result.target_quantity = target_quantity;

    const auto& timeline = backtester.get_timeline();
    if (timeline.empty()) {
        result.executed_quantity = 0;
        return result;
    }

    // Get arrival price (first price in timeline)
    result.arrival_price = timeline[0].price;

    // TWAP: Split order into equal slices over time
    uint32_t num_slices = duration_minutes;
    uint64_t slice_size = target_quantity / num_slices;

    // Simulate execution at regular intervals
    double total_value = 0.0;
    uint64_t total_executed = 0;
    uint32_t orders_placed = 0;

    size_t events_per_slice = timeline.size() / num_slices;

    for (uint32_t i = 0; i < num_slices; ++i) {
        size_t event_idx = i * events_per_slice;
        if (event_idx >= timeline.size()) break;

        // Execute slice at current market price
        double execution_price = timeline[event_idx].price;
        uint64_t executed = (i == num_slices - 1) ?
            (target_quantity - total_executed) : slice_size;

        total_value += execution_price * executed;
        total_executed += executed;
        orders_placed++;
    }

    result.executed_quantity = total_executed;
    result.avg_execution_price = total_value / total_executed;
    result.num_child_orders = orders_placed;

    // Implementation shortfall: (avg_price - arrival_price) / arrival_price * 10000 bps
    result.implementation_shortfall_bps =
        ((result.avg_execution_price - result.arrival_price) / result.arrival_price) * 10000.0;

    result.execution_time_ms = duration_minutes * 60 * 1000;
    result.total_cost_bps = result.implementation_shortfall_bps;

    return result;
}

/**
 * @brief Simulate VWAP execution on historical data
 */
ExecutionResult simulate_vwap_execution(
    const MicrostructureBacktester& backtester,
    const std::string& /* symbol */,
    uint64_t target_quantity
) {
    ExecutionResult result;
    result.strategy_name = "VWAP";
    result.target_quantity = target_quantity;

    const auto& timeline = backtester.get_timeline();
    if (timeline.empty()) {
        result.executed_quantity = 0;
        return result;
    }

    result.arrival_price = timeline[0].price;

    // VWAP: Weight execution by historical volume patterns
    // Calculate total volume across timeline
    uint64_t total_market_volume = 0;
    for (const auto& event : timeline) {
        total_market_volume += event.volume;
    }

    // Execute proportionally to market volume
    double total_value = 0.0;
    uint64_t total_executed = 0;
    uint32_t orders_placed = 0;

    // Group events into time buckets (e.g., 30 buckets)
    const size_t num_buckets = std::min(size_t(30), timeline.size());
    size_t events_per_bucket = timeline.size() / num_buckets;

    for (size_t bucket = 0; bucket < num_buckets; ++bucket) {
        size_t start_idx = bucket * events_per_bucket;
        size_t end_idx = std::min(start_idx + events_per_bucket, timeline.size());

        // Calculate volume in this bucket
        uint64_t bucket_volume = 0;
        double avg_price = 0.0;
        for (size_t i = start_idx; i < end_idx; ++i) {
            bucket_volume += timeline[i].volume;
            avg_price += timeline[i].price;
        }
        avg_price /= (end_idx - start_idx);

        // Execute proportional to bucket volume
        double volume_ratio = static_cast<double>(bucket_volume) / total_market_volume;
        uint64_t quantity_to_execute = static_cast<uint64_t>(target_quantity * volume_ratio);

        if (quantity_to_execute > 0 && total_executed < target_quantity) {
            // Don't exceed target
            if (total_executed + quantity_to_execute > target_quantity) {
                quantity_to_execute = target_quantity - total_executed;
            }

            total_value += avg_price * quantity_to_execute;
            total_executed += quantity_to_execute;
            orders_placed++;
        }
    }

    // Execute any remaining quantity
    if (total_executed < target_quantity) {
        uint64_t remaining = target_quantity - total_executed;
        double final_price = timeline.back().price;
        total_value += final_price * remaining;
        total_executed += remaining;
        orders_placed++;
    }

    result.executed_quantity = total_executed;
    result.avg_execution_price = total_value / total_executed;
    result.num_child_orders = orders_placed;

    result.implementation_shortfall_bps =
        ((result.avg_execution_price - result.arrival_price) / result.arrival_price) * 10000.0;

    // Estimate execution time (assume full day)
    if (!timeline.empty()) {
        result.execution_time_ms = (timeline.back().timestamp_ns - timeline[0].timestamp_ns) / 1000000;
    }

    result.total_cost_bps = result.implementation_shortfall_bps;

    return result;
}

/**
 * @brief Simulate Almgren-Chriss optimal execution on historical data
 */
ExecutionResult simulate_almgren_chriss_execution(
    const MicrostructureBacktester& backtester,
    const MarketImpactModel& impact_model,
    const std::string& /* symbol */,
    uint64_t target_quantity,
    double risk_aversion,
    uint64_t adv
) {
    ExecutionResult result;
    result.strategy_name = "Almgren-Chriss";
    result.target_quantity = target_quantity;

    const auto& timeline = backtester.get_timeline();
    if (timeline.empty()) {
        result.executed_quantity = 0;
        return result;
    }

    result.arrival_price = timeline[0].price;

    // Almgren-Chriss: Front-load execution to minimize risk
    // Trading trajectory: exponential decay based on risk aversion

    // Calculate optimal schedule (simplified)
    const uint32_t num_periods = 20;  // 20 execution periods
    std::vector<uint64_t> execution_schedule;
    execution_schedule.reserve(num_periods);

    // Higher risk aversion -> more aggressive (front-loaded) execution
    double decay_rate = 1.0 + risk_aversion * 2.0;  // Simplified decay

    double total_weight = 0.0;

    // Calculate weights (exponential decay)
    std::vector<double> weights;
    for (uint32_t i = 0; i < num_periods; ++i) {
        double weight = std::exp(-decay_rate * i / num_periods);
        weights.push_back(weight);
        total_weight += weight;
    }

    // Normalize weights and create schedule
    uint64_t allocated = 0;
    for (uint32_t i = 0; i < num_periods - 1; ++i) {
        uint64_t quantity = static_cast<uint64_t>((weights[i] / total_weight) * target_quantity);
        execution_schedule.push_back(quantity);
        allocated += quantity;
    }
    // Last period gets remainder
    execution_schedule.push_back(target_quantity - allocated);

    // Simulate execution following the optimal schedule
    double total_value = 0.0;
    uint64_t total_executed = 0;

    size_t events_per_period = timeline.size() / num_periods;

    for (uint32_t i = 0; i < num_periods && i < execution_schedule.size(); ++i) {
        size_t event_idx = i * events_per_period;
        if (event_idx >= timeline.size()) break;

        uint64_t quantity = execution_schedule[i];
        if (quantity == 0) continue;

        double execution_price = timeline[event_idx].price;

        // Apply market impact
        double impact_bps = impact_model.estimate_total_impact(quantity, adv);
        double impact_dollars = execution_price * (impact_bps / 10000.0);
        execution_price += impact_dollars;  // Price moves against us

        total_value += execution_price * quantity;
        total_executed += quantity;
    }

    result.executed_quantity = total_executed;
    result.avg_execution_price = total_value / total_executed;
    result.num_child_orders = num_periods;

    result.implementation_shortfall_bps =
        ((result.avg_execution_price - result.arrival_price) / result.arrival_price) * 10000.0;

    // Estimate execution time
    if (!timeline.empty()) {
        result.execution_time_ms = (timeline.back().timestamp_ns - timeline[0].timestamp_ns) / 1000000;
    }

    result.total_cost_bps = result.implementation_shortfall_bps;

    return result;
}

/**
 * @brief Print execution results comparison table
 */
void print_execution_comparison(const std::vector<ExecutionResult>& results) {
    std::cout << "\n=== Execution Strategy Comparison ===\n\n";

    // Print header
    printf("%-18s %12s %12s %12s %10s %15s\n",
           "Strategy", "Avg Price", "Impl. Short", "Total Cost", "Orders", "Exec Time");
    printf("%-18s %12s %12s %12s %10s %15s\n",
           "", "($)", "(bps)", "(bps)", "(count)", "(ms)");
    std::cout << "-------------------------------------------------------------------------------------\n";

    // Print results
    for (const auto& result : results) {
        printf("%-18s %12.4f %12.2f %12.2f %10u %15llu\n",
               result.strategy_name.c_str(),
               result.avg_execution_price,
               result.implementation_shortfall_bps,
               result.total_cost_bps,
               result.num_child_orders,
               static_cast<unsigned long long>(result.execution_time_ms));
    }

    std::cout << "\n";
}

/**
 * @brief Calculate and display cost savings
 */
void print_cost_analysis(const std::vector<ExecutionResult>& results,
                         const MarketImpactModel& impact_model,
                         uint64_t target_quantity,
                         uint64_t adv) {
    std::cout << "\n=== Cost Analysis ===\n";

    // Calculate naive execution cost (single market order)
    double naive_impact = impact_model.estimate_total_impact(target_quantity, adv);
    std::cout << "\nNaive Execution (single market order):\n";
    std::cout << "  Estimated impact: " << std::fixed << std::setprecision(2)
              << naive_impact << " bps\n";

    std::cout << "\nCost Savings vs. Naive Execution:\n";
    for (const auto& result : results) {
        double savings = naive_impact - result.implementation_shortfall_bps;
        double savings_pct = (savings / naive_impact) * 100.0;

        std::cout << "  " << std::setw(18) << std::left << result.strategy_name << ": "
                  << std::setw(8) << std::right << std::fixed << std::setprecision(2)
                  << savings << " bps saved ("
                  << std::setprecision(1) << savings_pct << "%)\n";
    }

    // Find best strategy
    auto best = std::min_element(results.begin(), results.end(),
        [](const ExecutionResult& a, const ExecutionResult& b) {
            return a.implementation_shortfall_bps < b.implementation_shortfall_bps;
        });

    if (best != results.end()) {
        std::cout << "\n✓ Best Strategy: " << best->strategy_name
                  << " with " << std::fixed << std::setprecision(2)
                  << best->implementation_shortfall_bps << " bps cost\n";
    }
}

int main(int argc, char* argv[]) {
    // Parse command line arguments
    if (argc < 4) {
        print_usage(argv[0]);
        return 1;
    }

    std::string csv_file = argv[1];
    std::string symbol = argv[2];
    uint64_t target_quantity = std::stoull(argv[3]);

    // Print header
    std::cout << "\n";
    std::cout << "##############################################################\n";
    std::cout << "#                                                            #\n";
    std::cout << "#          EXECUTION STRATEGY TESTING & COMPARISON          #\n";
    std::cout << "#                                                            #\n";
    std::cout << "##############################################################\n";
    std::cout << "\nData file: " << csv_file << "\n";
    std::cout << "Symbol: " << symbol << "\n";
    std::cout << "Target quantity: " << target_quantity << " shares\n";

    // Step 1: Load historical data
    print_section_header("1. Loading Historical Data");

    BacktesterConfig config;
    config.input_filename = csv_file;
    config.filter_symbol = symbol;

    MicrostructureBacktester backtester(config);

    try {
        backtester.build_event_timeline(csv_file);
    } catch (const std::exception& e) {
        std::cerr << "\nError: Failed to load historical data\n";
        std::cerr << "Reason: " << e.what() << "\n";
        return 1;
    }

    if (backtester.timeline_size() == 0) {
        std::cerr << "\nError: No events found for symbol " << symbol << "\n";
        return 1;
    }

    std::cout << "✓ Loaded " << backtester.timeline_size() << " events\n";

    // Step 2: Calibrate market impact model
    print_section_header("2. Calibrating Market Impact Model");

    MarketImpactModel impact_model;

    try {
        impact_model = backtester.calibrate_impact_model(symbol);
    } catch (const std::exception& e) {
        std::cerr << "\nWarning: Impact model calibration failed\n";
        std::cerr << "Reason: " << e.what() << "\n";
        std::cerr << "Continuing with default parameters...\n";
    }

    auto params = impact_model.get_parameters();
    std::cout << "\nCalibrated Impact Model:\n";
    std::cout << "  Permanent coefficient: " << std::fixed << std::setprecision(6)
              << params.permanent_impact_coeff << "\n";
    std::cout << "  Temporary coefficient: " << params.temporary_impact_coeff << "\n";
    std::cout << "  R-squared: " << std::setprecision(4) << params.r_squared << "\n";

    // Step 3: Compute ADV
    print_section_header("3. Volume Analysis");

    uint64_t adv = backtester.compute_adv(symbol);
    std::cout << "Average Daily Volume: " << adv << " shares\n";
    std::cout << "Order size as % of ADV: " << std::fixed << std::setprecision(2)
              << (static_cast<double>(target_quantity) / adv * 100.0) << "%\n";

    if (adv == 0) {
        std::cerr << "\nError: ADV is zero, cannot simulate execution\n";
        return 1;
    }

    // Step 4: Simulate execution strategies
    print_section_header("4. Simulating Execution Strategies");

    std::vector<ExecutionResult> results;

    std::cout << "\n[1/3] Simulating TWAP (30 minutes)...\n";
    auto twap_result = simulate_twap_execution(backtester, symbol, target_quantity, 30);
    results.push_back(twap_result);
    std::cout << "      Executed " << twap_result.executed_quantity << " shares, "
              << "avg price: $" << std::fixed << std::setprecision(4)
              << twap_result.avg_execution_price << "\n";

    std::cout << "\n[2/3] Simulating VWAP (volume-weighted)...\n";
    auto vwap_result = simulate_vwap_execution(backtester, symbol, target_quantity);
    results.push_back(vwap_result);
    std::cout << "      Executed " << vwap_result.executed_quantity << " shares, "
              << "avg price: $" << std::fixed << std::setprecision(4)
              << vwap_result.avg_execution_price << "\n";

    std::cout << "\n[3/3] Simulating Almgren-Chriss (risk aversion = 0.01)...\n";
    auto ac_result = simulate_almgren_chriss_execution(
        backtester, impact_model, symbol, target_quantity, 0.01, adv);
    results.push_back(ac_result);
    std::cout << "      Executed " << ac_result.executed_quantity << " shares, "
              << "avg price: $" << std::fixed << std::setprecision(4)
              << ac_result.avg_execution_price << "\n";

    // Step 5: Display comparison
    print_section_header("5. Results & Comparison");
    print_execution_comparison(results);

    // Step 6: Cost analysis
    print_cost_analysis(results, impact_model, target_quantity, adv);

    // Step 7: Recommendations
    print_section_header("6. Execution Recommendations");

    double participation_rate = static_cast<double>(target_quantity) / adv;

    std::cout << "\nMarket Conditions:\n";
    std::cout << "  Participation rate: " << std::fixed << std::setprecision(2)
              << (participation_rate * 100.0) << "% of ADV\n";

    if (participation_rate < 0.01) {
        std::cout << "\n✓ Small order relative to ADV (< 1%)\n";
        std::cout << "  Recommendation: Aggressive execution acceptable\n";
        std::cout << "  - TWAP or immediate execution will have minimal impact\n";
        std::cout << "  - Focus on speed rather than cost optimization\n";
    } else if (participation_rate < 0.05) {
        std::cout << "\n⚠ Moderate order size (1-5% of ADV)\n";
        std::cout << "  Recommendation: Use smart execution algorithms\n";
        std::cout << "  - VWAP recommended to blend with market volume\n";
        std::cout << "  - Consider market conditions and urgency\n";
    } else if (participation_rate < 0.20) {
        std::cout << "\n⚠ Large order size (5-20% of ADV)\n";
        std::cout << "  Recommendation: Use optimal execution strategies\n";
        std::cout << "  - Almgren-Chriss recommended for risk-optimal execution\n";
        std::cout << "  - Balance between speed and market impact critical\n";
        std::cout << "  - Consider splitting across multiple days\n";
    } else {
        std::cout << "\n⚠⚠ Very large order (> 20% of ADV)\n";
        std::cout << "  Recommendation: Requires careful execution planning\n";
        std::cout << "  - Multi-day execution strategy required\n";
        std::cout << "  - Consider alternative venues (dark pools)\n";
        std::cout << "  - Real-time adjustment based on market conditions\n";
    }

    std::cout << "\nKey Insights:\n";
    std::cout << "  • TWAP: Simple, predictable, but ignores market volume patterns\n";
    std::cout << "  • VWAP: Trades more when market is active, reduces impact\n";
    std::cout << "  • Almgren-Chriss: Optimal trade-off between timing risk and impact\n";
    std::cout << "  • All strategies significantly outperform naive execution\n";

    std::cout << "\n";
    std::cout << "##############################################################\n";
    std::cout << "#                  TESTING COMPLETE                          #\n";
    std::cout << "##############################################################\n\n";

    return 0;
}
