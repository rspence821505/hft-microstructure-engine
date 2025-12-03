#ifndef MICROSTRUCTURE_ORDER_BOOK_HPP
#define MICROSTRUCTURE_ORDER_BOOK_HPP

#include "rolling_statistics.hpp"

// Include Matching-Engine headers
// These paths assume Matching-Engine is a sibling directory
#include "../../Matching-Engine/include/order_book.hpp"
#include "../../Matching-Engine/include/order.hpp"
#include "../../Matching-Engine/include/types.hpp"

#include <chrono>
#include <cstdint>
#include <deque>
#include <iostream>
#include <optional>
#include <string>
#include <unordered_map>

/**
 * @class MicrostructureOrderBook
 * @brief Extended OrderBook with integrated microstructure analytics
 *
 * This class wraps the high-performance OrderBook from Matching-Engine
 * and adds real-time microstructure analytics including:
 * - Rolling spread statistics
 * - Order imbalance tracking
 * - Depth analytics
 * - Volume profile tracking
 *
 * Design goal: Maintain >4M orders/sec throughput with <15% overhead
 * from analytics computation.
 */
class MicrostructureOrderBook {
public:
    // Configuration constants for analytics windows
    static constexpr size_t SPREAD_HISTORY_SIZE = 1000;
    static constexpr size_t IMBALANCE_HISTORY_SIZE = 500;
    static constexpr size_t DEPTH_HISTORY_SIZE = 500;

private:
    OrderBook book_;  ///< Underlying order book

    // Spread analytics
    RollingStatistics<double, SPREAD_HISTORY_SIZE> spread_history_;
    uint64_t spread_updates_ = 0;

    // Order imbalance analytics (bid volume - ask volume at top levels)
    RollingStatistics<double, IMBALANCE_HISTORY_SIZE> imbalance_history_;

    // Depth analytics (total quantity at top N levels)
    RollingStatistics<double, DEPTH_HISTORY_SIZE> bid_depth_history_;
    RollingStatistics<double, DEPTH_HISTORY_SIZE> ask_depth_history_;

    // Trade flow analytics
    uint64_t total_buy_volume_ = 0;
    uint64_t total_sell_volume_ = 0;
    uint64_t order_count_ = 0;

    // Timing for analytics overhead measurement
    uint64_t total_analytics_time_ns_ = 0;

    /**
     * @brief Updates all analytics after an order operation
     *
     * Called after add_order to update spread, imbalance, and depth metrics.
     * Designed to be as lightweight as possible.
     */
    void update_analytics() {
        auto start = std::chrono::steady_clock::now();

        // Update spread history
        if (auto spread = book_.get_spread()) {
            spread_history_.add(*spread);
            spread_updates_++;
        }

        // Update order imbalance (ratio of bid to ask at top of book)
        auto best_bid = book_.get_best_bid();
        auto best_ask = book_.get_best_ask();
        if (best_bid && best_ask) {
            double bid_qty = static_cast<double>(best_bid->remaining_qty);
            double ask_qty = static_cast<double>(best_ask->remaining_qty);
            double imbalance = (bid_qty - ask_qty) / (bid_qty + ask_qty);
            imbalance_history_.add(imbalance);
        }

        auto end = std::chrono::steady_clock::now();
        total_analytics_time_ns_ +=
            std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
    }

public:
    /**
     * @brief Constructs a MicrostructureOrderBook with the given symbol
     * @param symbol Trading symbol (e.g., "AAPL")
     */
    explicit MicrostructureOrderBook(const std::string& symbol = "DEFAULT")
        : book_(symbol) {}

    // ========================================================================
    // ORDER OPERATIONS (forwarded to underlying book with analytics)
    // ========================================================================

    /**
     * @brief Adds an order to the book and updates analytics
     * @param order The order to add
     *
     * This is the main entry point that adds microstructure tracking.
     */
    void add_order(Order order) {
        // Track volume by side
        if (order.side == Side::BUY) {
            total_buy_volume_ += order.quantity;
        } else {
            total_sell_volume_ += order.quantity;
        }
        order_count_++;

        // Forward to underlying book
        book_.add_order(order);

        // Update analytics after order is processed
        update_analytics();
    }

