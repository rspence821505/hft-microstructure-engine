#pragma once

#include <cmath>
#include <cstdint>
#include <string>

/**
 * @enum MarketEventType
 * @brief Enumeration of market event types
 *
 * Defines the different types of market events that can occur
 * in a trading system. Named MarketEventType to avoid conflict with
 * Matching-Engine's EventType.
 */
enum class MarketEventType {
    TRADE,          ///< A trade execution
    QUOTE,          ///< A quote update (bid/ask)
    ORDER_ADD,      ///< New order added to book
    ORDER_CANCEL,   ///< Order cancelled from book
    ORDER_MODIFY    ///< Existing order modified
};

/**
 * @struct MarketEvent
 * @brief Represents a single market event with nanosecond precision
 *
 * This structure captures all relevant information about a market event,
 * including timestamp with nanosecond precision for accurate sequencing
 * in high-frequency trading scenarios.
 */
struct MarketEvent {
    uint64_t timestamp_ns;  ///< Timestamp in nanoseconds since epoch
    std::string symbol;     ///< Trading symbol (e.g., "AAPL", "GOOGL")
    double price;           ///< Price at which the event occurred
    uint64_t volume;        ///< Volume associated with the event
    MarketEventType type;   ///< Type of market event

    /**
     * @brief Comparison operator for sorting events chronologically
     * @param other The event to compare against
     * @return true if this event occurred before the other event
     */
    bool operator<(const MarketEvent& other) const {
        return timestamp_ns < other.timestamp_ns;
    }
};

/**
 * @class SimpleImpactModel
 * @brief Basic market impact model using square-root law
 *
 * Estimates the market impact of a trade based on its size relative
 * to the average daily volume (ADV). Uses the square-root law which
 * is a common empirical model for market impact.
 *
 * The model follows: Impact = coefficient * sqrt(participation_rate)
 *
 * where participation_rate = trade_volume / ADV
 *
 * This provides a rough estimate of how much the market will move
 * against a trader executing a large order.
 */
class SimpleImpactModel {
private:
    double impact_coeff_ = 0.01;  ///< Impact coefficient (1 bps per 1% of ADV)

public:
    /**
     * @brief Default constructor with default impact coefficient
     */
    SimpleImpactModel() = default;

    /**
     * @brief Constructor with custom impact coefficient
     * @param coefficient Custom impact coefficient
     */
    explicit SimpleImpactModel(double coefficient) : impact_coeff_(coefficient) {}

    /**
     * @brief Estimates market impact in basis points
     * @param volume Trade volume being executed
     * @param adv Average Daily Volume for the security
     * @return Estimated market impact in basis points (bps)
     *
     * The impact is calculated using the square-root law:
     * Impact (bps) = impact_coeff * sqrt(volume / adv) * 10000
     *
     * Example: Trading 1% of ADV with coefficient 0.01 yields
     * 0.01 * sqrt(0.01) * 10000 = 10 bps of impact
     */
    double estimate_impact(uint64_t volume, uint64_t adv) const {
        if (adv == 0) return 0.0;  // Avoid division by zero

        double participation = static_cast<double>(volume) / adv;
        return impact_coeff_ * std::sqrt(participation) * 10000.0;  // Return in bps
    }

    /**
     * @brief Estimates total transaction cost including impact
     * @param volume Trade volume
     * @param adv Average Daily Volume
     * @param spread_bps Current bid-ask spread in basis points
     * @return Total estimated transaction cost in basis points
     *
     * Total cost = (spread / 2) + market_impact
     * The half-spread represents the immediate execution cost,
     * while market impact represents the price movement from trading.
     */
    double estimate_total_cost(uint64_t volume, uint64_t adv, double spread_bps) const {
        double half_spread = spread_bps / 2.0;
        double impact = estimate_impact(volume, adv);
        return half_spread + impact;
    }

    /**
     * @brief Sets the impact coefficient
     * @param coefficient New impact coefficient value
     */
    void set_coefficient(double coefficient) {
        impact_coeff_ = coefficient;
    }

    /**
     * @brief Gets the current impact coefficient
     * @return Current impact coefficient
     */
    double get_coefficient() const {
        return impact_coeff_;
    }
};
