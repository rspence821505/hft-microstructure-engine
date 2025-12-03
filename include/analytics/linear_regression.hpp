#pragma once

#include <cmath>
#include <stdexcept>
#include <vector>

/**
 * @struct RegressionResult
 * @brief Result of an ordinary least squares linear regression
 *
 * Contains the fitted parameters and goodness-of-fit metrics
 * for a simple linear regression model: y = slope * x + intercept
 */
struct RegressionResult {
    double slope;       ///< Coefficient for the independent variable
    double intercept;   ///< Y-intercept of the fitted line
    double r_squared;   ///< R² goodness-of-fit (0 to 1)
    double std_error;   ///< Standard error of the estimate
    size_t n_samples;   ///< Number of data points used

    /**
     * @brief Predicts y value for a given x
     * @param x Input value
     * @return Predicted y value
     */
    double predict(double x) const {
        return slope * x + intercept;
    }

    /**
     * @brief Checks if the fit is statistically meaningful
     * @param min_r_squared Minimum R² threshold (default 0.1)
     * @return true if R² exceeds threshold
     */
    bool is_valid(double min_r_squared = 0.1) const {
        return r_squared >= min_r_squared && n_samples >= 3;
    }
};

/**
 * @brief Performs ordinary least squares linear regression
 * @param x Vector of independent variable values
 * @param y Vector of dependent variable values
 * @return RegressionResult containing fitted parameters
 * @throws std::invalid_argument if vectors have different sizes or < 2 points
 *
 * Uses the closed-form OLS solution:
 *   slope = Σ((x_i - x̄)(y_i - ȳ)) / Σ((x_i - x̄)²)
 *   intercept = ȳ - slope * x̄
 *
 * R² is calculated as: 1 - SS_res / SS_tot
 * where SS_res = Σ(y_i - ŷ_i)² and SS_tot = Σ(y_i - ȳ)²
 */
inline RegressionResult linear_regression(
    const std::vector<double>& x,
    const std::vector<double>& y
) {
    size_t n = x.size();

    if (n != y.size()) {
        throw std::invalid_argument("x and y vectors must have same size");
    }
    if (n < 2) {
        throw std::invalid_argument("Need at least 2 data points for regression");
    }

    // Calculate sums for the closed-form solution
    double sum_x = 0.0, sum_y = 0.0;
    double sum_xy = 0.0, sum_x2 = 0.0, sum_y2 = 0.0;

    for (size_t i = 0; i < n; ++i) {
        sum_x += x[i];
        sum_y += y[i];
        sum_xy += x[i] * y[i];
        sum_x2 += x[i] * x[i];
        sum_y2 += y[i] * y[i];
    }

    double mean_x = sum_x / static_cast<double>(n);
    double mean_y = sum_y / static_cast<double>(n);

    // Calculate slope using the formula:
    // slope = (Σxy - n*x̄*ȳ) / (Σx² - n*x̄²)
    double denominator = sum_x2 - static_cast<double>(n) * mean_x * mean_x;

    RegressionResult result;
    result.n_samples = n;

    if (std::abs(denominator) < 1e-10) {
        // No variance in x - return horizontal line at mean_y
        result.slope = 0.0;
        result.intercept = mean_y;
        result.r_squared = 0.0;
        result.std_error = 0.0;
        return result;
    }

    result.slope = (sum_xy - static_cast<double>(n) * mean_x * mean_y) / denominator;
    result.intercept = mean_y - result.slope * mean_x;

    // Calculate R-squared and standard error
    double ss_tot = sum_y2 - static_cast<double>(n) * mean_y * mean_y;
    double ss_res = 0.0;

    for (size_t i = 0; i < n; ++i) {
        double predicted = result.slope * x[i] + result.intercept;
        double residual = y[i] - predicted;
        ss_res += residual * residual;
    }

    // R² = 1 - SS_res / SS_tot
    result.r_squared = (std::abs(ss_tot) < 1e-10) ? 1.0 : (1.0 - ss_res / ss_tot);

    // Clamp R² to [0, 1] to handle numerical issues
    result.r_squared = std::max(0.0, std::min(1.0, result.r_squared));

    // Standard error of the estimate
    if (n > 2) {
        result.std_error = std::sqrt(ss_res / static_cast<double>(n - 2));
    } else {
        result.std_error = 0.0;
    }

    return result;
}

