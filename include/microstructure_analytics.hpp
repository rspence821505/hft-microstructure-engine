#ifndef MICROSTRUCTURE_ANALYTICS_HPP
#define MICROSTRUCTURE_ANALYTICS_HPP

#include "linear_regression.hpp"
#include "market_events.hpp"
#include "market_impact_calibration.hpp"
#include "microstructure_order_book.hpp"
#include "order_flow_tracker.hpp"
#include "rolling_statistics.hpp"

// Include Matching-Engine headers
#include "../../Matching-Engine/include/fill_router.hpp"
#include "../../Matching-Engine/include/order_book.hpp"

#include <chrono>
#include <cmath>
#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

/**
 * @struct TradeMetrics
 * @brief Aggregated trade metrics for a time period
 */
struct TradeMetrics {
    int64_t total_volume = 0;
    double total_notional = 0.0;
    int64_t trade_count = 0;
    double vwap = 0.0;
    double min_price = std::numeric_limits<double>::max();
    double max_price = std::numeric_limits<double>::lowest();
    double flow_imbalance = 0.0;
    TimePoint period_start;
    TimePoint period_end;
};

/**
 * @struct PriceImpactObservation
 * @brief Records a single price impact observation for calibration
 */
struct PriceImpactObservation {
    double participation_rate;  ///< Volume / ADV
    double price_impact_bps;    ///< Actual price move in basis points
    int64_t volume;
    double start_price;
    double end_price;
    TimePoint timestamp;
};

/**
 * @class MicrostructureAnalytics
 * @brief Central analytics hub integrating all microstructure analysis
 *
 * This class provides a unified interface for microstructure analysis:
 * - Order flow tracking and imbalance computation
 * - Price impact measurement and calibration
 * - Trade metrics aggregation
 * - Integration with OrderBook via FillRouter callbacks
 *
 * It can be connected to any OrderBook's FillRouter to automatically
 * capture and analyze all fills.
 */
class MicrostructureAnalytics {
public:
    static constexpr size_t PRICE_HISTORY_SIZE = 1000;
    static constexpr size_t IMPACT_HISTORY_SIZE = 500;

private:
    // Configuration (must be first for initialization order)
    int flow_window_seconds_ = 60;
    bool track_per_symbol_ = true;
    bool auto_calibrate_impact_ = false;

    // Order flow tracking
    OrderFlowTracker flow_tracker_;
    PerSymbolFlowTracker symbol_flow_tracker_;

    // Market impact tracking
    SimpleImpactModel impact_model_;
    MarketImpactModel calibrated_impact_model_;
    MarketImpactCalibrator impact_calibrator_;
    bool use_calibrated_model_ = false;
    std::vector<PriceImpactObservation> impact_observations_;
    std::unordered_map<std::string, uint64_t> symbol_adv_;  // Average Daily Volume
    std::vector<EnhancedFill> calibration_fills_;  // Fills for calibration

    // Price tracking per symbol
    std::unordered_map<std::string, RollingStatistics<double, PRICE_HISTORY_SIZE>> price_history_;
    std::unordered_map<std::string, double> last_price_;

    // Trade metrics
    TradeMetrics current_metrics_;
    std::vector<TradeMetrics> historical_metrics_;

    // Statistics
    uint64_t total_fills_processed_ = 0;

public:
    /**
     * @brief Default constructor
     */
    MicrostructureAnalytics()
        : flow_tracker_(flow_window_seconds_),
          symbol_flow_tracker_(flow_window_seconds_) {
        current_metrics_.period_start = Clock::now();
    }

    /**
     * @brief Constructor with custom flow window
     * @param flow_window_seconds Duration of flow tracking windows
     */
    explicit MicrostructureAnalytics(int flow_window_seconds)
        : flow_window_seconds_(flow_window_seconds),
          flow_tracker_(flow_window_seconds),
          symbol_flow_tracker_(flow_window_seconds) {
        current_metrics_.period_start = Clock::now();
    }

    // ========================================================================
    // FILL PROCESSING
    // ========================================================================