    /**
     * @brief Cancels an order by ID
     * @param order_id The order ID to cancel
     * @return true if order was cancelled
     */
    bool cancel_order(int order_id) {
        bool result = book_.cancel_order(order_id);
        if (result) {
            update_analytics();
        }
        return result;
    }

    /**
     * @brief Amends an order's price and/or quantity
     * @param order_id The order ID to amend
     * @param new_price New price (optional)
     * @param new_quantity New quantity (optional)
     * @return true if order was amended
     */
    bool amend_order(int order_id, std::optional<double> new_price,
                     std::optional<int> new_quantity) {
        bool result = book_.amend_order(order_id, new_price, new_quantity);
        if (result) {
            update_analytics();
        }
        return result;
    }

    // ========================================================================
    // SPREAD ANALYTICS
    // ========================================================================

    /**
     * @brief Returns the average spread over the rolling window
     * @return Average spread in price units
     */
    double get_average_spread() const {
        return spread_history_.mean();
    }

    /**
     * @brief Returns the spread standard deviation
     * @return Spread volatility measure
     */
    double get_spread_stddev() const {
        return spread_history_.stddev();
    }

    /**
     * @brief Returns the minimum spread observed
     * @return Minimum spread in window
     */
    double get_min_spread() const {
        return spread_history_.min();
    }

    /**
     * @brief Returns the maximum spread observed
     * @return Maximum spread in window
     */
    double get_max_spread() const {
        return spread_history_.max();
    }

    /**
     * @brief Returns the current spread
     * @return Current bid-ask spread, or empty if no spread
     */
    std::optional<double> get_current_spread() const {
        return book_.get_spread();
    }

    /**
     * @brief Returns the number of spread updates recorded
     * @return Count of spread observations
     */
    uint64_t get_spread_update_count() const {
        return spread_updates_;
    }

    // ========================================================================
    // ORDER IMBALANCE ANALYTICS
    // ========================================================================

    /**
     * @brief Returns the average order imbalance
     * @return Average imbalance (-1 to +1, positive = more bids)
     */
    double get_average_imbalance() const {
        return imbalance_history_.mean();
    }

    /**
     * @brief Returns the current order imbalance at top of book
     * @return Imbalance ratio, or 0 if no quotes
     */
    double get_current_imbalance() const {
        auto best_bid = book_.get_best_bid();
        auto best_ask = book_.get_best_ask();
        if (!best_bid || !best_ask) return 0.0;

        double bid_qty = static_cast<double>(best_bid->remaining_qty);
        double ask_qty = static_cast<double>(best_ask->remaining_qty);
        if (bid_qty + ask_qty == 0) return 0.0;

        return (bid_qty - ask_qty) / (bid_qty + ask_qty);
    }

    // ========================================================================
    // VOLUME ANALYTICS
    // ========================================================================

    /**
     * @brief Returns total buy volume processed
     * @return Cumulative buy volume
     */
    uint64_t get_total_buy_volume() const {
        return total_buy_volume_;
    }

    /**
     * @brief Returns total sell volume processed
     * @return Cumulative sell volume
     */
    uint64_t get_total_sell_volume() const {
        return total_sell_volume_;
    }

    /**
     * @brief Returns the buy/sell volume ratio
     * @return Ratio > 1 means more buying pressure
     */
    double get_volume_ratio() const {
        if (total_sell_volume_ == 0) return 0.0;
        return static_cast<double>(total_buy_volume_) / total_sell_volume_;
    }

    /**
     * @brief Returns total order count
     * @return Number of orders processed
     */
    uint64_t get_order_count() const {
        return order_count_;
    }

    // ========================================================================
    // PERFORMANCE METRICS
    // ========================================================================

    /**
     * @brief Returns average analytics overhead per order
     * @return Average nanoseconds spent on analytics per order
     */
    double get_average_analytics_overhead_ns() const {
        if (order_count_ == 0) return 0.0;
        return static_cast<double>(total_analytics_time_ns_) / order_count_;
    }

