#pragma once

#include "execution_algorithm.hpp"
#include "market_impact_calibration.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <vector>

/**
 * @class AlmgrenChrissStrategy
 * @brief Optimal execution strategy based on Almgren-Chriss model
 *
 * The Almgren-Chriss model provides an optimal trade execution strategy that
 * balances market impact costs against timing risk (price volatility).
 * It minimizes the expected cost of execution considering:
 * - Permanent market impact (price moves permanently due to trading)
 * - Temporary market impact (transient price moves during execution)
 * - Volatility risk (price may move adversely during execution)
 *
 * Key characteristics:
 * - Trades more aggressively when volatility is low (timing risk low)
 * - Trades more passively when market impact is high
 * - Optimal trajectory is typically front-loaded (executes more early)
 * - Provides theoretical minimum cost execution
 *
 * The strategy computes an optimal trading trajectory using:
 * - Risk aversion parameter (lambda): higher = more risk-averse, slower execution
 * - Permanent impact coefficient (gamma): price move per unit traded
 * - Temporary impact coefficient (eta): temporary price impact
 * - Volatility (sigma): price volatility per unit time
 *
 * Use cases:
 * - Large institutional orders where impact is significant
 * - Situations requiring optimal cost-risk tradeoff
 * - Benchmark for sophisticated execution algorithms
 * - Markets with measurable impact and volatility
 *
 * Reference: Almgren, R., & Chriss, N. (2000). "Optimal execution of portfolio
 * transactions." Journal of Risk.
 */
class AlmgrenChrissStrategy : public ExecutionAlgorithm {
public:
    using Duration = std::chrono::milliseconds;

private:
    Duration duration_;                     ///< Total execution duration
    Duration slice_interval_;               ///< Time between slices
    size_t num_slices_;                     ///< Total number of slices
    size_t current_slice_ = 0;              ///< Current slice index
    TimePoint last_slice_time_;             ///< Time of last slice execution
    bool use_limit_orders_ = false;         ///< Use limit vs market orders
    double limit_offset_bps_ = 0.0;         ///< Offset from mid for limit orders

    // Almgren-Chriss model parameters
    double risk_aversion_ = 1e-6;           ///< Risk aversion parameter (lambda)
    double permanent_impact_ = 0.1;         ///< Permanent impact coef (gamma)
    double temporary_impact_ = 0.01;        ///< Temporary impact coef (eta)
    double volatility_ = 0.02;              ///< Daily volatility (sigma)
    double adv_ = 1000000.0;                ///< Average daily volume

    // Optimal trajectory
    std::vector<double> trajectory_;        ///< Optimal holdings over time [0,1]
    std::vector<uint64_t> slice_sizes_;     ///< Pre-computed slice sizes
    bool trajectory_computed_ = false;

public:
    /**
     * @brief Constructor with duration in minutes
     * @param target_qty Target quantity to execute
     * @param duration_minutes Total execution duration in minutes
     * @param num_slices Number of slices to divide into
     * @param is_buy true for buy, false for sell
     */
    AlmgrenChrissStrategy(uint64_t target_qty, int duration_minutes, size_t num_slices,
                          bool is_buy = true)
        : ExecutionAlgorithm(target_qty, is_buy),
          duration_(std::chrono::minutes(duration_minutes)),
          num_slices_(num_slices > 0 ? num_slices : 1)
    {
        strategy_name_ = "Almgren-Chriss";
        slice_interval_ = Duration(duration_.count() / num_slices_);
    }

    /**
     * @brief Constructor with custom duration
     * @param target_qty Target quantity to execute
     * @param duration Total execution duration
     * @param num_slices Number of slices to divide into
     * @param is_buy true for buy, false for sell
     */
    AlmgrenChrissStrategy(uint64_t target_qty, Duration duration, size_t num_slices,
                          bool is_buy = true)
        : ExecutionAlgorithm(target_qty, is_buy),
          duration_(duration),
          num_slices_(num_slices > 0 ? num_slices : 1)
    {
        strategy_name_ = "Almgren-Chriss";
        slice_interval_ = Duration(duration_.count() / num_slices_);
    }

