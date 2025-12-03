#ifndef TWAP_STRATEGY_HPP
#define TWAP_STRATEGY_HPP

#include "execution_algorithm.hpp"

#include <chrono>
#include <cmath>
#include <iostream>

/**
 * @class TWAPStrategy
 * @brief Time-Weighted Average Price execution algorithm
 *
 * TWAP divides a large order into equal-sized slices and executes
 * them at regular time intervals. This is one of the simplest
 * execution algorithms and provides a benchmark for comparison.
 *
 * Key characteristics:
 * - Divides order into N equal slices (one per time interval)
 * - Executes regardless of market conditions
 * - Provides predictable execution schedule
 * - Does not adapt to volume patterns (unlike VWAP)
 *
 * Use cases:
 * - Orders where timing risk is not a concern
 * - Benchmark for comparing more sophisticated algorithms
 * - Markets with relatively stable volume throughout the day
 *
 * Implementation shortfall is typically 10-30% higher than VWAP
 * because TWAP ignores intraday volume patterns.
 */
class TWAPStrategy : public ExecutionAlgorithm {
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

    // Configuration
    bool randomize_timing_ = false;         ///< Add random jitter to timing
    double min_slice_pct_ = 0.5;            ///< Min slice as % of target slice
    double max_slice_pct_ = 1.5;            ///< Max slice as % of target slice

public:
    /**
     * @brief Constructor with duration in minutes
     * @param target_qty Target quantity to execute
     * @param duration_minutes Total execution duration in minutes
     * @param is_buy true for buy, false for sell
     */
    TWAPStrategy(uint64_t target_qty, int duration_minutes, bool is_buy = true)
        : ExecutionAlgorithm(target_qty, is_buy),
          duration_(std::chrono::minutes(duration_minutes)),
          num_slices_(static_cast<size_t>(duration_minutes))
    {
        strategy_name_ = "TWAP";
        if (num_slices_ == 0) num_slices_ = 1;
        slice_interval_ = Duration(duration_.count() / num_slices_);
    }

    /**
     * @brief Constructor with custom duration and slice count
     * @param target_qty Target quantity to execute
     * @param duration Total execution duration
     * @param num_slices Number of slices to divide into
     * @param is_buy true for buy, false for sell
     */
    TWAPStrategy(uint64_t target_qty, Duration duration, size_t num_slices, bool is_buy = true)
        : ExecutionAlgorithm(target_qty, is_buy),
          duration_(duration),
          num_slices_(num_slices > 0 ? num_slices : 1)
    {
        strategy_name_ = "TWAP";
        slice_interval_ = Duration(duration_.count() / num_slices_);
    }

    /**
     * @brief Sets whether to use limit orders instead of market orders
     * @param use_limit true to use limit orders
     * @param offset_bps Offset from mid price in basis points (positive = more aggressive)
     */
    void set_use_limit_orders(bool use_limit, double offset_bps = 0.0) {
        use_limit_orders_ = use_limit;
        limit_offset_bps_ = offset_bps;
    }

    /**
     * @brief Enables randomization of slice timing
     * @param enable Whether to randomize
     * @param min_pct Minimum slice as percentage of target (default 0.5)
     * @param max_pct Maximum slice as percentage of target (default 1.5)
     */
    void set_randomize_timing(bool enable, double min_pct = 0.5, double max_pct = 1.5) {
        randomize_timing_ = enable;
        min_slice_pct_ = min_pct;
        max_slice_pct_ = max_pct;
    }

    /**
     * @brief Computes child orders for TWAP execution
     * @param data Current market data
     * @return Vector of orders (0 or 1 orders)
     *
     * TWAP logic:
     * 1. Check if it's time for the next slice
     * 2. Calculate slice size (equal division of remaining)
     * 3. Generate order at current market price
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
     * @brief Gets the slice interval duration
     * @return Duration between slices
     */
    Duration get_slice_interval() const {
        return slice_interval_;
    }

    /**
     * @brief Gets the total execution duration
     * @return Total duration
     */
    Duration get_duration() const {
        return duration_;
    }

    /**
     * @brief Calculates expected slice size (before randomization)
     * @return Base slice quantity
     */
    uint64_t get_base_slice_size() const {
        return target_quantity_ / num_slices_;
    }

