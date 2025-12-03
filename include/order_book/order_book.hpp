#pragma once

#include "event.hpp"
#include "fill.hpp"
#include "fill_router.hpp"
#include "order.hpp"
#include "snapshot.hpp"
#include "timer.hpp"
#include <map>
#include <optional>
#include <queue>
#include <unordered_map>
#include <vector>

// NEW: Structure to track account ownership of fills
struct AccountFill {
  Fill fill;
  int buy_account_id;
  int sell_account_id;
  std::string symbol; // NEW: Also track symbol for each fill

  AccountFill(const Fill &f, int buy_acct, int sell_acct,
              const std::string &sym)
      : fill(f), buy_account_id(buy_acct), sell_account_id(sell_acct),
        symbol(sym) {}
};

class OrderBook {
private:
  std::priority_queue<Order, std::vector<Order>, BidComparator> bids_;
  std::priority_queue<Order, std::vector<Order>, AskComparator> asks_;
  std::unordered_map<int, Order> active_orders_;    // id -> order
  std::unordered_map<int, Order> cancelled_orders_; // id -> order
  std::vector<Fill> fills_;
  std::vector<AccountFill> account_fills_; // NEW: Track fills with account info
  std::vector<long long> insertion_latencies_ns_;
  std::unique_ptr<FillRouter> fill_router_;

  bool execute_trade(Order &aggressive_order, Order &passive_order);
  void update_order_state(Order &order);
  bool can_match(const Order &aggressive, const Order &passive) const;

  template <typename PriorityQueue>
  void handle_passive_order_after_match(Order &passive_order,
                                        PriorityQueue &book);

  void handle_unfilled_order(
      Order &order,
      std::priority_queue<Order, std::vector<Order>, BidComparator> *bid_book,
      std::priority_queue<Order, std::vector<Order>, AskComparator> *ask_book);

  bool check_fok_condition(const Order &order);

  void match_buy_order(Order &buy_order);
  void match_sell_order(Order &sell_order);
  bool can_fill_order(const Order &order) const;

  // Event logging
  std::vector<OrderEvent> event_log_;
  bool logging_enabled_;

  // Stop orders storage (sorted by stop price)
  std::multimap<double, Order> stop_buys_;  // Buy stops: trigger at or above
  std::multimap<double, Order> stop_sells_; // Sell stops: trigger at or below
  double last_trade_price_;

  size_t snapshot_counter_;

  // NEW: Symbol tracking for multi-instrument support
  std::string current_symbol_; // Can be set per order book instance

  struct PriceLevel {
    double price;
    int total_quantity;
    int num_orders;
  };

  std::vector<PriceLevel> get_bid_levels(int max_levels) const;
  std::vector<PriceLevel> get_ask_levels(int max_levels) const;

  // Helpers for stop triggers & post-match finalization
  double current_trigger_price_for_side(Side side) const;
  bool stop_should_trigger_now(const Order &o) const;
  void trigger_stop_order_immediately(Order &stop_order, double ref_price);
  void finalize_after_matching(Order &o);

public:
  OrderBook(const std::string &symbol = "DEFAULT");

  // ==================================================================
  // OPERATIONAL METHODS
  // ==================================================================

  void add_order(Order o);
  std::optional<Order> get_best_bid() const;
  std::optional<Order> get_best_ask() const;
  std::optional<double> get_spread() const;

  // Fill router access
  FillRouter &get_fill_router() { return *fill_router_; }
  const FillRouter &get_fill_router() const { return *fill_router_; }

  // Get fills with account information
  const std::vector<AccountFill> &get_account_fills() const;

  // Get fills for a specific account
  std::vector<AccountFill> get_fills_for_account(int account_id) const;

  // Configure fill routing
  void enable_self_trade_prevention(bool enable) {
    fill_router_->set_self_trade_prevention(enable);
  }

  void set_fee_schedule(double maker_rate, double taker_rate) {
    fill_router_->set_fee_schedule(maker_rate, taker_rate);
  }

  // Get fills with enhanced metadata
  const std::vector<EnhancedFill> &get_enhanced_fills() const {
    return fill_router_->get_all_fills();
  }

  // Keep backward compatibility
  const std::vector<Fill> &get_fills() const;

  // NEW: Set/get the symbol this order book handles
  void set_symbol(const std::string &symbol) { current_symbol_ = symbol; }
  std::string get_symbol() const { return current_symbol_; }

  // ==================================================================
  // ORDER LIFECYCLE MANAGEMENT
  // ==================================================================

  bool cancel_order(int order_id);
  bool amend_order(int order_id, std::optional<double> new_price,
                   std::optional<int> new_quantity);
  std::optional<Order> get_order(int order_id) const;
  void check_stop_triggers(double trade_price);

  // NEW: Get order's account
  std::optional<int> get_order_account(int order_id) const;

  // ==================================================================
  // SNAPSHOT AND RECOVERY
  // ==================================================================

  Snapshot create_snapshot() const;
  void restore_from_snapshot(const Snapshot &snapshot);
  void save_snapshot(const std::string &filename) const;
  void load_snapshot(const std::string &filename);

  // Incremental recovery (snapshot + events)
  void save_checkpoint(const std::string &snapshot_file,
                       const std::string &events_file) const;
  void recover_from_checkpoint(const std::string &snapshot_file,
                               const std::string &events_file);

  // ==================================================================
  // EVENT LOGGING CONTROL
  // ==================================================================

  void enable_logging() { logging_enabled_ = true; }
  void disable_logging() { logging_enabled_ = false; }
  bool is_logging() const { return logging_enabled_; }

  // Save/load events
  void save_events(const std::string &filename) const;
  size_t event_count() const { return event_log_.size(); }
  void clear_events() { event_log_.clear(); }

  // Get events for validation
  const std::vector<OrderEvent> &get_events() const { return event_log_; }

  // ==================================================================
  // STATISTICS AND DISPLAY METHODS
  // ==================================================================

  void print_fills() const;
  void print_account_fills() const; // NEW: Print fills with account info
  void print_top_of_book() const;
  void print_latency_stats() const;
  void print_match_stats() const;
  void print_fill_rate_analysis() const;
  void print_book_summary() const;
  void print_trade_timeline() const;
  void print_order_status(int order_id) const;
  void print_market_depth(int levels) const;
  void print_market_depth_compact() const;
  void print_pending_stops() const;

  size_t bids_size() const { return bids_.size(); }
  size_t asks_size() const { return asks_.size(); }

  size_t active_bids_count() const;
  size_t active_asks_count() const;

  size_t pending_stop_count() const {
    return stop_buys_.size() + stop_sells_.size();
  }
};