    /**
     * @brief Constructor with market impact model
     * @param target_qty Target quantity to execute
     * @param duration_minutes Total execution duration in minutes
     * @param num_slices Number of slices
     * @param impact_model Calibrated market impact model
     * @param is_buy true for buy, false for sell
     */
    AlmgrenChrissStrategy(uint64_t target_qty, int duration_minutes, size_t num_slices,
                          const MarketImpactModel& impact_model, bool is_buy = true)
        : ExecutionAlgorithm(target_qty, is_buy),
          duration_(std::chrono::minutes(duration_minutes)),
          num_slices_(num_slices > 0 ? num_slices : 1)
    {
        strategy_name_ = "Almgren-Chriss";
        slice_interval_ = Duration(duration_.count() / num_slices_);

        // Extract parameters from impact model
        permanent_impact_ = impact_model.get_permanent_coef();
        temporary_impact_ = impact_model.get_temporary_coef();
        adv_ = impact_model.get_adv();
    }

    /**
     * @brief Constructor with custom duration and market impact model
     * @param target_qty Target quantity to execute
     * @param duration Total execution duration
     * @param num_slices Number of slices
     * @param impact_model Calibrated market impact model
     * @param is_buy true for buy, false for sell
     */
    AlmgrenChrissStrategy(uint64_t target_qty, Duration duration, size_t num_slices,
                          const MarketImpactModel& impact_model, bool is_buy = true)
        : ExecutionAlgorithm(target_qty, is_buy),
          duration_(duration),
          num_slices_(num_slices > 0 ? num_slices : 1)
    {
        strategy_name_ = "Almgren-Chriss";
        slice_interval_ = Duration(duration_.count() / num_slices_);

        // Extract parameters from impact model
        permanent_impact_ = impact_model.get_permanent_coef();
        temporary_impact_ = impact_model.get_temporary_coef();
        adv_ = impact_model.get_adv();
    }

    /**
     * @brief Sets market impact parameters
     * @param permanent Permanent impact coefficient (gamma)
     * @param temporary Temporary impact coefficient (eta)
     * @param adv_volume Average daily volume
     */
    void set_market_impact(double permanent, double temporary, double adv_volume) {
        permanent_impact_ = permanent;
        temporary_impact_ = temporary;
        adv_ = adv_volume;
        trajectory_computed_ = false;  // Recompute trajectory
    }

    /**
     * @brief Sets risk aversion parameter
     * @param lambda Risk aversion (higher = more risk-averse, slower execution)
     *               Typical range: 1e-8 (aggressive) to 1e-4 (conservative)
     */
    void set_risk_aversion(double lambda) {
        risk_aversion_ = std::max(1e-10, lambda);
        trajectory_computed_ = false;  // Recompute trajectory
    }

    /**
     * @brief Sets volatility parameter
     * @param sigma Daily price volatility (e.g., 0.02 = 2% daily vol)
     */
    void set_volatility(double sigma) {
        volatility_ = std::max(0.001, sigma);
        trajectory_computed_ = false;  // Recompute trajectory
    }

    /**
     * @brief Sets whether to use limit orders
     * @param use_limit true to use limit orders
     * @param offset_bps Offset from mid price in basis points
     */
    void set_use_limit_orders(bool use_limit, double offset_bps = 0.0) {
        use_limit_orders_ = use_limit;
        limit_offset_bps_ = offset_bps;
    }

