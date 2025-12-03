#ifndef EXECUTION_ALGORITHM_HPP
#define EXECUTION_ALGORITHM_HPP

#include "market_impact_calibration.hpp"

// Include Matching-Engine headers
#include "../../Matching-Engine/include/fill.hpp"
#include "../../Matching-Engine/include/order.hpp"
#include "../../Matching-Engine/include/types.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <string>
#include <vector>

/**
 * @struct MarketData
 * @brief Represents current market state for execution decisions
 *
 * Contains the current market snapshot including price, spread,
 * and volume information needed by execution algorithms.
 */
struct MarketData {
    double price;           ///< Current mid price or last trade price
    double bid_price;       ///< Best bid price
    double ask_price;       ///< Best ask price
    double spread;          ///< Current spread in price terms
    uint64_t bid_volume;    ///< Volume at best bid
    uint64_t ask_volume;    ///< Volume at best ask
    uint64_t total_volume;  ///< Total traded volume
    TimePoint timestamp;    ///< Time of this market snapshot
    std::string symbol;     ///< Trading symbol

    MarketData()
        : price(0.0), bid_price(0.0), ask_price(0.0), spread(0.0),
          bid_volume(0), ask_volume(0), total_volume(0),
          timestamp(Clock::now()) {}

    /**
     * @brief Creates MarketData from bid/ask prices
     */
    static MarketData from_quotes(double bid, double ask,
                                   uint64_t bid_vol = 0, uint64_t ask_vol = 0) {
        MarketData data;
        data.bid_price = bid;
        data.ask_price = ask;
        data.price = (bid + ask) / 2.0;  // Mid price
        data.spread = ask - bid;
        data.bid_volume = bid_vol;
        data.ask_volume = ask_vol;
        data.timestamp = Clock::now();
        return data;
    }

    /**
     * @brief Creates MarketData from a single price (e.g., last trade)
     */
    static MarketData from_price(double px, uint64_t volume = 0) {
        MarketData data;
        data.price = px;
        data.bid_price = px;
        data.ask_price = px;
        data.spread = 0.0;
        data.total_volume = volume;
        data.timestamp = Clock::now();
        return data;
    }
};

/**
 * @struct ExecutionReport
 * @brief Report generated after execution algorithm completes
 *
 * Contains performance metrics including average price, slippage,
 * implementation shortfall, and execution statistics.
 */
struct ExecutionReport {
    std::string algorithm_name;          ///< Name of the algorithm
    double arrival_price = 0.0;          ///< Price when execution started
    double avg_execution_price = 0.0;    ///< Volume-weighted average price
    double implementation_shortfall_bps = 0.0;  ///< Cost vs arrival in bps
    double slippage_bps = 0.0;           ///< Slippage from target
    size_t num_child_orders = 0;         ///< Number of orders generated
    size_t num_fills = 0;                ///< Number of fills received
    uint64_t total_quantity = 0;         ///< Total quantity executed
    uint64_t target_quantity = 0;        ///< Target quantity
    double fill_rate = 0.0;              ///< Percentage filled
    std::chrono::milliseconds execution_time{0};  ///< Total execution time
    TimePoint start_time;                ///< When execution started
    TimePoint end_time;                  ///< When execution completed

    /**
     * @brief Prints execution report summary
     */
    void print() const {
        std::cout << "\n=== Execution Report: " << algorithm_name << " ===\n";
        std::cout << "  Target quantity: " << target_quantity << "\n";
        std::cout << "  Executed quantity: " << total_quantity << "\n";
        std::cout << "  Fill rate: " << (fill_rate * 100.0) << "%\n";
        std::cout << "  Arrival price: " << arrival_price << "\n";
        std::cout << "  Avg execution price: " << avg_execution_price << "\n";
        std::cout << "  Implementation shortfall: " << implementation_shortfall_bps << " bps\n";
        std::cout << "  Num child orders: " << num_child_orders << "\n";
        std::cout << "  Num fills: " << num_fills << "\n";
        std::cout << "  Execution time: " << execution_time.count() << " ms\n";
    }
};

