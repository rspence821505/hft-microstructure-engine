// include/fill_router.hpp
#pragma once

#include "fill.hpp"
#include "order.hpp"
#include "types.hpp"
#include <functional>
#include <unordered_map>
#include <vector>

// Enhanced fill with additional metadata
struct EnhancedFill {
  Fill base_fill;

  // Account routing
  int buy_account_id;
  int sell_account_id;

  // Fill metadata
  std::string symbol;
  uint64_t fill_id;          // Unique fill identifier
  bool is_aggressive_buy;    // Was buyer the aggressor?
  bool self_trade_prevented; // Was this a self-trade attempt?

  // Liquidity indicators
  enum class LiquidityFlag {
    MAKER,       // Added liquidity
    TAKER,       // Removed liquidity
    MAKER_MAKER, // Both sides added liquidity (rare)
  };
  LiquidityFlag liquidity_flag;

  // Fees (if applicable)
  double buyer_fee;
  double seller_fee;

  // Timestamps
  TimePoint match_time;
  TimePoint routing_time;

  EnhancedFill(const Fill &fill, int buy_acct, int sell_acct,
               const std::string &sym, uint64_t id, bool aggressive_buy)
      : base_fill(fill), buy_account_id(buy_acct), sell_account_id(sell_acct),
        symbol(sym), fill_id(id), is_aggressive_buy(aggressive_buy),
        self_trade_prevented(false),
        liquidity_flag(aggressive_buy ? LiquidityFlag::TAKER
                                      : LiquidityFlag::MAKER),
        buyer_fee(0.0), seller_fee(0.0), match_time(Clock::now()),
        routing_time(Clock::now()) {}
};

// Callback types
using FillCallback = std::function<void(const EnhancedFill &)>;
using SelfTradeCallback =
    std::function<void(int account_id, const Order &, const Order &)>;

class FillRouter {
private:
  // Fill history
  std::vector<EnhancedFill> routed_fills_;
  uint64_t next_fill_id_;

  // Callbacks
  std::vector<FillCallback> fill_callbacks_;
  std::vector<SelfTradeCallback> self_trade_callbacks_;

  // Configuration
  bool prevent_self_trades_;
  bool enable_fees_;
  double maker_fee_rate_;
  double taker_fee_rate_;

  // Statistics
  uint64_t self_trades_prevented_;
  uint64_t total_fills_routed_;

public:
  FillRouter(bool prevent_self_trades = true)
      : next_fill_id_(1), prevent_self_trades_(prevent_self_trades),
        enable_fees_(false), maker_fee_rate_(0.0), taker_fee_rate_(0.0),
        self_trades_prevented_(0), total_fills_routed_(0) {}

  // Configuration
  void set_self_trade_prevention(bool enable) { prevent_self_trades_ = enable; }
  void set_fee_schedule(double maker_rate, double taker_rate) {
    enable_fees_ = true;
    maker_fee_rate_ = maker_rate;
    taker_fee_rate_ = taker_rate;
  }

  // Callback registration
  void register_fill_callback(FillCallback callback) {
    fill_callbacks_.push_back(callback);
  }

  void register_self_trade_callback(SelfTradeCallback callback) {
    self_trade_callbacks_.push_back(callback);
  }

  // Main routing function
  bool route_fill(const Fill &fill, const Order &aggressive_order,
                  const Order &passive_order, const std::string &symbol);

  // Query fills
  const std::vector<EnhancedFill> &get_all_fills() const {
    return routed_fills_;
  }
  std::vector<EnhancedFill> get_fills_for_account(int account_id) const;
  std::vector<EnhancedFill>
  get_fills_for_symbol(const std::string &symbol) const;
  EnhancedFill *get_fill_by_id(uint64_t fill_id);

  // Statistics
  uint64_t get_self_trades_prevented() const { return self_trades_prevented_; }
  uint64_t get_total_fills() const { return total_fills_routed_; }

  void print_statistics() const;

private:
  bool is_self_trade(const Order &aggressive, const Order &passive) const;
  void calculate_fees(EnhancedFill &fill, bool aggressive_is_buyer);
  void notify_callbacks(const EnhancedFill &fill);
  void notify_self_trade(int account_id, const Order &order1,
                         const Order &order2);
};