#pragma once

#include "fill_router.hpp"
#include "rolling_statistics.hpp"
#include "types.hpp"

#include <chrono>
#include <cstdint>
#include <deque>
#include <iostream>
#include <string>
#include <unordered_map>

/**
 * @struct FlowWindow
 * @brief Represents a time window of order flow data
 *
 * Captures buy and sell volume within a specific time window
 * for order flow imbalance analysis.
 */
struct FlowWindow {
  int64_t buy_volume = 0;     ///< Total buy volume in this window
  int64_t sell_volume = 0;    ///< Total sell volume in this window
  int64_t buy_count = 0;      ///< Number of buy fills
  int64_t sell_count = 0;     ///< Number of sell fills
  double buy_notional = 0.0;  ///< Total buy notional value
  double sell_notional = 0.0; ///< Total sell notional value
  TimePoint window_start;     ///< Start time of this window

  FlowWindow() : window_start(Clock::now()) {}
  explicit FlowWindow(TimePoint start) : window_start(start) {}
};

/**
 * @class OrderFlowTracker
 * @brief Tracks order flow and computes flow imbalance metrics
 *
 * This class maintains a rolling window of order flow data, tracking
 * buy vs sell volume, trade counts, and notional values. It computes
 * various imbalance metrics useful for market microstructure analysis.
 *
 * The tracker uses time-based windows (default 60 seconds) and maintains
 * historical windows for trend analysis.
 */
class OrderFlowTracker {
public:
  static constexpr size_t MAX_WINDOWS =
      60; // Keep 1 hour of history (60 x 1min windows)
  static constexpr size_t IMBALANCE_HISTORY_SIZE = 1000;

private:
  std::deque<FlowWindow> windows_;
  std::chrono::seconds window_duration_{60};
  size_t max_windows_ = MAX_WINDOWS;

  // Rolling statistics for imbalance
  RollingStatistics<double, IMBALANCE_HISTORY_SIZE> imbalance_history_;

  // Aggregate statistics
  int64_t total_buy_volume_ = 0;
  int64_t total_sell_volume_ = 0;
  int64_t total_buy_count_ = 0;
  int64_t total_sell_count_ = 0;
  double total_buy_notional_ = 0.0;
  double total_sell_notional_ = 0.0;

public:
  /**
   * @brief Default constructor with 60-second windows
   */
  OrderFlowTracker() = default;

  /**
   * @brief Constructor with custom window duration
   * @param window_seconds Duration of each flow window in seconds
   */
  explicit OrderFlowTracker(int window_seconds)
      : window_duration_(window_seconds) {}

  /**
   * @brief Records a fill and updates flow statistics
   * @param fill The enhanced fill to record
   *
   * The fill is classified as a buy or sell based on the aggressor side:
   * - If is_aggressive_buy is true, it's counted as buy flow
   * - Otherwise, it's counted as sell flow
   */
  void record_fill(const EnhancedFill &fill) {
    auto now = Clock::now();

    // Create new window if needed
    if (windows_.empty() ||
        now - windows_.back().window_start >= window_duration_) {
      windows_.emplace_back(FlowWindow{now});

      // Prune old windows
      while (windows_.size() > max_windows_) {
        windows_.pop_front();
      }
    }

    auto &current = windows_.back();

    // Determine if this is buy or sell flow based on aggressor
    int64_t quantity = fill.base_fill.quantity;
    double notional = fill.base_fill.price * quantity;

    if (fill.is_aggressive_buy) {
      current.buy_volume += quantity;
      current.buy_count++;
      current.buy_notional += notional;
      total_buy_volume_ += quantity;
      total_buy_count_++;
      total_buy_notional_ += notional;
    } else {
      current.sell_volume += quantity;
      current.sell_count++;
      current.sell_notional += notional;
      total_sell_volume_ += quantity;
      total_sell_count_++;
      total_sell_notional_ += notional;
    }

    // Record imbalance for history
    double imbalance = compute_current_imbalance();
    imbalance_history_.add(imbalance);
  }

  /**
   * @brief Computes the current window's order flow imbalance
   * @return Imbalance ratio from -1 (all sells) to +1 (all buys)
   */
  double compute_current_imbalance() const {
    if (windows_.empty())
      return 0.0;

    const auto &w = windows_.back();
    int64_t total = w.buy_volume + w.sell_volume;

    return total == 0
               ? 0.0
               : static_cast<double>(w.buy_volume - w.sell_volume) / total;
  }

