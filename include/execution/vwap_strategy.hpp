#pragma once

#include "execution_algorithm.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <numeric>
#include <vector>

/**
 * @class VWAPStrategy
 * @brief Volume-Weighted Average Price execution algorithm
 *
 * VWAP divides a large order into slices sized proportionally to expected
 * volume patterns throughout the trading day. Unlike TWAP which uses equal
 * time slices, VWAP adapts to intraday volume patterns to minimize market impact.
 *
 * Key characteristics:
 * - Slices are sized based on historical or predicted volume patterns
 * - Executes more during high-volume periods, less during low-volume periods
 * - Minimizes market impact by matching natural market flow
 * - Commonly used benchmark for execution quality
 *
 * Volume Profile Options:
 * - Uniform: Equal volume in each slice (equivalent to TWAP)
 * - U-Shaped: Higher volume at open/close, lower midday
 * - Custom: User-provided volume distribution
 *
 * Use cases:
 * - Large orders where minimizing market impact is critical
 * - Benchmarking execution performance
 * - Markets with predictable intraday volume patterns
 *
 * Implementation shortfall typically 10-30% lower than TWAP
 * in markets with strong intraday volume patterns.
 */
class VWAPStrategy : public ExecutionAlgorithm {
public:
    using Duration = std::chrono::milliseconds;

    /**
     * @enum VolumeProfile
     * @brief Predefined intraday volume patterns
     */
    enum class VolumeProfile {
        UNIFORM,    ///< Equal volume distribution (like TWAP)
        U_SHAPED,   ///< High at open/close, low midday
        MORNING,    ///< Front-loaded, higher early volume
        AFTERNOON,  ///< Back-loaded, higher late volume
        CUSTOM      ///< User-provided distribution
    };

private:
    Duration duration_;                     ///< Total execution duration
    Duration slice_interval_;               ///< Time between slices
    size_t num_slices_;                     ///< Total number of slices
    size_t current_slice_ = 0;              ///< Current slice index
    TimePoint last_slice_time_;             ///< Time of last slice execution
    bool use_limit_orders_ = false;         ///< Use limit vs market orders
    double limit_offset_bps_ = 0.0;         ///< Offset from mid for limit orders

    // Volume profile
    VolumeProfile profile_type_ = VolumeProfile::U_SHAPED;
    std::vector<double> volume_weights_;    ///< Per-slice volume weights
    std::vector<uint64_t> slice_sizes_;     ///< Pre-computed slice sizes

    // Real-time volume tracking
    bool use_real_time_volume_ = false;     ///< Adapt to actual market volume
    uint64_t last_market_volume_ = 0;       ///< Last observed market volume
    double volume_participation_rate_ = 0.1; ///< Target % of market volume

public:
    /**
     * @brief Constructor with duration in minutes and volume profile
     * @param target_qty Target quantity to execute
     * @param duration_minutes Total execution duration in minutes
     * @param num_slices Number of slices to divide into
     * @param profile Volume profile pattern
     * @param is_buy true for buy, false for sell
     */
    VWAPStrategy(uint64_t target_qty, int duration_minutes, size_t num_slices,
                 VolumeProfile profile = VolumeProfile::U_SHAPED, bool is_buy = true)
        : ExecutionAlgorithm(target_qty, is_buy),
          duration_(std::chrono::minutes(duration_minutes)),
          num_slices_(num_slices > 0 ? num_slices : 1),
          profile_type_(profile)
    {
        strategy_name_ = "VWAP";
        slice_interval_ = Duration(duration_.count() / num_slices_);
        initialize_volume_profile();
        compute_slice_sizes();
    }

    /**
     * @brief Constructor with custom duration
     * @param target_qty Target quantity to execute
     * @param duration Total execution duration
     * @param num_slices Number of slices to divide into
     * @param profile Volume profile pattern
     * @param is_buy true for buy, false for sell
     */
    VWAPStrategy(uint64_t target_qty, Duration duration, size_t num_slices,
                 VolumeProfile profile = VolumeProfile::U_SHAPED, bool is_buy = true)
        : ExecutionAlgorithm(target_qty, is_buy),
          duration_(duration),
          num_slices_(num_slices > 0 ? num_slices : 1),
          profile_type_(profile)
    {
        strategy_name_ = "VWAP";
        slice_interval_ = Duration(duration_.count() / num_slices_);
        initialize_volume_profile();
        compute_slice_sizes();
    }