    /**
     * @brief Processes a fill and updates all analytics
     * @param fill The enhanced fill to process
     *
     * This is the main entry point for fill data. Call this from a
     * FillRouter callback to automatically capture all fills.
     */
    void process_fill(const EnhancedFill& fill) {
        total_fills_processed_++;

        // Update flow trackers
        flow_tracker_.record_fill(fill);
        if (track_per_symbol_) {
            symbol_flow_tracker_.record_fill(fill);
        }

        // Update price tracking
        update_price_tracking(fill);

        // Update trade metrics
        update_trade_metrics(fill);

        // Record impact observation if we have enough data
        if (auto_calibrate_impact_) {
            maybe_record_impact_observation(fill);
        }
    }

    /**
     * @brief Creates a callback function for FillRouter registration
     * @return FillCallback that routes to process_fill
     *
     * Usage:
     *   analytics.connect_to_order_book(order_book);
     *   // or manually:
     *   order_book.get_fill_router().register_fill_callback(
     *       analytics.create_fill_callback());
     */
    FillCallback create_fill_callback() {
        return [this](const EnhancedFill& fill) {
            this->process_fill(fill);
        };
    }

    /**
     * @brief Connects analytics to an OrderBook's FillRouter
     * @param book The order book to connect to
     */
    void connect_to_order_book(OrderBook& book) {
        book.get_fill_router().register_fill_callback(create_fill_callback());
    }

    /**
     * @brief Connects analytics to a MicrostructureOrderBook
     * @param book The microstructure order book to connect to
     */
    void connect_to_order_book(MicrostructureOrderBook& book) {
        book.get_fill_router().register_fill_callback(create_fill_callback());
    }

    // ========================================================================
    // ORDER FLOW ANALYTICS
    // ========================================================================

    /**
     * @brief Gets the current order flow imbalance
     * @return Imbalance from -1 (all sells) to +1 (all buys)
     */
    double get_flow_imbalance() const {
        return flow_tracker_.compute_current_imbalance();
    }

    /**
     * @brief Gets flow imbalance over multiple windows
     * @param num_windows Number of windows to aggregate
     * @return Aggregate imbalance
     */
    double get_flow_imbalance(size_t num_windows) const {
        return flow_tracker_.compute_imbalance(num_windows);
    }

    /**
     * @brief Gets flow imbalance for a specific symbol
     * @param symbol Trading symbol
     * @return Symbol-specific imbalance, or 0 if not tracked
     */
    double get_symbol_flow_imbalance(const std::string& symbol) const {
        return symbol_flow_tracker_.get_imbalance(symbol);
    }

    /**
     * @brief Gets the average historical imbalance
     * @return Mean imbalance over time
     */
    double get_average_imbalance() const {
        return flow_tracker_.get_average_imbalance();
    }

    /**
     * @brief Gets the buy ratio (percentage of volume that is buying)
     * @return Buy ratio from 0.0 to 1.0
     */
    double get_buy_ratio() const {
        return flow_tracker_.get_buy_ratio();
    }

    /**
     * @brief Gets the aggregate flow tracker
     * @return Reference to the flow tracker
     */
    const OrderFlowTracker& get_flow_tracker() const {
        return flow_tracker_;
    }

    /**
     * @brief Gets the per-symbol flow tracker
     * @return Reference to the per-symbol tracker
     */
    const PerSymbolFlowTracker& get_symbol_flow_tracker() const {
        return symbol_flow_tracker_;
    }

    // ========================================================================
    // MARKET IMPACT ANALYTICS
    // ========================================================================

    /**
     * @brief Sets the ADV for a symbol (used in impact calculation)
     * @param symbol Trading symbol
     * @param adv Average daily volume
     */
    void set_symbol_adv(const std::string& symbol, uint64_t adv) {
        symbol_adv_[symbol] = adv;
    }

    /**
     * @brief Gets the ADV for a symbol
     * @param symbol Trading symbol
     * @param default_adv Default value if not set
     * @return ADV for the symbol
     */
    uint64_t get_symbol_adv(const std::string& symbol,
                            uint64_t default_adv = 10000000) const {
        auto it = symbol_adv_.find(symbol);
        return it != symbol_adv_.end() ? it->second : default_adv;
    }

