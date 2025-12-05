#pragma once

#include "linear_regression.hpp"
#include "market_events.hpp"

// Include order book headers (local copies)
#include "fill.hpp"
#include "fill_router.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

/**
 * @struct ImpactModelParameters
 * @brief Calibrated parameters for a market impact model
 *
 * Contains the fitted coefficients for the square-root impact model:
 *   Impact = permanent_coeff * sqrt(volume/ADV) + temporary_coeff * sqrt(volume/ADV)
 *
 * The permanent impact represents the lasting price change after trading,
 * while temporary impact captures the transient component that reverts.
 */
struct ImpactModelParameters {
    double permanent_impact_coeff = 0.01;   ///< Permanent impact coefficient
    double temporary_impact_coeff = 0.02;   ///< Temporary impact coefficient
    double impact_exponent = 0.5;           ///< Exponent for power law (typically 0.5)
    double r_squared = 0.0;                 ///< Goodness of fit from calibration
    double std_error = 0.0;                 ///< Standard error of the regression
    size_t num_observations = 0;            ///< Number of observations used

    /**
     * @brief Checks if calibration produced valid results
     * @param min_r_squared Minimum acceptable R² (default 0.1)
     * @param min_observations Minimum number of observations (default 10)
     * @return true if calibration is valid
     */
    bool is_valid(double min_r_squared = 0.1, size_t min_observations = 10) const {
        return r_squared >= min_r_squared &&
               num_observations >= min_observations &&
               permanent_impact_coeff > 0.0;
    }
};

/**
 * @class MarketImpactModel
 * @brief Calibrated market impact model for execution cost estimation
 *
 * Implements the square-root law for market impact:
 *   Impact (bps) = coeff * (volume/ADV)^exponent * 10000
 *
 * This model can estimate both permanent and temporary impact components,
 * useful for optimal execution algorithms like Almgren-Chriss.
 */
class MarketImpactModel {
private:
    ImpactModelParameters params_;
    uint64_t default_adv_ = 10000000;  // 10M shares default

public:
    /**
     * @brief Default constructor with standard parameters
     */
    MarketImpactModel() = default;

    /**
     * @brief Constructor with specific coefficients
     * @param permanent_coeff Permanent impact coefficient
     * @param temporary_coeff Temporary impact coefficient
     * @param adv Default average daily volume
     */
    MarketImpactModel(double permanent_coeff, double temporary_coeff, uint64_t adv = 10000000)
        : default_adv_(adv) {
        params_.permanent_impact_coeff = permanent_coeff;
        params_.temporary_impact_coeff = temporary_coeff;
    }

    /**
     * @brief Constructor from calibration parameters
     * @param params Calibrated parameters
     * @param adv Default average daily volume
     */
    explicit MarketImpactModel(const ImpactModelParameters& params, uint64_t adv = 10000000)
        : params_(params), default_adv_(adv) {}

    /**
     * @brief Estimates permanent market impact
     * @param volume Trade volume
     * @param adv Average daily volume (uses default if 0)
     * @return Estimated permanent impact in basis points
     */
    double estimate_permanent_impact(uint64_t volume, uint64_t adv = 0) const {
        if (adv == 0) adv = default_adv_;
        if (adv == 0) return 0.0;

        double participation = static_cast<double>(volume) / adv;
        return params_.permanent_impact_coeff *
               std::pow(participation, params_.impact_exponent) * 10000.0;
    }

    /**
     * @brief Estimates temporary market impact
     * @param volume Trade volume
     * @param adv Average daily volume (uses default if 0)
     * @return Estimated temporary impact in basis points
     */
    double estimate_temporary_impact(uint64_t volume, uint64_t adv = 0) const {
        if (adv == 0) adv = default_adv_;
        if (adv == 0) return 0.0;

        double participation = static_cast<double>(volume) / adv;
        return params_.temporary_impact_coeff *
               std::pow(participation, params_.impact_exponent) * 10000.0;
    }

    /**
     * @brief Estimates total market impact
     * @param volume Trade volume
     * @param adv Average daily volume (uses default if 0)
     * @return Estimated total impact in basis points
     */
    double estimate_total_impact(uint64_t volume, uint64_t adv = 0) const {
        return estimate_permanent_impact(volume, adv) + estimate_temporary_impact(volume, adv);
    }

    /**
     * @brief Estimates implementation shortfall cost
     * @param volume Trade volume
     * @param adv Average daily volume
     * @param spread_bps Current bid-ask spread in basis points
     * @return Total estimated execution cost in basis points
     *
     * Implementation shortfall = spread cost + permanent impact + temporary impact
     */
    double estimate_implementation_shortfall(uint64_t volume, uint64_t adv,
                                              double spread_bps) const {
        double half_spread = spread_bps / 2.0;
        return half_spread + estimate_total_impact(volume, adv);
    }