/**
 * @class ExecutionAlgorithm
 * @brief Base class for execution algorithms
 *
 * Provides the framework for implementing execution strategies like
 * TWAP, VWAP, and Almgren-Chriss. Subclasses implement compute_child_orders()
 * to define their specific slicing logic.
 *
 * Usage:
 *   1. Create algorithm instance with target quantity
 *   2. Feed market data via on_market_data()
 *   3. Execute returned orders and report fills via on_fill()
 *   4. Generate report when complete via generate_report()
 */
class ExecutionAlgorithm {
protected:
    std::string strategy_name_ = "ExecutionAlgorithm";
    int account_id_ = 1;
    int next_order_id_ = 1;

    // Execution state
    uint64_t target_quantity_ = 0;
    uint64_t executed_quantity_ = 0;
    double arrival_price_ = 0.0;
    bool is_buy_ = true;  // Direction of execution

    // Fill tracking
    std::vector<Fill> my_fills_;

    // Timing
    TimePoint start_time_;
    bool started_ = false;

    // Order tracking
    size_t orders_generated_ = 0;

public:
    /**
     * @brief Default constructor
     */
    ExecutionAlgorithm() = default;

    /**
     * @brief Constructor with target quantity
     * @param target_qty Target quantity to execute
     * @param is_buy true for buy execution, false for sell
     */
    explicit ExecutionAlgorithm(uint64_t target_qty, bool is_buy = true)
        : target_quantity_(target_qty), is_buy_(is_buy) {}

    /**
     * @brief Virtual destructor
     */
    virtual ~ExecutionAlgorithm() = default;

    /**
     * @brief Sets the account ID for generated orders
     * @param account_id Account identifier
     */
    void set_account_id(int account_id) {
        account_id_ = account_id;
    }

    /**
     * @brief Gets the next order ID and increments counter
     * @return Next order ID
     */
    int get_next_order_id() {
        return next_order_id_++;
    }

    /**
     * @brief Sets the starting order ID
     * @param id Starting order ID
     */
    void set_starting_order_id(int id) {
        next_order_id_ = id;
    }

    /**
     * @brief Processes market data and generates child orders
     * @param data Current market state
     * @return Vector of orders to execute (may be empty)
     *
     * This method records the arrival price on first call, checks
     * if execution is complete, and delegates to compute_child_orders().
     */
    virtual std::vector<Order> on_market_data(const MarketData& data) {
        // Record start time and arrival price
        if (!started_) {
            started_ = true;
            start_time_ = data.timestamp;
            arrival_price_ = data.price;
        }

        // Check if complete
        if (is_complete()) {
            return {};
        }

        // Delegate to subclass
        return compute_child_orders(data);
    }

    /**
     * @brief Computes child orders based on market data
     * @param data Current market state
     * @return Vector of orders to execute
     *
     * Subclasses must implement this method to define their
     * specific slicing and timing logic.
     */
    virtual std::vector<Order> compute_child_orders(const MarketData& data) = 0;

    /**
     * @brief Processes a fill notification
     * @param fill The fill to record
     *
     * Updates executed quantity and stores fill for reporting.
     */
    virtual void on_fill(const Fill& fill) {
        executed_quantity_ += fill.quantity;
        my_fills_.push_back(fill);
    }

    /**
     * @brief Checks if execution is complete
     * @return true if target quantity has been reached
     */
    bool is_complete() const {
        return executed_quantity_ >= target_quantity_;
    }

    /**
     * @brief Gets remaining quantity to execute
     * @return Remaining quantity
     */
    uint64_t remaining_quantity() const {
        return target_quantity_ > executed_quantity_
            ? target_quantity_ - executed_quantity_
            : 0;
    }

    /**
     * @brief Gets the execution progress as a fraction
     * @return Progress from 0.0 to 1.0
     */
    double progress() const {
        return target_quantity_ > 0
            ? static_cast<double>(executed_quantity_) / target_quantity_
            : 1.0;
    }

    /**
     * @brief Gets the algorithm name
     * @return Strategy name
     */
    const std::string& name() const {
        return strategy_name_;
    }

    /**
     * @brief Gets the arrival price
     * @return Price when execution started
     */
    double get_arrival_price() const {
        return arrival_price_;
    }