    /**
     * @brief Computes the optimal trading trajectory
     *
     * Uses the Almgren-Chriss formula to compute optimal holdings over time.
     * The trajectory balances market impact costs against timing risk.
     */
    void compute_trajectory() {
        trajectory_.resize(num_slices_ + 1);
        slice_sizes_.resize(num_slices_);

        // Time per slice in units of day (assuming duration is intraday)
        double tau = (duration_.count() / 1000.0) / 86400.0;  // Convert ms to days
        double dt = tau / num_slices_;  // Time per slice

        // Almgren-Chriss parameters
        // Kappa = permanent impact rate scaled by ADV
        double kappa = permanent_impact_ / adv_;

        // Calculate optimal urgency parameter (kappa_tilde)
        // kappa_tilde = sqrt(lambda * sigma^2 / eta)
        double kappa_tilde = std::sqrt(risk_aversion_ * volatility_ * volatility_ /
                                       (temporary_impact_ / adv_));

        // Calculate sinh and cosh terms
        double sinh_term = std::sinh(kappa_tilde * tau);
        double cosh_term = std::cosh(kappa_tilde * tau);

        // Compute optimal trajectory: x(t) = X * sinh(kappa_tilde * (T - t)) / sinh(kappa_tilde * T)
        // where x(t) is remaining shares at time t, X is initial shares
        for (size_t i = 0; i <= num_slices_; ++i) {
            double t = i * dt;
            double time_remaining = tau - t;

            if (sinh_term > 0) {
                trajectory_[i] = std::sinh(kappa_tilde * time_remaining) / sinh_term;
            } else {
                // Fallback to linear if numerical issues
                trajectory_[i] = time_remaining / tau;
            }
        }

        // Compute slice sizes from trajectory (differences)
        uint64_t allocated = 0;
        for (size_t i = 0; i < num_slices_; ++i) {
            double fraction = trajectory_[i] - trajectory_[i + 1];
            slice_sizes_[i] = static_cast<uint64_t>(target_quantity_ * fraction);
            allocated += slice_sizes_[i];
        }

        // Handle rounding errors - add remainder to first slice
        if (allocated < target_quantity_) {
            slice_sizes_[0] += (target_quantity_ - allocated);
        } else if (allocated > target_quantity_) {
            // Subtract excess from largest slice
            auto max_it = std::max_element(slice_sizes_.begin(), slice_sizes_.end());
            if (*max_it >= (allocated - target_quantity_)) {
                *max_it -= (allocated - target_quantity_);
            }
        }

        trajectory_computed_ = true;
    }

    /**
     * @brief Computes child orders for Almgren-Chriss execution
     * @param data Current market data
     * @return Vector of orders (0 or 1 orders)
     */
    std::vector<Order> compute_child_orders(const MarketData& data) override {
        // Compute trajectory on first call
        if (!trajectory_computed_) {
            compute_trajectory();
        }

        // Initialize timing on first call
        if (current_slice_ == 0 && !started_) {
            last_slice_time_ = data.timestamp;
        }

        // Check if it's time for next slice
        if (!is_time_for_slice(data.timestamp)) {
            return {};
        }

        // Calculate slice size
        uint64_t slice_size = calculate_slice_size();
        if (slice_size == 0) {
            return {};
        }

        // Update state
        current_slice_++;
        last_slice_time_ = data.timestamp;

        // Create order
        return {create_slice_order(data, static_cast<int>(slice_size))};
    }

    /**
     * @brief Resets strategy state
     */
    void reset() override {
        ExecutionAlgorithm::reset();
        current_slice_ = 0;
        trajectory_computed_ = false;
    }

    /**
     * @brief Gets the number of slices
     * @return Total slice count
     */
    size_t get_num_slices() const {
        return num_slices_;
    }

    /**
     * @brief Gets the current slice index
     * @return Current slice (0-indexed)
     */
    size_t get_current_slice() const {
        return current_slice_;
    }

    /**
     * @brief Gets the optimal trajectory
     * @return Vector of optimal holdings [0,1] at each time point
     */
    const std::vector<double>& get_trajectory() const {
        return trajectory_;
    }

    /**
     * @brief Gets the pre-computed slice sizes
     * @return Vector of slice sizes
     */
    const std::vector<uint64_t>& get_slice_sizes() const {
        return slice_sizes_;
    }

    /**
     * @brief Gets risk aversion parameter
     * @return Lambda value
     */
    double get_risk_aversion() const {
        return risk_aversion_;
    }

    /**
     * @brief Gets permanent impact coefficient
     * @return Gamma value
     */
    double get_permanent_impact() const {
        return permanent_impact_;
    }

    /**
     * @brief Gets temporary impact coefficient
     * @return Eta value
     */
    double get_temporary_impact() const {
        return temporary_impact_;
    }

    /**
     * @brief Gets volatility
     * @return Sigma value
     */
    double get_volatility() const {
        return volatility_;
    }