/**
 * @brief Performs weighted least squares linear regression
 * @param x Vector of independent variable values
 * @param y Vector of dependent variable values
 * @param weights Vector of weights for each observation
 * @return RegressionResult containing fitted parameters
 * @throws std::invalid_argument if vectors have different sizes or < 2 points
 *
 * Each observation is weighted by w_i in the least squares objective:
 *   minimize Σ w_i * (y_i - (slope * x_i + intercept))²
 */
inline RegressionResult weighted_linear_regression(
    const std::vector<double>& x,
    const std::vector<double>& y,
    const std::vector<double>& weights
) {
    size_t n = x.size();

    if (n != y.size() || n != weights.size()) {
        throw std::invalid_argument("x, y, and weights vectors must have same size");
    }
    if (n < 2) {
        throw std::invalid_argument("Need at least 2 data points for regression");
    }

    // Calculate weighted sums
    double sum_w = 0.0;
    double sum_wx = 0.0, sum_wy = 0.0;
    double sum_wxy = 0.0, sum_wx2 = 0.0, sum_wy2 = 0.0;

    for (size_t i = 0; i < n; ++i) {
        double w = weights[i];
        sum_w += w;
        sum_wx += w * x[i];
        sum_wy += w * y[i];
        sum_wxy += w * x[i] * y[i];
        sum_wx2 += w * x[i] * x[i];
        sum_wy2 += w * y[i] * y[i];
    }

    double mean_x = sum_wx / sum_w;
    double mean_y = sum_wy / sum_w;

    double denominator = sum_wx2 - sum_w * mean_x * mean_x;

    RegressionResult result;
    result.n_samples = n;

    if (std::abs(denominator) < 1e-10) {
        result.slope = 0.0;
        result.intercept = mean_y;
        result.r_squared = 0.0;
        result.std_error = 0.0;
        return result;
    }

    result.slope = (sum_wxy - sum_w * mean_x * mean_y) / denominator;
    result.intercept = mean_y - result.slope * mean_x;

    // Calculate weighted R-squared
    double ss_tot = sum_wy2 - sum_w * mean_y * mean_y;
    double ss_res = 0.0;

    for (size_t i = 0; i < n; ++i) {
        double predicted = result.slope * x[i] + result.intercept;
        double residual = y[i] - predicted;
        ss_res += weights[i] * residual * residual;
    }

    result.r_squared = (std::abs(ss_tot) < 1e-10) ? 1.0 : (1.0 - ss_res / ss_tot);
    result.r_squared = std::max(0.0, std::min(1.0, result.r_squared));

    if (n > 2) {
        result.std_error = std::sqrt(ss_res / (sum_w * (n - 2) / n));
    } else {
        result.std_error = 0.0;
    }

    return result;
}

/**
 * @brief Computes correlation coefficient between two vectors
 * @param x First vector
 * @param y Second vector
 * @return Pearson correlation coefficient (-1 to 1)
 */
inline double correlation(
    const std::vector<double>& x,
    const std::vector<double>& y
) {
    if (x.size() != y.size() || x.size() < 2) {
        return 0.0;
    }

    size_t n = x.size();
    double sum_x = 0.0, sum_y = 0.0;
    double sum_xy = 0.0, sum_x2 = 0.0, sum_y2 = 0.0;

    for (size_t i = 0; i < n; ++i) {
        sum_x += x[i];
        sum_y += y[i];
        sum_xy += x[i] * y[i];
        sum_x2 += x[i] * x[i];
        sum_y2 += y[i] * y[i];
    }

    double num = n * sum_xy - sum_x * sum_y;
    double den = std::sqrt((n * sum_x2 - sum_x * sum_x) *
                           (n * sum_y2 - sum_y * sum_y));

    return (std::abs(den) < 1e-10) ? 0.0 : num / den;
}
