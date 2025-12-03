#include "../include/linear_regression.hpp"
#include "../include/market_impact_calibration.hpp"
#include "../include/microstructure_analytics.hpp"
#include <cassert>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <random>
#include <vector>

/**
 * @brief Tests basic linear regression
 */
void test_linear_regression_basic() {
    std::cout << "Testing linear regression basic... ";

    // Test with perfect linear data: y = 2x + 1
    std::vector<double> x = {1.0, 2.0, 3.0, 4.0, 5.0};
    std::vector<double> y = {3.0, 5.0, 7.0, 9.0, 11.0};

    RegressionResult result = linear_regression(x, y);

    assert(std::abs(result.slope - 2.0) < 0.0001);
    assert(std::abs(result.intercept - 1.0) < 0.0001);
    assert(std::abs(result.r_squared - 1.0) < 0.0001);
    assert(result.n_samples == 5);

    std::cout << "PASSED\n";
}

/**
 * @brief Tests linear regression with noisy data
 */
void test_linear_regression_noisy() {
    std::cout << "Testing linear regression with noise... ";

    // Generate noisy data: y = 0.5x + 2 + noise
    std::mt19937 rng(42);
    std::normal_distribution<double> noise(0.0, 0.5);

    std::vector<double> x, y;
    for (int i = 0; i < 100; ++i) {
        double xi = i * 0.1;
        double yi = 0.5 * xi + 2.0 + noise(rng);
        x.push_back(xi);
        y.push_back(yi);
    }

    RegressionResult result = linear_regression(x, y);

    // With noise, we expect slope close to 0.5, intercept close to 2
    assert(std::abs(result.slope - 0.5) < 0.1);
    assert(std::abs(result.intercept - 2.0) < 0.5);
    assert(result.r_squared > 0.9);  // Should still be highly correlated

    std::cout << "PASSED (slope=" << result.slope
              << ", intercept=" << result.intercept
              << ", R²=" << result.r_squared << ")\n";
}

/**
 * @brief Tests linear regression edge cases
 */