    /**
     * @brief Gets the model parameters
     * @return Reference to parameters
     */
    const ImpactModelParameters& get_parameters() const {
        return params_;
    }

    /**
     * @brief Sets model parameters
     * @param params New parameters
     */
    void set_parameters(const ImpactModelParameters& params) {
        params_ = params;
    }

    /**
     * @brief Sets the default ADV
     * @param adv New default ADV
     */
    void set_default_adv(uint64_t adv) {
        default_adv_ = adv;
    }

    /**
     * @brief Gets the permanent impact coefficient
     * @return Permanent impact coefficient
     */
    double get_permanent_coef() const {
        return params_.permanent_impact_coeff;
    }

    /**
     * @brief Gets the temporary impact coefficient
     * @return Temporary impact coefficient
     */
    double get_temporary_coef() const {
        return params_.temporary_impact_coeff;
    }

    /**
     * @brief Gets the default ADV
     * @return Average daily volume
     */
    uint64_t get_adv() const {
        return default_adv_;
    }

    /**
     * @brief Gets the default ADV
     * @return Default ADV
     */
    uint64_t get_default_adv() const {
        return default_adv_;
    }

    /**
     * @brief Prints model summary
     */
    void print_summary() const {
        std::cout << "\n=== Market Impact Model ===\n";
        std::cout << "Permanent coefficient: " << params_.permanent_impact_coeff << "\n";
        std::cout << "Temporary coefficient: " << params_.temporary_impact_coeff << "\n";
        std::cout << "Impact exponent: " << params_.impact_exponent << "\n";
        std::cout << "Default ADV: " << default_adv_ << "\n";
        if (params_.num_observations > 0) {
            std::cout << "R²: " << params_.r_squared << "\n";
            std::cout << "Observations: " << params_.num_observations << "\n";
        }
    }
};

/**
 * @class MarketImpactCalibrator
 * @brief Calibrates market impact models from historical fill data
 *
 * Uses linear regression on log-log transformed data to fit the
 * square-root law: log(impact) = log(coeff) + exponent * log(volume/ADV)
 *
 * The calibrator can work with either raw Fill objects or EnhancedFill
 * objects from the FillRouter.
 */
class MarketImpactCalibrator {
private:
    std::vector<double> log_volumes_;
    std::vector<double> log_impacts_;
    std::vector<double> weights_;
    double min_participation_rate_ = 0.0001;  // Minimum volume/ADV to include
    double min_price_impact_ = 0.0001;        // Minimum price change to include

public:
    /**
     * @brief Default constructor
     */
    MarketImpactCalibrator() = default;

    /**
     * @brief Sets minimum participation rate for observations
     * @param rate Minimum volume/ADV ratio
     */
    void set_min_participation_rate(double rate) {
        min_participation_rate_ = rate;
    }

    /**
     * @brief Sets minimum price impact for observations
     * @param impact Minimum price change (as decimal, not bps)
     */
    void set_min_price_impact(double impact) {
        min_price_impact_ = impact;
    }

    /**
     * @brief Adds an observation for calibration
     * @param participation_rate Volume / ADV
     * @param price_impact Absolute price change as decimal
     * @param weight Optional weight for this observation
     */
    void add_observation(double participation_rate, double price_impact, double weight = 1.0) {
        if (participation_rate < min_participation_rate_ || price_impact < min_price_impact_) {
            return;
        }

        log_volumes_.push_back(std::log(participation_rate));
        log_impacts_.push_back(std::log(price_impact));
        weights_.push_back(weight);
    }

    /**
     * @brief Calibrates impact model from Fill data
     * @param fills Vector of fills to calibrate from
     * @param adv Average daily volume for the symbol
     * @return Calibrated MarketImpactModel
     *
     * Computes price impact as |price[i] - price[i-1]| / price[i-1]
     * and volume ratio as quantity / ADV.
     */
    MarketImpactModel calibrate_from_fills(const std::vector<Fill>& fills, uint64_t adv) {
        clear();

        for (size_t i = 1; i < fills.size(); ++i) {
            double volume_ratio = static_cast<double>(fills[i].quantity) / adv;
            double price_impact = std::abs(fills[i].price - fills[i-1].price) / fills[i-1].price;

            add_observation(volume_ratio, price_impact);
        }

        return calibrate(adv);
    }

    /**
     * @brief Calibrates impact model from EnhancedFill data
     * @param fills Vector of enhanced fills to calibrate from
     * @param adv Average daily volume for the symbol
     * @return Calibrated MarketImpactModel
     */
    MarketImpactModel calibrate_from_enhanced_fills(const std::vector<EnhancedFill>& fills,
                                                     uint64_t adv) {
        clear();

        for (size_t i = 1; i < fills.size(); ++i) {
            double volume_ratio = static_cast<double>(fills[i].base_fill.quantity) / adv;
            double price_impact = std::abs(fills[i].base_fill.price - fills[i-1].base_fill.price)
                                  / fills[i-1].base_fill.price;

            add_observation(volume_ratio, price_impact);
        }

        return calibrate(adv);
    }