  /**
   * @brief Computes order flow imbalance over recent N windows
   * @param num_windows Number of windows to consider
   * @return Aggregate imbalance ratio
   */
  double compute_imbalance(size_t num_windows = 1) const {
    if (windows_.empty())
      return 0.0;

    int64_t total_buy = 0;
    int64_t total_sell = 0;

    size_t count = std::min(num_windows, windows_.size());
    auto it = windows_.rbegin();

    for (size_t i = 0; i < count && it != windows_.rend(); ++i, ++it) {
      total_buy += it->buy_volume;
      total_sell += it->sell_volume;
    }

    int64_t total = total_buy + total_sell;
    return total == 0 ? 0.0
                      : static_cast<double>(total_buy - total_sell) / total;
  }

  /**
   * @brief Computes the trade count imbalance
   * @return Ratio of buy trades to total trades
   */
  double compute_trade_count_imbalance() const {
    if (windows_.empty())
      return 0.0;

    const auto &w = windows_.back();
    int64_t total = w.buy_count + w.sell_count;

    return total == 0 ? 0.0
                      : static_cast<double>(w.buy_count - w.sell_count) / total;
  }

  /**
   * @brief Computes the notional-weighted imbalance
   * @return Imbalance based on dollar volume
   */
  double compute_notional_imbalance() const {
    if (windows_.empty())
      return 0.0;

    const auto &w = windows_.back();
    double total = w.buy_notional + w.sell_notional;

    return total == 0.0 ? 0.0 : (w.buy_notional - w.sell_notional) / total;
  }

  /**
   * @brief Gets the average imbalance over the history
   * @return Mean imbalance from rolling history
   */
  double get_average_imbalance() const { return imbalance_history_.mean(); }

  /**
   * @brief Gets the standard deviation of imbalance
   * @return Imbalance volatility
   */
  double get_imbalance_volatility() const {
    return imbalance_history_.stddev();
  }

  /**
   * @brief Computes Volume-Weighted Average Price from flow data
   * @return VWAP of recent trades
   */
  double compute_vwap() const {
    if (windows_.empty())
      return 0.0;

    double total_notional = 0.0;
    int64_t total_volume = 0;

    for (const auto &w : windows_) {
      total_notional += w.buy_notional + w.sell_notional;
      total_volume += w.buy_volume + w.sell_volume;
    }

    return total_volume == 0 ? 0.0 : total_notional / total_volume;
  }

  /**
   * @brief Gets the buy ratio (percentage of volume that is buying)
   * @return Buy ratio from 0.0 to 1.0
   */
  double get_buy_ratio() const {
    if (windows_.empty())
      return 0.5;

    int64_t total_buy = 0;
    int64_t total_sell = 0;

    for (const auto &w : windows_) {
      total_buy += w.buy_volume;
      total_sell += w.sell_volume;
    }

    int64_t total = total_buy + total_sell;
    return total == 0 ? 0.5 : static_cast<double>(total_buy) / total;
  }

  /**
   * @brief Gets the current flow window
   * @return Pointer to current window, or nullptr if none
   */
  const FlowWindow *get_current_window() const {
    return windows_.empty() ? nullptr : &windows_.back();
  }

  /**
   * @brief Gets the number of tracked windows
   * @return Window count
   */
  size_t window_count() const { return windows_.size(); }

  /**
   * @brief Gets total buy volume across all time
   * @return Cumulative buy volume
   */
  int64_t get_total_buy_volume() const { return total_buy_volume_; }

  /**
   * @brief Gets total sell volume across all time
   * @return Cumulative sell volume
   */
  int64_t get_total_sell_volume() const { return total_sell_volume_; }

  /**
   * @brief Gets total buy trade count
   * @return Number of buy fills
   */
  int64_t get_total_buy_count() const { return total_buy_count_; }

  /**
   * @brief Gets total sell trade count
   * @return Number of sell fills
   */
  int64_t get_total_sell_count() const { return total_sell_count_; }

  /**
   * @brief Sets the window duration
   * @param seconds New window duration in seconds
   */
  void set_window_duration(int seconds) {
    window_duration_ = std::chrono::seconds(seconds);
  }

  /**
   * @brief Sets maximum number of windows to retain
   * @param max_windows New maximum
   */
  void set_max_windows(size_t max_windows) { max_windows_ = max_windows; }

  /**
   * @brief Clears all flow data
   */
  void clear() {
    windows_.clear();
    imbalance_history_.clear();
    total_buy_volume_ = 0;
    total_sell_volume_ = 0;
    total_buy_count_ = 0;
    total_sell_count_ = 0;
    total_buy_notional_ = 0.0;
    total_sell_notional_ = 0.0;
  }