    /**
     * @brief Sets custom volume weights for each slice
     * @param weights Vector of weights (will be normalized to sum to 1.0)
     */
    void set_custom_volume_weights(const std::vector<double>& weights) {
        if (weights.size() != num_slices_) {
            std::cerr << "Warning: weight count mismatch, expected " << num_slices_
                      << " got " << weights.size() << "\n";
            return;
        }

        profile_type_ = VolumeProfile::CUSTOM;
        volume_weights_ = weights;

        // Normalize weights to sum to 1.0
        double sum = std::accumulate(weights.begin(), weights.end(), 0.0);
        if (sum > 0) {
            for (auto& w : volume_weights_) {
                w /= sum;
            }
        }

        compute_slice_sizes();
    }

    /**
     * @brief Enables real-time volume adaptation
     * @param enable Whether to adapt to real market volume
     * @param participation_rate Target percentage of market volume (0.0 to 1.0)
     */
    void set_real_time_volume(bool enable, double participation_rate = 0.1) {
        use_real_time_volume_ = enable;
        volume_participation_rate_ = std::clamp(participation_rate, 0.01, 0.5);
    }

    /**
     * @brief Sets whether to use limit orders instead of market orders
     * @param use_limit true to use limit orders
     * @param offset_bps Offset from mid price in basis points
     */
    void set_use_limit_orders(bool use_limit, double offset_bps = 0.0) {
        use_limit_orders_ = use_limit;
        limit_offset_bps_ = offset_bps;
    }

    /**
     * @brief Computes child orders for VWAP execution
     * @param data Current market data
     * @return Vector of orders (0 or 1 orders)
     *
     * VWAP logic:
     * 1. Check if it's time for the next slice
     * 2. Get pre-computed slice size based on volume profile
     * 3. Optionally adjust based on real-time market volume
     * 4. Generate order at current market price
     */
    std::vector<Order> compute_child_orders(const MarketData& data) override {
        // Initialize timing on first call
        if (current_slice_ == 0 && !started_) {
            last_slice_time_ = data.timestamp;
        }

        // Check if it's time for next slice
        if (!is_time_for_slice(data.timestamp)) {
            return {};
        }

        // Calculate slice size
        uint64_t slice_size = calculate_slice_size(data);
        if (slice_size == 0) {
            return {};
        }

        // Update state
        current_slice_++;
        last_slice_time_ = data.timestamp;
        if (data.total_volume > 0) {
            last_market_volume_ = data.total_volume;
        }

        // Create order
        return {create_slice_order(data, static_cast<int>(slice_size))};
    }