    /**
     * @brief Estimates market impact for a trade
     * @param volume Trade volume
     * @param symbol Trading symbol
     * @return Estimated impact in basis points
     */
    double estimate_impact(uint64_t volume, const std::string& symbol) const {
        uint64_t adv = get_symbol_adv(symbol);
        return impact_model_.estimate_impact(volume, adv);
    }

    /**
     * @brief Gets the impact model
     * @return Reference to the impact model
     */
    const SimpleImpactModel& get_impact_model() const {
        return impact_model_;
    }

    /**
     * @brief Gets mutable impact model for calibration
     * @return Reference to the impact model
     */
    SimpleImpactModel& get_impact_model() {
        return impact_model_;
    }

    /**
     * @brief Gets recorded impact observations
     * @return Vector of impact observations
     */
    const std::vector<PriceImpactObservation>& get_impact_observations() const {
        return impact_observations_;
    }

    /**
     * @brief Enables automatic impact observation recording
     * @param enable Whether to enable auto-calibration
     */
    void set_auto_calibrate(bool enable) {
        auto_calibrate_impact_ = enable;
    }

    // ========================================================================
    // MARKET IMPACT CALIBRATION
    // ========================================================================

    /**
     * @brief Records a fill for calibration (in addition to regular processing)
     * @param fill The fill to record for calibration
     */
    void record_fill_for_calibration(const EnhancedFill& fill) {
        calibration_fills_.push_back(fill);
    }

    /**
     * @brief Calibrates the impact model from accumulated fills
     * @param symbol Symbol to calibrate for (uses ADV from symbol_adv_)
     * @return true if calibration was successful
     */
    bool calibrate_impact_model(const std::string& symbol) {
        uint64_t adv = get_symbol_adv(symbol);
        return calibrate_impact_model(adv);
    }

    /**
     * @brief Calibrates the impact model from accumulated fills
     * @param adv Average daily volume to use
     * @return true if calibration was successful
     */
    bool calibrate_impact_model(uint64_t adv) {
        if (calibration_fills_.size() < 10) {
            std::cout << "Warning: Too few fills for calibration ("
                      << calibration_fills_.size() << ")\n";
            return false;
        }

        calibrated_impact_model_ = impact_calibrator_.calibrate_from_enhanced_fills(
            calibration_fills_, adv);

        const auto& params = calibrated_impact_model_.get_parameters();
        if (params.is_valid()) {
            use_calibrated_model_ = true;
            std::cout << "Impact model calibrated successfully:\n";
            std::cout << "  Permanent coeff: " << params.permanent_impact_coeff << "\n";
            std::cout << "  Temporary coeff: " << params.temporary_impact_coeff << "\n";
            std::cout << "  Exponent: " << params.impact_exponent << "\n";
            std::cout << "  R²: " << params.r_squared << "\n";
            std::cout << "  Observations: " << params.num_observations << "\n";
            return true;
        }

        std::cout << "Warning: Calibration produced invalid parameters\n";
        return false;
    }

    /**
     * @brief Calibrates from external fill data
     * @param fills Vector of fills to calibrate from
     * @param adv Average daily volume
     * @return true if calibration was successful
     */
    bool calibrate_from_fills(const std::vector<Fill>& fills, uint64_t adv) {
        calibrated_impact_model_ = ::calibrate_impact_model(fills, adv);

        const auto& params = calibrated_impact_model_.get_parameters();
        if (params.is_valid()) {
            use_calibrated_model_ = true;
            return true;
        }
        return false;
    }

    /**
     * @brief Calibrates from external enhanced fill data
     * @param fills Vector of enhanced fills to calibrate from
     * @param adv Average daily volume
     * @return true if calibration was successful
     */
    bool calibrate_from_enhanced_fills(const std::vector<EnhancedFill>& fills, uint64_t adv) {
        calibrated_impact_model_ = ::calibrate_impact_model(fills, adv);

        const auto& params = calibrated_impact_model_.get_parameters();
        if (params.is_valid()) {
            use_calibrated_model_ = true;
            return true;
        }
        return false;
    }