    /**
     * @brief Performs the calibration using accumulated observations
     * @param adv Average daily volume for the model
     * @return Calibrated MarketImpactModel
     *
     * Uses linear regression on log-log data:
     *   log(impact) = log(coeff) + exponent * log(volume/ADV)
     *
     * slope gives the impact exponent (typically ~0.5 for square-root law)
     * exp(intercept) gives the impact coefficient
     */
    MarketImpactModel calibrate(uint64_t adv = 10000000) {
        ImpactModelParameters params;

        if (log_volumes_.size() < 3) {
            std::cout << "Warning: Too few observations for calibration ("
                      << log_volumes_.size() << "). Using default parameters.\n";
            return MarketImpactModel(params, adv);
        }

        // Perform regression: log(impact) = intercept + slope * log(volume)
        RegressionResult result;

        if (weights_.size() == log_volumes_.size() &&
            std::any_of(weights_.begin(), weights_.end(), [](double w) { return w != 1.0; })) {
            result = weighted_linear_regression(log_volumes_, log_impacts_, weights_);
        } else {
            result = linear_regression(log_volumes_, log_impacts_);
        }

        // Extract parameters from regression
        params.impact_exponent = result.slope;
        params.permanent_impact_coeff = std::exp(result.intercept);
        params.temporary_impact_coeff = params.permanent_impact_coeff * 2.0;  // Heuristic: temp = 2x perm
        params.r_squared = result.r_squared;
        params.std_error = result.std_error;
        params.num_observations = result.n_samples;

        // Sanity checks - revert to defaults if results are unreasonable
        if (params.impact_exponent < 0.1 || params.impact_exponent > 2.0) {
            std::cout << "Warning: Unusual impact exponent (" << params.impact_exponent
                      << "). Clamping to [0.3, 1.0].\n";
            params.impact_exponent = std::max(0.3, std::min(1.0, params.impact_exponent));
        }

        if (params.permanent_impact_coeff < 1e-6 || params.permanent_impact_coeff > 1.0) {
            std::cout << "Warning: Unusual permanent impact coefficient ("
                      << params.permanent_impact_coeff << "). Using default.\n";
            params.permanent_impact_coeff = 0.01;
            params.temporary_impact_coeff = 0.02;
        }

        return MarketImpactModel(params, adv);
    }

    /**
     * @brief Gets the number of accumulated observations
     * @return Observation count
     */
    size_t observation_count() const {
        return log_volumes_.size();
    }

    /**
     * @brief Clears all accumulated observations
     */
    void clear() {
        log_volumes_.clear();
        log_impacts_.clear();
        weights_.clear();
    }

    /**
     * @brief Prints calibration data summary
     */
    void print_data_summary() const {
        std::cout << "\n=== Calibration Data Summary ===\n";
        std::cout << "Observations: " << log_volumes_.size() << "\n";

        if (!log_volumes_.empty()) {
            double min_vol = *std::min_element(log_volumes_.begin(), log_volumes_.end());
            double max_vol = *std::max_element(log_volumes_.begin(), log_volumes_.end());
            double min_imp = *std::min_element(log_impacts_.begin(), log_impacts_.end());
            double max_imp = *std::max_element(log_impacts_.begin(), log_impacts_.end());

            std::cout << "Log volume range: [" << min_vol << ", " << max_vol << "]\n";
            std::cout << "Log impact range: [" << min_imp << ", " << max_imp << "]\n";
            std::cout << "Volume ratio range: [" << std::exp(min_vol)
                      << ", " << std::exp(max_vol) << "]\n";
            std::cout << "Impact range: [" << std::exp(min_imp)
                      << ", " << std::exp(max_imp) << "]\n";
        }
    }
};

/**
 * @brief Convenience function to calibrate impact model from fills
 * @param fills Vector of fills
 * @param adv Average daily volume
 * @return Calibrated MarketImpactModel
 */
inline MarketImpactModel calibrate_impact_model(
    const std::vector<Fill>& fills,
    uint64_t adv
) {
    MarketImpactCalibrator calibrator;
    return calibrator.calibrate_from_fills(fills, adv);
}

/**
 * @brief Convenience function to calibrate impact model from enhanced fills
 * @param fills Vector of enhanced fills
 * @param adv Average daily volume
 * @return Calibrated MarketImpactModel
 */
inline MarketImpactModel calibrate_impact_model(
    const std::vector<EnhancedFill>& fills,
    uint64_t adv
) {
    MarketImpactCalibrator calibrator;
    return calibrator.calibrate_from_enhanced_fills(fills, adv);
}