    /**
     * @brief Resets strategy state
     */
    void reset() override {
        ExecutionAlgorithm::reset();
        current_slice_ = 0;
        last_market_volume_ = 0;
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
     * @brief Gets the volume profile type
     * @return VolumeProfile enum
     */
    VolumeProfile get_profile_type() const {
        return profile_type_;
    }

    /**
     * @brief Gets the volume weights vector
     * @return Vector of normalized weights
     */
    const std::vector<double>& get_volume_weights() const {
        return volume_weights_;
    }

    /**
     * @brief Gets the pre-computed slice sizes
     * @return Vector of slice sizes
     */
    const std::vector<uint64_t>& get_slice_sizes() const {
        return slice_sizes_;
    }

    /**
     * @brief Prints VWAP configuration
     */
    void print_config() const {
        std::cout << "\n=== VWAP Configuration ===\n";
        std::cout << "  Target quantity: " << target_quantity_ << "\n";
        std::cout << "  Duration: " << duration_.count() << " ms\n";
        std::cout << "  Num slices: " << num_slices_ << "\n";
        std::cout << "  Slice interval: " << slice_interval_.count() << " ms\n";
        std::cout << "  Profile: " << profile_name() << "\n";
        std::cout << "  Use limit orders: " << (use_limit_orders_ ? "Yes" : "No") << "\n";
        std::cout << "  Real-time volume: " << (use_real_time_volume_ ? "Yes" : "No") << "\n";
        std::cout << "  Direction: " << (is_buy_ ? "BUY" : "SELL") << "\n";

        if (num_slices_ <= 20) {
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
     * @brief Initializes the volume profile weights
     */
    void initialize_volume_profile() {
        volume_weights_.resize(num_slices_);

        switch (profile_type_) {
            case VolumeProfile::UNIFORM:
                // Equal weight for all slices
                std::fill(volume_weights_.begin(), volume_weights_.end(),
                         1.0 / num_slices_);
                break;

            case VolumeProfile::U_SHAPED:
                // Higher volume at beginning and end, lower in middle
                for (size_t i = 0; i < num_slices_; ++i) {
                    double t = static_cast<double>(i) / (num_slices_ - 1);
                    // U-shaped curve: higher at 0 and 1, lower at 0.5
                    double weight = 1.0 + 2.0 * std::pow(t - 0.5, 2);
                    volume_weights_[i] = weight;
                }
                break;

            case VolumeProfile::MORNING:
                // Front-loaded: exponentially decreasing
                for (size_t i = 0; i < num_slices_; ++i) {
                    double t = static_cast<double>(i) / num_slices_;
                    volume_weights_[i] = std::exp(-2.0 * t);
                }
                break;

            case VolumeProfile::AFTERNOON:
                // Back-loaded: exponentially increasing
                for (size_t i = 0; i < num_slices_; ++i) {
                    double t = static_cast<double>(i) / num_slices_;
                    volume_weights_[i] = std::exp(2.0 * (t - 1.0));
                }
                break;

            case VolumeProfile::CUSTOM:
                // Will be set via set_custom_volume_weights()
                std::fill(volume_weights_.begin(), volume_weights_.end(),
                         1.0 / num_slices_);
                break;
        }

        // Normalize weights to sum to 1.0
        double sum = std::accumulate(volume_weights_.begin(), volume_weights_.end(), 0.0);
        if (sum > 0) {
            for (auto& w : volume_weights_) {
                w /= sum;
            }
        }
    }

    /**
     * @brief Pre-computes slice sizes based on volume profile
     */
    void compute_slice_sizes() {
        slice_sizes_.resize(num_slices_);
        uint64_t allocated = 0;

        // Allocate based on weights
        for (size_t i = 0; i < num_slices_; ++i) {
            slice_sizes_[i] = static_cast<uint64_t>(
                target_quantity_ * volume_weights_[i]);
            allocated += slice_sizes_[i];
        }

        // Distribute any remaining quantity due to rounding
        if (allocated < target_quantity_) {
            uint64_t remainder = target_quantity_ - allocated;
            // Add to slices with highest weights
            for (size_t i = 0; i < remainder && i < num_slices_; ++i) {
                slice_sizes_[i]++;
            }
        }
    }

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
     * @param data Current market data
     * @return Slice quantity
     */
    uint64_t calculate_slice_size(const MarketData& data) {
        uint64_t remaining = remaining_quantity();
        if (remaining == 0) return 0;

        // Last slice gets all remaining
        if (current_slice_ >= num_slices_ - 1) {
            return remaining;
        }

        // Get base slice size from pre-computed profile
        uint64_t base_slice = slice_sizes_[current_slice_];

        // Adjust based on real-time volume if enabled
        if (use_real_time_volume_ && data.total_volume > last_market_volume_) {
            uint64_t interval_volume = data.total_volume - last_market_volume_;
            if (interval_volume > 0) {
                // Target our participation rate of interval volume
                uint64_t target_slice = static_cast<uint64_t>(
                    interval_volume * volume_participation_rate_);

                // Blend with base slice (70% real-time, 30% profile)
                base_slice = static_cast<uint64_t>(
                    0.7 * target_slice + 0.3 * base_slice);
            }
        }

        // Ensure we don't exceed remaining
        return std::min(base_slice, remaining);
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

    /**
     * @brief Gets the profile name as string
     * @return Profile name
     */
    std::string profile_name() const {
        switch (profile_type_) {
            case VolumeProfile::UNIFORM: return "Uniform";
            case VolumeProfile::U_SHAPED: return "U-Shaped";
            case VolumeProfile::MORNING: return "Morning-Weighted";
            case VolumeProfile::AFTERNOON: return "Afternoon-Weighted";
            case VolumeProfile::CUSTOM: return "Custom";
            default: return "Unknown";
        }
    }
};