    /**
     * @brief Gets the calibrated impact model
     * @return Reference to the calibrated model
     */
    const MarketImpactModel& get_calibrated_impact_model() const {
        return calibrated_impact_model_;
    }

    /**
     * @brief Gets mutable access to the calibrated impact model
     * @return Reference to the calibrated model
     */
    MarketImpactModel& get_calibrated_impact_model() {
        return calibrated_impact_model_;
    }

    /**
     * @brief Estimates impact using the calibrated model if available
     * @param volume Trade volume
     * @param symbol Trading symbol
     * @return Estimated total impact in basis points
     */
    double estimate_calibrated_impact(uint64_t volume, const std::string& symbol) const {
        uint64_t adv = get_symbol_adv(symbol);
        if (use_calibrated_model_) {
            return calibrated_impact_model_.estimate_total_impact(volume, adv);
        }
        return impact_model_.estimate_impact(volume, adv);
    }

    /**
     * @brief Estimates permanent impact using calibrated model
     * @param volume Trade volume
     * @param symbol Trading symbol
     * @return Estimated permanent impact in basis points
     */
    double estimate_permanent_impact(uint64_t volume, const std::string& symbol) const {
        uint64_t adv = get_symbol_adv(symbol);
        if (use_calibrated_model_) {
            return calibrated_impact_model_.estimate_permanent_impact(volume, adv);
        }
        // Simple model doesn't distinguish - return full impact
        return impact_model_.estimate_impact(volume, adv);
    }

    /**
     * @brief Estimates temporary impact using calibrated model
     * @param volume Trade volume
     * @param symbol Trading symbol
     * @return Estimated temporary impact in basis points
     */
    double estimate_temporary_impact(uint64_t volume, const std::string& symbol) const {
        uint64_t adv = get_symbol_adv(symbol);
        if (use_calibrated_model_) {
            return calibrated_impact_model_.estimate_temporary_impact(volume, adv);
        }
        // Simple model doesn't have temporary component
        return 0.0;
    }

    /**
     * @brief Checks if calibrated model is in use
     * @return true if using calibrated model
     */
    bool is_using_calibrated_model() const {
        return use_calibrated_model_;
    }

    /**
     * @brief Enables or disables use of calibrated model
     * @param use Whether to use the calibrated model
     */
    void set_use_calibrated_model(bool use) {
        use_calibrated_model_ = use;
    }

    /**
     * @brief Gets fills accumulated for calibration
     * @return Vector of fills
     */
    const std::vector<EnhancedFill>& get_calibration_fills() const {
        return calibration_fills_;
    }

    /**
     * @brief Clears calibration fills
     */
    void clear_calibration_fills() {
        calibration_fills_.clear();
    }

    /**
     * @brief Gets the calibration data summary
     */
    void print_calibration_summary() const {
        std::cout << "\n=== Calibration Summary ===\n";
        std::cout << "Fills for calibration: " << calibration_fills_.size() << "\n";
        std::cout << "Using calibrated model: " << (use_calibrated_model_ ? "Yes" : "No") << "\n";

        if (use_calibrated_model_) {
            const auto& params = calibrated_impact_model_.get_parameters();
            std::cout << "Calibrated parameters:\n";
            std::cout << "  Permanent coeff: " << params.permanent_impact_coeff << "\n";
            std::cout << "  Temporary coeff: " << params.temporary_impact_coeff << "\n";
            std::cout << "  Exponent: " << params.impact_exponent << "\n";
            std::cout << "  R²: " << params.r_squared << "\n";
        }
    }

    // ========================================================================
    // TRADE METRICS
    // ========================================================================

    /**
     * @brief Gets current period trade metrics
     * @return Current metrics
     */
    const TradeMetrics& get_current_metrics() const {
        return current_metrics_;
    }