    /**
     * @brief Estimates expected cost of execution (in basis points)
     * @return Expected cost in bps
     */
    double estimate_expected_cost() const {
        if (!trajectory_computed_) {
            return 0.0;
        }

        // Simplified cost estimate:
        // Cost = permanent_impact * X + temporary_impact * sum(trades^2) + timing_cost
        double X = static_cast<double>(target_quantity_);
        double perm_cost = permanent_impact_ * X / adv_;

        double temp_cost = 0.0;
        for (const auto& size : slice_sizes_) {
            double n = static_cast<double>(size) / adv_;
            temp_cost += temporary_impact_ * n * n;
        }

        // Timing risk cost (simplified)
        double tau = (duration_.count() / 1000.0) / 86400.0;
        double timing_cost = 0.5 * risk_aversion_ * volatility_ * volatility_ *
                            X * X * tau / (num_slices_ * adv_ * adv_);

        // Convert to basis points (approximate)
        double total_cost = perm_cost + temp_cost + timing_cost;
        return total_cost * 10000.0;  // Convert to bps
    }

    /**
     * @brief Prints Almgren-Chriss configuration
     */
    void print_config() const {
        std::cout << "\n=== Almgren-Chriss Configuration ===\n";
        std::cout << "  Target quantity: " << target_quantity_ << "\n";
        std::cout << "  Duration: " << duration_.count() << " ms\n";
        std::cout << "  Num slices: " << num_slices_ << "\n";
        std::cout << "  Slice interval: " << slice_interval_.count() << " ms\n";
        std::cout << "  Risk aversion (lambda): " << risk_aversion_ << "\n";
        std::cout << "  Permanent impact (gamma): " << permanent_impact_ << "\n";
        std::cout << "  Temporary impact (eta): " << temporary_impact_ << "\n";
        std::cout << "  Volatility (sigma): " << volatility_ << "\n";
        std::cout << "  ADV: " << adv_ << "\n";
        std::cout << "  Use limit orders: " << (use_limit_orders_ ? "Yes" : "No") << "\n";
        std::cout << "  Direction: " << (is_buy_ ? "BUY" : "SELL") << "\n";

        if (trajectory_computed_ && estimate_expected_cost() > 0) {
            std::cout << "  Est. cost: " << std::fixed << std::setprecision(2)
                      << estimate_expected_cost() << " bps\n";
        }

        if (trajectory_computed_ && num_slices_ <= 20) {
            std::cout << "  Slice sizes: ";
            for (size_t i = 0; i < slice_sizes_.size(); ++i) {
                std::cout << slice_sizes_[i];
                if (i < slice_sizes_.size() - 1) std::cout << ", ";
            }
            std::cout << "\n";
        }
    }

private:
    /**
     * @brief Checks if it's time to execute the next slice
     * @param current_time Current timestamp
     * @return true if next slice should execute
     */
    bool is_time_for_slice(TimePoint current_time) const {
        // First slice executes immediately
        if (current_slice_ == 0) {
            return true;
        }

        // Check if interval has elapsed since last slice
        auto elapsed_since_last = std::chrono::duration_cast<Duration>(
            current_time - last_slice_time_);

        return elapsed_since_last >= slice_interval_;
    }

    /**
     * @brief Calculates the size for the current slice
     * @return Slice quantity
     */
    uint64_t calculate_slice_size() {
        uint64_t remaining = remaining_quantity();
        if (remaining == 0) return 0;

        // Last slice gets all remaining
        if (current_slice_ >= num_slices_ - 1) {
            return remaining;
        }

        // Get pre-computed optimal slice size
        uint64_t optimal_slice = slice_sizes_[current_slice_];

        // Ensure we don't exceed remaining
        return std::min(optimal_slice, remaining);
    }

    /**
     * @brief Creates an order for the current slice
     * @param data Current market data
     * @param quantity Slice quantity
     * @return Order to execute
     */
    Order create_slice_order(const MarketData& data, int quantity) {
        if (use_limit_orders_) {
            // Calculate limit price with offset
            double offset = data.price * (limit_offset_bps_ / 10000.0);
            double limit_price = is_buy_
                ? data.ask_price + offset  // For buys, offset above ask
                : data.bid_price - offset; // For sells, offset below bid

            if (limit_price <= 0) {
                limit_price = data.price;
            }

            return create_limit_order(limit_price, quantity);
        } else {
            // Market order
            return create_market_order(quantity);
        }
    }
};