  /**
   * @brief Prints flow statistics summary
   */
  void print_statistics() const {
    std::cout << "\n=== Order Flow Statistics ===\n";
    std::cout << "Windows tracked: " << windows_.size() << "\n";
    std::cout << "Window duration: " << window_duration_.count()
              << " seconds\n";

    if (!windows_.empty()) {
      const auto &current = windows_.back();
      std::cout << "\n--- Current Window ---\n";
      std::cout << "  Buy volume: " << current.buy_volume << "\n";
      std::cout << "  Sell volume: " << current.sell_volume << "\n";
      std::cout << "  Buy count: " << current.buy_count << "\n";
      std::cout << "  Sell count: " << current.sell_count << "\n";
      std::cout << "  Current imbalance: " << compute_current_imbalance()
                << "\n";
    }

    std::cout << "\n--- Aggregate Statistics ---\n";
    std::cout << "  Total buy volume: " << total_buy_volume_ << "\n";
    std::cout << "  Total sell volume: " << total_sell_volume_ << "\n";
    std::cout << "  Total buy count: " << total_buy_count_ << "\n";
    std::cout << "  Total sell count: " << total_sell_count_ << "\n";
    std::cout << "  Buy ratio: " << get_buy_ratio() << "\n";
    std::cout << "  Average imbalance: " << get_average_imbalance() << "\n";
    std::cout << "  Imbalance volatility: " << get_imbalance_volatility()
              << "\n";
  }
};

/**
 * @class PerSymbolFlowTracker
 * @brief Tracks order flow separately for each symbol
 *
 * Wrapper around OrderFlowTracker that maintains separate trackers
 * for each trading symbol.
 */
class PerSymbolFlowTracker {
private:
  std::unordered_map<std::string, OrderFlowTracker> trackers_;
  int window_seconds_ = 60;

public:
  /**
   * @brief Default constructor
   */
  PerSymbolFlowTracker() = default;

  /**
   * @brief Constructor with custom window duration
   * @param window_seconds Duration of each flow window
   */
  explicit PerSymbolFlowTracker(int window_seconds)
      : window_seconds_(window_seconds) {}

  /**
   * @brief Records a fill for the appropriate symbol
   * @param fill The enhanced fill to record
   */
  void record_fill(const EnhancedFill &fill) {
    // Get or create tracker for this symbol
    auto it = trackers_.find(fill.symbol);
    if (it == trackers_.end()) {
      auto [new_it, _] =
          trackers_.emplace(fill.symbol, OrderFlowTracker(window_seconds_));
      it = new_it;
    }
    it->second.record_fill(fill);
  }

  /**
   * @brief Gets the tracker for a specific symbol
   * @param symbol Trading symbol
   * @return Pointer to tracker, or nullptr if not found
   */
  const OrderFlowTracker *get_tracker(const std::string &symbol) const {
    auto it = trackers_.find(symbol);
    return it != trackers_.end() ? &it->second : nullptr;
  }

  /**
   * @brief Gets mutable tracker for a specific symbol
   * @param symbol Trading symbol
   * @return Pointer to tracker, or nullptr if not found
   */
  OrderFlowTracker *get_tracker(const std::string &symbol) {
    auto it = trackers_.find(symbol);
    return it != trackers_.end() ? &it->second : nullptr;
  }

  /**
   * @brief Gets the imbalance for a specific symbol
   * @param symbol Trading symbol
   * @return Imbalance, or 0 if symbol not found
   */
  double get_imbalance(const std::string &symbol) const {
    auto tracker = get_tracker(symbol);
    return tracker ? tracker->compute_current_imbalance() : 0.0;
  }

  /**
   * @brief Gets all tracked symbols
   * @return Vector of symbol names
   */
  std::vector<std::string> get_symbols() const {
    std::vector<std::string> symbols;
    symbols.reserve(trackers_.size());
    for (const auto &[symbol, _] : trackers_) {
      symbols.push_back(symbol);
    }
    return symbols;
  }

  /**
   * @brief Gets the number of symbols being tracked
   * @return Symbol count
   */
  size_t symbol_count() const { return trackers_.size(); }

  /**
   * @brief Clears all trackers
   */
  void clear() { trackers_.clear(); }

  /**
   * @brief Prints statistics for all symbols
   */
  void print_all_statistics() const {
    for (const auto &[symbol, tracker] : trackers_) {
      std::cout << "\n=== Flow Statistics for " << symbol << " ===";
      tracker.print_statistics();
    }
  }
};