    /**
     * @brief Closes the current metrics period and starts a new one
     * @return The completed metrics
     */
    TradeMetrics close_metrics_period() {
        current_metrics_.period_end = Clock::now();
        current_metrics_.flow_imbalance = get_flow_imbalance();

        // Calculate VWAP
        if (current_metrics_.total_volume > 0) {
            current_metrics_.vwap =
                current_metrics_.total_notional / current_metrics_.total_volume;
        }

        TradeMetrics completed = current_metrics_;
        historical_metrics_.push_back(completed);

        // Reset for new period
        current_metrics_ = TradeMetrics{};
        current_metrics_.period_start = Clock::now();

        return completed;
    }

    /**
     * @brief Gets historical metrics periods
     * @return Vector of historical metrics
     */
    const std::vector<TradeMetrics>& get_historical_metrics() const {
        return historical_metrics_;
    }

    // ========================================================================
    // PRICE ANALYTICS
    // ========================================================================

    /**
     * @brief Gets the last known price for a symbol
     * @param symbol Trading symbol
     * @return Last price, or nullopt if unknown
     */
    std::optional<double> get_last_price(const std::string& symbol) const {
        auto it = last_price_.find(symbol);
        if (it != last_price_.end()) {
            return it->second;
        }
        return std::nullopt;
    }

    /**
     * @brief Gets average price for a symbol
     * @param symbol Trading symbol
     * @return Average price from history
     */
    double get_average_price(const std::string& symbol) const {
        auto it = price_history_.find(symbol);
        if (it != price_history_.end()) {
            return it->second.mean();
        }
        return 0.0;
    }

    /**
     * @brief Gets price volatility for a symbol
     * @param symbol Trading symbol
     * @return Standard deviation of prices
     */
    double get_price_volatility(const std::string& symbol) const {
        auto it = price_history_.find(symbol);
        if (it != price_history_.end()) {
            return it->second.stddev();
        }
        return 0.0;
    }

    // ========================================================================
    // STATISTICS & REPORTING
    // ========================================================================

    /**
     * @brief Gets total fills processed
     * @return Fill count
     */
    uint64_t get_total_fills_processed() const {
        return total_fills_processed_;
    }

    /**
     * @brief Enables or disables per-symbol tracking
     * @param enable Whether to track per symbol
     */
    void set_per_symbol_tracking(bool enable) {
        track_per_symbol_ = enable;
    }

    /**
     * @brief Sets the flow window duration
     * @param seconds Window duration in seconds
     */
    void set_flow_window(int seconds) {
        flow_window_seconds_ = seconds;
        flow_tracker_.set_window_duration(seconds);
    }

    /**
     * @brief Clears all analytics data
     */
    void clear() {
        flow_tracker_.clear();
        symbol_flow_tracker_.clear();
        impact_observations_.clear();
        calibration_fills_.clear();
        impact_calibrator_.clear();
        use_calibrated_model_ = false;
        price_history_.clear();
        last_price_.clear();
        current_metrics_ = TradeMetrics{};
        current_metrics_.period_start = Clock::now();
        historical_metrics_.clear();
        total_fills_processed_ = 0;
    }