    /**
     * @brief Gets executed quantity
     * @return Quantity executed so far
     */
    uint64_t get_executed_quantity() const {
        return executed_quantity_;
    }

    /**
     * @brief Gets target quantity
     * @return Target quantity
     */
    uint64_t get_target_quantity() const {
        return target_quantity_;
    }

    /**
     * @brief Gets the list of fills
     * @return Vector of fills
     */
    const std::vector<Fill>& get_fills() const {
        return my_fills_;
    }

    /**
     * @brief Calculates volume-weighted average execution price
     * @return VWAP of fills, or 0 if no fills
     */
    double calculate_vwap() const {
        if (my_fills_.empty() || executed_quantity_ == 0) {
            return 0.0;
        }

        double total_cost = 0.0;
        for (const auto& fill : my_fills_) {
            total_cost += fill.price * fill.quantity;
        }
        return total_cost / static_cast<double>(executed_quantity_);
    }

    /**
     * @brief Generates execution report
     * @return ExecutionReport with performance metrics
     */
    ExecutionReport generate_report() const {
        ExecutionReport report;

        report.algorithm_name = strategy_name_;
        report.arrival_price = arrival_price_;
        report.target_quantity = target_quantity_;
        report.total_quantity = executed_quantity_;
        report.num_fills = my_fills_.size();
        report.num_child_orders = orders_generated_;

        // Calculate VWAP
        report.avg_execution_price = calculate_vwap();

        // Fill rate
        report.fill_rate = target_quantity_ > 0
            ? static_cast<double>(executed_quantity_) / target_quantity_
            : 0.0;

        // Implementation shortfall: (avg_price - arrival_price) / arrival_price * 10000
        // For buys: positive means we paid more (bad)
        // For sells: negative means we received less (bad)
        if (arrival_price_ > 0.0 && report.avg_execution_price > 0.0) {
            double price_diff = report.avg_execution_price - arrival_price_;
            report.implementation_shortfall_bps = (price_diff / arrival_price_) * 10000.0;

            // For sells, flip the sign
            if (!is_buy_) {
                report.implementation_shortfall_bps = -report.implementation_shortfall_bps;
            }
        }

        // Execution time
        if (!my_fills_.empty()) {
            report.start_time = start_time_;
            report.end_time = my_fills_.back().timestamp;
            report.execution_time = std::chrono::duration_cast<std::chrono::milliseconds>(
                report.end_time - report.start_time);
        }

        return report;
    }

    /**
     * @brief Resets algorithm state for reuse
     */
    virtual void reset() {
        executed_quantity_ = 0;
        arrival_price_ = 0.0;
        started_ = false;
        my_fills_.clear();
        orders_generated_ = 0;
    }

    /**
     * @brief Resets with new target quantity
     * @param target_qty New target quantity
     * @param is_buy Direction (buy or sell)
     */
    void reset(uint64_t target_qty, bool is_buy = true) {
        reset();
        target_quantity_ = target_qty;
        is_buy_ = is_buy;
    }

protected:
    /**
     * @brief Creates a limit order
     * @param price Limit price
     * @param quantity Order quantity
     * @return Order object
     */
    Order create_limit_order(double price, int quantity) {
        orders_generated_++;
        Side side = is_buy_ ? Side::BUY : Side::SELL;
        return Order(get_next_order_id(), account_id_, side, price, quantity);
    }

    /**
     * @brief Creates a market order
     * @param quantity Order quantity
     * @return Order object
     */
    Order create_market_order(int quantity) {
        orders_generated_++;
        Side side = is_buy_ ? Side::BUY : Side::SELL;
        return Order(get_next_order_id(), account_id_, side, OrderType::MARKET,
                     quantity, TimeInForce::IOC);
    }

    /**
     * @brief Clamps quantity to remaining amount
     * @param requested Requested quantity
     * @return Min of requested and remaining
     */
    uint64_t clamp_to_remaining(uint64_t requested) const {
        return std::min(requested, remaining_quantity());
    }
};

#endif // EXECUTION_ALGORITHM_HPP
