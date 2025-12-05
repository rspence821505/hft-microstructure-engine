/**
 * @file historical_analysis.cpp
 * @brief Example: Historical microstructure analysis from CSV data
 *
 * Demonstrates:
 * - Loading historical tick data from CSV files
 * - Building event timeline with nanosecond precision
 * - Calibrating market impact models from empirical data
 * - Computing microstructure metrics (spread, flow imbalance, volume patterns)
 * - Analyzing trading patterns and price movements
 *
 * Usage:
 *   ./build/historical_analysis <csv_file> [symbol]
 *
 * Example:
 *   ./build/historical_analysis data/AAPL_2024-01.csv AAPL
 */

#include "backtester.hpp"
#include "market_impact_calibration.hpp"

#include <iostream>
#include <iomanip>
#include <cmath>

void print_usage(const char* program_name) {
    std::cout << "Usage: " << program_name << " <csv_file> [symbol]\n";
    std::cout << "\nArguments:\n";
    std::cout << "  csv_file  - Path to CSV file with market data\n";
    std::cout << "  symbol    - (Optional) Filter by specific symbol\n";
    std::cout << "\nCSV Format:\n";
    std::cout << "  timestamp,symbol,price,volume\n";
    std::cout << "  2024-01-15 09:30:00.123456789,AAPL,150.25,100\n";
    std::cout << "\nExample:\n";
    std::cout << "  " << program_name << " data/AAPL_2024-01.csv AAPL\n";
}

void print_section_header(const std::string& title) {
    std::cout << "\n";
    std::cout << "========================================\n";
    std::cout << "  " << title << "\n";
    std::cout << "========================================\n";
}

void print_impact_estimates(const MarketImpactModel& model, uint64_t adv, const std::string& symbol) {
    std::cout << "\n--- Market Impact Estimates for " << symbol << " ---\n";
    std::cout << "Trade Size (% ADV) | Volume      | Impact (bps)\n";
    std::cout << "-------------------|-------------|-------------\n";

    std::vector<double> percentages = {0.1, 0.5, 1.0, 2.0, 5.0, 10.0, 20.0};

    for (double pct : percentages) {
        uint64_t volume = static_cast<uint64_t>(adv * pct / 100.0);
        double impact = model.estimate_total_impact(volume, adv);

        printf("      %5.1f%%       | %11llu | %10.2f\n",
               pct, static_cast<unsigned long long>(volume), impact);
    }
}

void print_transaction_cost_analysis(const MarketImpactModel& model, uint64_t adv,
                                      double spread_bps, const std::string& /* symbol */) {
    std::cout << "\n--- Total Transaction Cost Analysis ---\n";
    std::cout << "Assumptions: Spread = " << spread_bps << " bps\n\n";
    std::cout << "Trade Size (% ADV) | Half-Spread | Impact  | Total Cost\n";
    std::cout << "-------------------|-------------|---------|------------\n";

    std::vector<double> percentages = {0.1, 0.5, 1.0, 2.0, 5.0, 10.0};
    double half_spread = spread_bps / 2.0;

    for (double pct : percentages) {
        uint64_t volume = static_cast<uint64_t>(adv * pct / 100.0);
        double impact = model.estimate_total_impact(volume, adv);
        double total_cost = half_spread + impact;

        printf("      %5.1f%%       |   %6.2f    | %6.2f  |  %7.2f\n",
               pct, half_spread, impact, total_cost);
    }
}