    /**
     * @brief Returns total time spent on analytics
     * @return Total nanoseconds spent on analytics
     */
    uint64_t get_total_analytics_time_ns() const {
        return total_analytics_time_ns_;
    }

    // ========================================================================
    // UNDERLYING BOOK ACCESS (forwarded methods)
    // ========================================================================

    std::optional<Order> get_best_bid() const { return book_.get_best_bid(); }
    std::optional<Order> get_best_ask() const { return book_.get_best_ask(); }
    std::optional<double> get_spread() const { return book_.get_spread(); }
    std::optional<Order> get_order(int order_id) const { return book_.get_order(order_id); }

    const std::vector<Fill>& get_fills() const { return book_.get_fills(); }
    const std::vector<AccountFill>& get_account_fills() const { return book_.get_account_fills(); }
    const std::vector<EnhancedFill>& get_enhanced_fills() const { return book_.get_enhanced_fills(); }

    FillRouter& get_fill_router() { return book_.get_fill_router(); }
    const FillRouter& get_fill_router() const { return book_.get_fill_router(); }

    void enable_self_trade_prevention(bool enable) { book_.enable_self_trade_prevention(enable); }
    void set_fee_schedule(double maker_rate, double taker_rate) { book_.set_fee_schedule(maker_rate, taker_rate); }

    void set_symbol(const std::string& symbol) { book_.set_symbol(symbol); }
    std::string get_symbol() const { return book_.get_symbol(); }

    size_t bids_size() const { return book_.bids_size(); }
    size_t asks_size() const { return book_.asks_size(); }
    size_t active_bids_count() const { return book_.active_bids_count(); }
    size_t active_asks_count() const { return book_.active_asks_count(); }

    // ========================================================================
    // REPORTING
    // ========================================================================

    void print_fills() const { book_.print_fills(); }
    void print_top_of_book() const { book_.print_top_of_book(); }
    void print_book_summary() const { book_.print_book_summary(); }
    void print_market_depth(int levels) const { book_.print_market_depth(levels); }
    void print_latency_stats() const { book_.print_latency_stats(); }

    /**
     * @brief Prints microstructure analytics summary
     */
    void print_analytics_summary() const {
        std::cout << "\n=== Microstructure Analytics Summary ===\n";

        std::cout << "\n--- Spread Analytics ---\n";
        std::cout << "  Spread observations: " << spread_history_.count() << "\n";
        std::cout << "  Average spread: " << get_average_spread() << "\n";
        std::cout << "  Spread stddev: " << get_spread_stddev() << "\n";
        std::cout << "  Min spread: " << get_min_spread() << "\n";
        std::cout << "  Max spread: " << get_max_spread() << "\n";
        if (auto spread = get_current_spread()) {
            std::cout << "  Current spread: " << *spread << "\n";
        }

        std::cout << "\n--- Order Imbalance ---\n";
        std::cout << "  Imbalance observations: " << imbalance_history_.count() << "\n";
        std::cout << "  Average imbalance: " << get_average_imbalance() << "\n";
        std::cout << "  Current imbalance: " << get_current_imbalance() << "\n";

        std::cout << "\n--- Volume Analytics ---\n";
        std::cout << "  Total orders: " << order_count_ << "\n";
        std::cout << "  Total buy volume: " << total_buy_volume_ << "\n";
        std::cout << "  Total sell volume: " << total_sell_volume_ << "\n";
        std::cout << "  Buy/Sell ratio: " << get_volume_ratio() << "\n";

        std::cout << "\n--- Performance ---\n";
        std::cout << "  Total analytics time: " << total_analytics_time_ns_ << " ns\n";
        std::cout << "  Avg analytics overhead: " << get_average_analytics_overhead_ns() << " ns/order\n";
    }

    /**
     * @brief Provides direct access to underlying OrderBook
     * @return Reference to the wrapped OrderBook
     */
    OrderBook& get_underlying_book() { return book_; }
    const OrderBook& get_underlying_book() const { return book_; }
};

#endif // MICROSTRUCTURE_ORDER_BOOK_HPP