    /**
     * @brief Prints a comprehensive analytics summary
     */
    void print_summary() const {
        std::cout << "\n========================================\n";
        std::cout << "    MICROSTRUCTURE ANALYTICS SUMMARY    \n";
        std::cout << "========================================\n";

        std::cout << "\n--- Fill Processing ---\n";
        std::cout << "  Total fills processed: " << total_fills_processed_ << "\n";

        std::cout << "\n--- Order Flow ---\n";
        std::cout << "  Current imbalance: " << get_flow_imbalance() << "\n";
        std::cout << "  Average imbalance: " << get_average_imbalance() << "\n";
        std::cout << "  Buy ratio: " << get_buy_ratio() << "\n";
        std::cout << "  Total buy volume: " << flow_tracker_.get_total_buy_volume() << "\n";
        std::cout << "  Total sell volume: " << flow_tracker_.get_total_sell_volume() << "\n";

        std::cout << "\n--- Current Period Metrics ---\n";
        std::cout << "  Trade count: " << current_metrics_.trade_count << "\n";
        std::cout << "  Total volume: " << current_metrics_.total_volume << "\n";
        std::cout << "  Total notional: " << current_metrics_.total_notional << "\n";
        if (current_metrics_.min_price != std::numeric_limits<double>::max()) {
            std::cout << "  Price range: " << current_metrics_.min_price
                      << " - " << current_metrics_.max_price << "\n";
        }

        if (track_per_symbol_) {
            std::cout << "\n--- Per-Symbol Tracking ---\n";
            std::cout << "  Symbols tracked: " << symbol_flow_tracker_.symbol_count() << "\n";
            for (const auto& symbol : symbol_flow_tracker_.get_symbols()) {
                auto tracker = symbol_flow_tracker_.get_tracker(symbol);
                if (tracker) {
                    std::cout << "  " << symbol << ": imbalance="
                              << tracker->compute_current_imbalance()
                              << ", buy_ratio=" << tracker->get_buy_ratio() << "\n";
                }
            }
        }

        if (!impact_observations_.empty()) {
            std::cout << "\n--- Impact Observations ---\n";
            std::cout << "  Observations recorded: " << impact_observations_.size() << "\n";
        }

        std::cout << "\n--- Market Impact Calibration ---\n";
        std::cout << "  Fills for calibration: " << calibration_fills_.size() << "\n";
        std::cout << "  Using calibrated model: " << (use_calibrated_model_ ? "Yes" : "No") << "\n";
        if (use_calibrated_model_) {
            const auto& params = calibrated_impact_model_.get_parameters();
            std::cout << "  Permanent coeff: " << params.permanent_impact_coeff << "\n";
            std::cout << "  Temporary coeff: " << params.temporary_impact_coeff << "\n";
            std::cout << "  Impact exponent: " << params.impact_exponent << "\n";
            std::cout << "  R²: " << params.r_squared << "\n";
        }

        std::cout << "\n========================================\n";
    }

private:
    /**
     * @brief Updates price tracking with a new fill
     * @param fill The fill to process
     */
    void update_price_tracking(const EnhancedFill& fill) {
        const std::string& symbol = fill.symbol;
        double price = fill.base_fill.price;

        // Update price history
        auto it = price_history_.find(symbol);
        if (it == price_history_.end()) {
            price_history_.emplace(symbol,
                RollingStatistics<double, PRICE_HISTORY_SIZE>{});
            it = price_history_.find(symbol);
        }
        it->second.add(price);

        // Update last price
        last_price_[symbol] = price;
    }

    /**
     * @brief Updates trade metrics with a new fill
     * @param fill The fill to process
     */
    void update_trade_metrics(const EnhancedFill& fill) {
        double price = fill.base_fill.price;
        int64_t quantity = fill.base_fill.quantity;
        double notional = price * quantity;

        current_metrics_.total_volume += quantity;
        current_metrics_.total_notional += notional;
        current_metrics_.trade_count++;
        current_metrics_.min_price = std::min(current_metrics_.min_price, price);
        current_metrics_.max_price = std::max(current_metrics_.max_price, price);
    }

    /**
     * @brief Records an impact observation if conditions are met
     * @param fill The fill to potentially record
     */
    void maybe_record_impact_observation(const EnhancedFill& fill) {
        // Only record for significant fills
        if (fill.base_fill.quantity < 100) return;

        const std::string& symbol = fill.symbol;
        uint64_t adv = get_symbol_adv(symbol);

        auto price_it = price_history_.find(symbol);
        if (price_it == price_history_.end() || price_it->second.count() < 10) {
            return;  // Not enough price history
        }

        double avg_price = price_it->second.mean();
        double current_price = fill.base_fill.price;
        double price_move_bps = ((current_price - avg_price) / avg_price) * 10000.0;

        PriceImpactObservation obs;
        obs.volume = fill.base_fill.quantity;
        obs.participation_rate = static_cast<double>(obs.volume) / adv;
        obs.price_impact_bps = std::abs(price_move_bps);
        obs.start_price = avg_price;
        obs.end_price = current_price;
        obs.timestamp = Clock::now();

        impact_observations_.push_back(obs);

        // Limit history size
        if (impact_observations_.size() > IMPACT_HISTORY_SIZE) {
            impact_observations_.erase(impact_observations_.begin());
        }
    }
};

#endif // MICROSTRUCTURE_ANALYTICS_HPP