    /**
     * @brief Gets elapsed time since start
     * @param current_time Current timestamp
     * @return Elapsed duration
     */
    Duration get_elapsed_time(TimePoint current_time) const {
        if (!started_) return Duration(0);
        return std::chrono::duration_cast<Duration>(current_time - start_time_);
    }

    /**
     * @brief Checks if execution should be complete by now
     * @param current_time Current timestamp
     * @return true if past duration
     */
    bool is_past_duration(TimePoint current_time) const {
        return get_elapsed_time(current_time) >= duration_;
    }

    /**
     * @brief Prints TWAP configuration
     */
    void print_config() const {
        std::cout << "\n=== TWAP Configuration ===\n";
        std::cout << "  Target quantity: " << target_quantity_ << "\n";
        std::cout << "  Duration: " << duration_.count() << " ms\n";
        std::cout << "  Num slices: " << num_slices_ << "\n";
        std::cout << "  Slice interval: " << slice_interval_.count() << " ms\n";
        std::cout << "  Base slice size: " << get_base_slice_size() << "\n";
        std::cout << "  Use limit orders: " << (use_limit_orders_ ? "Yes" : "No") << "\n";
        std::cout << "  Direction: " << (is_buy_ ? "BUY" : "SELL") << "\n";
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

        // Base slice size
        uint64_t slices_remaining = num_slices_ - current_slice_;
        uint64_t base_slice = remaining / slices_remaining;

        // Apply randomization if enabled
        if (randomize_timing_ && base_slice > 0) {
            // Random factor between min_pct and max_pct
            // Using simple deterministic "randomization" based on slice number
            double factor = min_slice_pct_ +
                (max_slice_pct_ - min_slice_pct_) *
                (static_cast<double>(current_slice_ % 7) / 6.0);

            base_slice = static_cast<uint64_t>(base_slice * factor);
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
};

/**
 * @class AggressiveTWAP
 * @brief TWAP variant that catches up if behind schedule
 *
 * If market conditions prevent fills, AggressiveTWAP will
 * increase subsequent slice sizes to stay on track.
 */
class AggressiveTWAP : public TWAPStrategy {
private:
    double max_catchup_multiplier_ = 2.0;

public:
    /**
     * @brief Constructor
     */
    AggressiveTWAP(uint64_t target_qty, int duration_minutes, bool is_buy = true)
        : TWAPStrategy(target_qty, duration_minutes, is_buy) {
        strategy_name_ = "AggressiveTWAP";
    }

    /**
     * @brief Sets maximum catchup multiplier
     * @param multiplier Max multiplier for slice size (default 2.0)
     */
    void set_max_catchup_multiplier(double multiplier) {
        max_catchup_multiplier_ = multiplier;
    }

    std::vector<Order> compute_child_orders(const MarketData& data) override {
        // Get base TWAP orders
        auto orders = TWAPStrategy::compute_child_orders(data);

        if (orders.empty()) return orders;

        // Calculate how much we should have executed by now
        auto elapsed = get_elapsed_time(data.timestamp);
        double expected_progress = static_cast<double>(elapsed.count()) / get_duration().count();
        expected_progress = std::min(1.0, expected_progress);

        uint64_t expected_executed = static_cast<uint64_t>(
            get_target_quantity() * expected_progress);

        // If behind, increase slice size
        if (get_executed_quantity() < expected_executed) {
            uint64_t shortfall = expected_executed - get_executed_quantity();
            uint64_t base_slice = get_base_slice_size();
            uint64_t additional = std::min(shortfall,
                static_cast<uint64_t>(base_slice * (max_catchup_multiplier_ - 1.0)));

            // Modify order quantity
            if (!orders.empty() && additional > 0) {
                int new_qty = orders[0].quantity + static_cast<int>(additional);
                new_qty = static_cast<int>(std::min(
                    static_cast<uint64_t>(new_qty), remaining_quantity()));

                // Create new order with updated quantity
                if (orders[0].is_market_order()) {
                    orders[0] = create_market_order(new_qty);
                } else {
                    orders[0] = create_limit_order(orders[0].price, new_qty);
                }
            }
        }

        return orders;
    }
};

#endif // TWAP_STRATEGY_HPP