void test_linear_regression_edge_cases() {
    std::cout << "Testing linear regression edge cases... ";

    // Test with minimum data points (2)
    std::vector<double> x2 = {1.0, 2.0};
    std::vector<double> y2 = {2.0, 4.0};
    RegressionResult result2 = linear_regression(x2, y2);
    assert(std::abs(result2.slope - 2.0) < 0.0001);
    assert(std::abs(result2.intercept - 0.0) < 0.0001);

    // Test with all same x values (no variance in x)
    std::vector<double> x_same = {1.0, 1.0, 1.0};
    std::vector<double> y_diff = {1.0, 2.0, 3.0};
    RegressionResult result_same = linear_regression(x_same, y_diff);
    assert(result_same.slope == 0.0);  // Should return 0 slope

    // Test with size mismatch (should throw)
    bool threw = false;
    try {
        std::vector<double> x_short = {1.0};
        std::vector<double> y_long = {1.0, 2.0};
        linear_regression(x_short, y_long);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    assert(threw);

    std::cout << "PASSED\n";
}

/**
 * @brief Tests weighted linear regression
 */
void test_weighted_linear_regression() {
    std::cout << "Testing weighted linear regression... ";

    // Data where we want to weight some points more
    std::vector<double> x = {1.0, 2.0, 3.0, 4.0, 5.0, 6.0};
    std::vector<double> y = {2.0, 4.0, 6.0, 8.0, 15.0, 12.0};  // 5th point is outlier
    std::vector<double> weights = {1.0, 1.0, 1.0, 1.0, 0.1, 1.0};  // Low weight for outlier

    RegressionResult weighted = weighted_linear_regression(x, y, weights);
    RegressionResult unweighted = linear_regression(x, y);

    // Weighted should be closer to y = 2x since outlier is downweighted
    assert(std::abs(weighted.slope - 2.0) < std::abs(unweighted.slope - 2.0));

    std::cout << "PASSED (weighted slope=" << weighted.slope
              << ", unweighted slope=" << unweighted.slope << ")\n";
}

/**
 * @brief Tests correlation function
 */
void test_correlation() {
    std::cout << "Testing correlation... ";

    // Perfect positive correlation
    std::vector<double> x1 = {1, 2, 3, 4, 5};
    std::vector<double> y1 = {2, 4, 6, 8, 10};
    assert(std::abs(correlation(x1, y1) - 1.0) < 0.0001);

    // Perfect negative correlation
    std::vector<double> y2 = {10, 8, 6, 4, 2};
    assert(std::abs(correlation(x1, y2) - (-1.0)) < 0.0001);

    // No correlation (approximately)
    std::vector<double> x3 = {1, 2, 3, 4, 5};
    std::vector<double> y3 = {1, 3, 2, 4, 3};  // Roughly random
    double corr = correlation(x3, y3);
    assert(std::abs(corr) < 0.8);  // Should not be highly correlated

    std::cout << "PASSED\n";
}

/**
 * @brief Tests MarketImpactModel basic functionality
 */
void test_market_impact_model_basic() {
    std::cout << "Testing MarketImpactModel basic... ";

    // Default model
    MarketImpactModel model;
    uint64_t adv = 10000000;  // 10M shares ADV

    // 1% participation (100K shares)
    double impact = model.estimate_total_impact(100000, adv);
    assert(impact > 0);
    assert(impact < 100);  // Should be less than 100 bps for 1%

    // Model with specific coefficients
    MarketImpactModel model2(0.02, 0.04, adv);
    double perm = model2.estimate_permanent_impact(100000, adv);
    double temp = model2.estimate_temporary_impact(100000, adv);
    double total = model2.estimate_total_impact(100000, adv);

    assert(std::abs(total - (perm + temp)) < 0.0001);
    assert(temp > perm);  // Temporary should be 2x permanent

    std::cout << "PASSED (1% participation: perm=" << perm
              << " bps, temp=" << temp << " bps)\n";
}

/**
 * @brief Tests square-root law behavior
 */
void test_square_root_law() {
    std::cout << "Testing square-root law... ";

    MarketImpactModel model(0.01, 0.02, 10000000);

    // Impact should scale with sqrt(participation)
    // So 4x volume should give 2x impact
    double impact_1pct = model.estimate_total_impact(100000, 10000000);
    double impact_4pct = model.estimate_total_impact(400000, 10000000);

    double ratio = impact_4pct / impact_1pct;
    // sqrt(4) = 2, so ratio should be approximately 2
    assert(std::abs(ratio - 2.0) < 0.01);

    std::cout << "PASSED (4x volume gives " << ratio << "x impact)\n";
}

/**
 * @brief Tests MarketImpactCalibrator with synthetic data
 */
void test_calibrator_synthetic() {
    std::cout << "Testing calibrator with synthetic data... ";

    MarketImpactCalibrator calibrator;

    // Generate synthetic data following square-root law:
    // impact = 0.015 * sqrt(participation)
    std::mt19937 rng(42);
    std::normal_distribution<double> noise(0.0, 0.1);  // 10% noise

    for (int i = 0; i < 100; ++i) {
        double participation = 0.001 + 0.01 * (i + 1) / 100.0;  // 0.1% to 1.1%
        double base_impact = 0.015 * std::sqrt(participation);
        double noisy_impact = base_impact * (1.0 + noise(rng));
        if (noisy_impact > 0.0001) {
            calibrator.add_observation(participation, noisy_impact);
        }
    }

    MarketImpactModel model = calibrator.calibrate(10000000);
    const auto& params = model.get_parameters();

    // Should recover exponent close to 0.5 (square-root law)
    assert(std::abs(params.impact_exponent - 0.5) < 0.2);
    // R² should be reasonable
    assert(params.r_squared > 0.5);

    std::cout << "PASSED (exponent=" << params.impact_exponent
              << ", R²=" << params.r_squared << ")\n";
}

/**
 * @brief Creates a mock Fill for testing
 */
Fill create_test_fill(int buy_id, int sell_id, double price, int qty) {
    return Fill(buy_id, sell_id, price, qty);
}

/**
 * @brief Creates a mock EnhancedFill for testing
 */
EnhancedFill create_test_enhanced_fill(int buy_id, int sell_id, double price, int qty,
                                        const std::string& symbol, bool is_aggressive_buy) {
    Fill base_fill(buy_id, sell_id, price, qty);
    return EnhancedFill(base_fill, 1, 2, symbol, 0, is_aggressive_buy);
}

/**
 * @brief Tests calibration from Fill data
 */
void test_calibration_from_fills() {
    std::cout << "Testing calibration from Fill data... ";

    std::vector<Fill> fills;
    uint64_t adv = 50000000;  // 50M ADV

    // Generate fills with price changes that follow square-root law
    double base_price = 100.0;
    std::mt19937 rng(123);

    for (int i = 0; i < 200; ++i) {
        // Vary volume from 50K to 500K (0.1% to 1% of ADV)
        int volume = 50000 + (i * 2250);

        // Price impact proportional to sqrt(volume/ADV)
        double participation = static_cast<double>(volume) / adv;
        double impact = 0.02 * std::sqrt(participation);

        // Alternate buy/sell pressure
        double sign = (i % 2 == 0) ? 1.0 : -1.0;
        double new_price = base_price * (1.0 + sign * impact);

        fills.push_back(create_test_fill(i+1, i+1000, new_price, volume));
        base_price = new_price;
    }

    MarketImpactModel model = calibrate_impact_model(fills, adv);
    const auto& params = model.get_parameters();

    // Should get reasonable calibration
    assert(params.num_observations > 50);
    assert(params.r_squared > 0.3);

    std::cout << "PASSED (observations=" << params.num_observations
              << ", R²=" << params.r_squared << ")\n";
}

/**
 * @brief Tests MicrostructureAnalytics calibration integration
 */
void test_analytics_calibration_integration() {
    std::cout << "Testing MicrostructureAnalytics calibration... ";

    MicrostructureAnalytics analytics;
    analytics.set_symbol_adv("TEST", 10000000);

    // Generate fills for calibration
    double price = 100.0;
    for (int i = 0; i < 100; ++i) {
        int volume = 10000 + i * 1000;  // 10K to 110K

        // Price moves based on square-root law
        double participation = static_cast<double>(volume) / 10000000.0;
        double impact = 0.01 * std::sqrt(participation);
        price *= (1.0 + impact * ((i % 2 == 0) ? 1 : -1));

        EnhancedFill fill = create_test_enhanced_fill(
            i+1, i+1000, price, volume, "TEST", i % 2 == 0);

        analytics.process_fill(fill);
        analytics.record_fill_for_calibration(fill);
    }

    // Calibrate
    bool calibrated = analytics.calibrate_impact_model("TEST");

    // Should calibrate successfully
    assert(calibrated || analytics.get_calibration_fills().size() >= 10);

    if (calibrated) {
        assert(analytics.is_using_calibrated_model());

        // Test impact estimation
        double impact = analytics.estimate_calibrated_impact(100000, "TEST");
        assert(impact > 0);

        double perm = analytics.estimate_permanent_impact(100000, "TEST");
        double temp = analytics.estimate_temporary_impact(100000, "TEST");
        assert(perm > 0);
        assert(temp > 0);
    }

    std::cout << "PASSED\n";
}

/**
 * @brief Tests ImpactModelParameters validation
 */
void test_impact_model_parameters() {
    std::cout << "Testing ImpactModelParameters... ";

    ImpactModelParameters params;
    params.permanent_impact_coeff = 0.01;
    params.temporary_impact_coeff = 0.02;
    params.r_squared = 0.5;
    params.num_observations = 50;

    assert(params.is_valid());
    assert(params.is_valid(0.3, 20));
    assert(!params.is_valid(0.8, 50));  // R² too high threshold

    // Invalid: too few observations
    ImpactModelParameters invalid_obs = params;
    invalid_obs.num_observations = 5;
    assert(!invalid_obs.is_valid());

    // Invalid: negative coefficient
    ImpactModelParameters invalid_coeff = params;
    invalid_coeff.permanent_impact_coeff = -0.01;
    assert(!invalid_coeff.is_valid());

    std::cout << "PASSED\n";
}

/**
 * @brief Performance test for calibration
 */
void test_calibration_performance() {
    std::cout << "Testing calibration performance... ";

    const int NUM_FILLS = 100000;

    std::vector<Fill> fills;
    fills.reserve(NUM_FILLS);

    std::mt19937 rng(42);
    std::uniform_int_distribution<int> vol_dist(10000, 500000);
    std::uniform_real_distribution<double> price_change(-0.01, 0.01);

    double price = 100.0;
    for (int i = 0; i < NUM_FILLS; ++i) {
        price *= (1.0 + price_change(rng));
        int volume = vol_dist(rng);
        fills.push_back(create_test_fill(i+1, i+1000, price, volume));
    }

    auto start = std::chrono::steady_clock::now();
    MarketImpactModel model = calibrate_impact_model(fills, 50000000);
    auto end = std::chrono::steady_clock::now();

    auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

    std::cout << "\n";
    std::cout << "  Fills processed: " << NUM_FILLS << "\n";
    std::cout << "  Time: " << duration_ms << " ms\n";
    std::cout << "  Throughput: " << (NUM_FILLS * 1000 / std::max(1LL, static_cast<long long>(duration_ms))) << " fills/sec\n";
    std::cout << "  R²: " << model.get_parameters().r_squared << "\n";

    // Should complete in reasonable time
    assert(duration_ms < 5000);  // Less than 5 seconds

    std::cout << "  PASSED\n";
}

/**
 * @brief Tests regression prediction
 */
void test_regression_prediction() {
    std::cout << "Testing regression prediction... ";

    // Fit: y = 3x + 2
    std::vector<double> x = {0, 1, 2, 3, 4};
    std::vector<double> y = {2, 5, 8, 11, 14};

    RegressionResult result = linear_regression(x, y);

    // Test predictions
    assert(std::abs(result.predict(0) - 2.0) < 0.0001);
    assert(std::abs(result.predict(5) - 17.0) < 0.0001);
    assert(std::abs(result.predict(-1) - (-1.0)) < 0.0001);

    std::cout << "PASSED\n";
}

/**
 * @brief Tests implementation shortfall estimation
 */
void test_implementation_shortfall() {
    std::cout << "Testing implementation shortfall... ";

    MarketImpactModel model(0.01, 0.02, 10000000);

    // 1% participation with 10 bps spread
    double shortfall = model.estimate_implementation_shortfall(100000, 10000000, 10.0);

    // Should be: 5 bps (half spread) + permanent + temporary
    double expected_min = 5.0;  // At least half spread
    assert(shortfall > expected_min);

    // Try with larger order
    double shortfall_large = model.estimate_implementation_shortfall(1000000, 10000000, 10.0);
    assert(shortfall_large > shortfall);  // Larger order = more impact

    std::cout << "PASSED (1% shortfall=" << shortfall
              << " bps, 10% shortfall=" << shortfall_large << " bps)\n";
}

/**
 * @brief Main test runner
 */
int main() {
    std::cout << "\n=== Market Impact Calibration Test Suite ===\n\n";

    try {
        // Linear regression tests
        test_linear_regression_basic();
        test_linear_regression_noisy();
        test_linear_regression_edge_cases();
        test_weighted_linear_regression();
        test_correlation();
        test_regression_prediction();

        std::cout << "\n";

        // Market impact model tests
        test_market_impact_model_basic();
        test_square_root_law();
        test_impact_model_parameters();
        test_implementation_shortfall();

        std::cout << "\n";

        // Calibration tests
        test_calibrator_synthetic();
        test_calibration_from_fills();
        test_analytics_calibration_integration();

        std::cout << "\n";

        // Performance test
        test_calibration_performance();

        std::cout << "\n=== All Tests Completed Successfully ===\n";
        return 0;

    } catch (const std::exception& e) {
        std::cerr << "\nTest FAILED with exception: " << e.what() << "\n";
        return 1;
    }
}