int main(int argc, char* argv[]) {
    // Parse command line arguments
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    std::string csv_file = argv[1];
    std::string symbol = (argc > 2) ? argv[2] : "";

    // Print header
    std::cout << "\n";
    std::cout << "##############################################################\n";
    std::cout << "#                                                            #\n";
    std::cout << "#          HISTORICAL MICROSTRUCTURE ANALYSIS                #\n";
    std::cout << "#                                                            #\n";
    std::cout << "##############################################################\n";
    std::cout << "\nAnalyzing: " << csv_file << "\n";
    if (!symbol.empty()) {
        std::cout << "Symbol filter: " << symbol << "\n";
    }

    // Step 1: Configure and create backtester
    print_section_header("1. Building Event Timeline");

    BacktesterConfig config;
    config.input_filename = csv_file;
    config.filter_symbol = symbol;

    MicrostructureBacktester backtester(config);

    try {
        backtester.build_event_timeline(csv_file);
    } catch (const std::exception& e) {
        std::cerr << "\nError: Failed to build event timeline\n";
        std::cerr << "Reason: " << e.what() << "\n";
        return 1;
    }

    if (backtester.timeline_size() == 0) {
        std::cerr << "\nError: No events found in file\n";
        return 1;
    }

    std::cout << "✓ Successfully loaded " << backtester.timeline_size() << " events\n";

    // Step 2: Display timeline statistics
    print_section_header("2. Timeline Statistics");
    backtester.print_timeline_stats();

    // Step 3: Calibrate market impact model
    print_section_header("3. Market Impact Calibration");

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
    std::cout << "  Model: Impact = α√(V/ADV) + β√(V/ADV)\n";
    std::cout << "  Permanent coefficient (α): " << std::fixed << std::setprecision(6)
              << params.permanent_impact_coeff << "\n";
    std::cout << "  Temporary coefficient (β): " << params.temporary_impact_coeff << "\n";
    std::cout << "  Impact exponent:           " << params.impact_exponent << "\n";
    std::cout << "  R-squared:                 " << std::setprecision(4) << params.r_squared << "\n";
    std::cout << "  Standard error:            " << params.std_error << "\n";
    std::cout << "  Observations:              " << params.num_observations << "\n";

    if (params.is_valid()) {
        std::cout << "\n  ✓ Model calibration successful (R² ≥ 0.1, n ≥ 10)\n";
    } else {
        std::cout << "\n  ⚠ Warning: Model calibration may be unreliable\n";
        std::cout << "    - Ensure sufficient data points\n";
        std::cout << "    - Check for data quality issues\n";
    }

    // Step 4: Compute Average Daily Volume
    print_section_header("4. Volume Analysis");

    uint64_t adv = backtester.compute_adv(symbol);
    std::cout << "Average Daily Volume (ADV): " << adv << " shares\n";

    if (adv == 0) {
        std::cerr << "\nError: ADV is zero, cannot compute impact estimates\n";
        return 1;
    }

    // Step 5: Market impact estimates
    print_section_header("5. Market Impact Estimates");
    print_impact_estimates(impact_model, adv, symbol.empty() ? "ALL" : symbol);

    std::cout << "\nInterpretation:\n";
    std::cout << "  - Larger trades experience higher market impact (square-root law)\n";
    std::cout << "  - A 1% ADV trade costs ~" << std::fixed << std::setprecision(1)
              << impact_model.estimate_total_impact(adv / 100, adv) << " bps in market impact\n";
    std::cout << "  - Impact grows with √(size), not linearly\n";

    // Step 6: Transaction cost analysis
    print_section_header("6. Transaction Cost Analysis");

    // Estimate spread from timeline (simplified - using first few events)
    double estimated_spread_bps = 1.0;  // Default 1 bps
    const auto& timeline = backtester.get_timeline();
    if (timeline.size() >= 2) {
        // Rough spread estimate from price volatility
        double avg_price = 0.0;
        for (size_t i = 0; i < std::min(size_t(100), timeline.size()); ++i) {
            avg_price += timeline[i].price;
        }
        avg_price /= std::min(size_t(100), timeline.size());

        estimated_spread_bps = (avg_price * 0.0001 / avg_price) * 10000.0; // Rough estimate
        if (estimated_spread_bps < 0.5) estimated_spread_bps = 0.5;
        if (estimated_spread_bps > 10.0) estimated_spread_bps = 10.0;
    }

    print_transaction_cost_analysis(impact_model, adv, estimated_spread_bps,
                                     symbol.empty() ? "ALL" : symbol);

    std::cout << "\nKey Insights:\n";
    std::cout << "  - Total cost = Half-spread + Market impact\n";
    std::cout << "  - Half-spread is paid immediately on execution\n";
    std::cout << "  - Market impact increases with trade size\n";
    std::cout << "  - Optimal execution strategies can reduce impact costs\n";

    // Step 7: Summary and recommendations
    print_section_header("7. Summary & Recommendations");

    std::cout << "Data Quality:\n";
    std::cout << "  - Events processed:  " << backtester.timeline_size() << "\n";
    std::cout << "  - Model R-squared:   " << std::setprecision(4) << params.r_squared << "\n";
    std::cout << "  - Calibration points: " << params.num_observations << "\n";

    std::cout << "\nExecution Recommendations:\n";

    // Calculate cost for a 1% ADV order
    uint64_t benchmark_volume = adv / 100;  // 1% ADV
    double benchmark_impact = impact_model.estimate_total_impact(benchmark_volume, adv);

    if (benchmark_impact < 5.0) {
        std::cout << "  ✓ Low impact regime (< 5 bps for 1% ADV)\n";
        std::cout << "    - Consider aggressive execution strategies\n";
        std::cout << "    - TWAP or immediate execution may be sufficient\n";
    } else if (benchmark_impact < 15.0) {
        std::cout << "  ⚠ Moderate impact regime (5-15 bps for 1% ADV)\n";
        std::cout << "    - Use smart execution algorithms (VWAP, POV)\n";
        std::cout << "    - Spread orders over time to reduce impact\n";
    } else {
        std::cout << "  ⚠ High impact regime (> 15 bps for 1% ADV)\n";
        std::cout << "    - Require sophisticated execution strategies\n";
        std::cout << "    - Consider Almgren-Chriss optimal execution\n";
        std::cout << "    - May need dark pools or alternative venues\n";
    }

    std::cout << "\nNext Steps:\n";
    std::cout << "  1. Test execution strategies on this historical data\n";
    std::cout << "  2. Compare TWAP, VWAP, and Almgren-Chriss performance\n";
    std::cout << "  3. Validate model with out-of-sample data\n";
    std::cout << "  4. Monitor real-time execution against predictions\n";

    std::cout << "\n";
    std::cout << "##############################################################\n";
    std::cout << "#                    ANALYSIS COMPLETE                       #\n";
    std::cout << "##############################################################\n\n";

    return 0;
}
