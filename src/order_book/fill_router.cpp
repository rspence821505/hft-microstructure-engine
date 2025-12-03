// src/fill_router.cpp
#include "fill_router.hpp"
#include <algorithm>
#include <iomanip>
#include <iostream>

bool FillRouter::route_fill(const Fill &fill, const Order &aggressive_order,
                            const Order &passive_order,
                            const std::string &symbol) {
  // 1. Check for self-trades
  if (prevent_self_trades_ && is_self_trade(aggressive_order, passive_order)) {
    self_trades_prevented_++;
    notify_self_trade(aggressive_order.account_id, aggressive_order,
                      passive_order);
    return false; // Reject the fill
  }

  // 2. Determine account routing
  int buy_account = (aggressive_order.side == Side::BUY)
                        ? aggressive_order.account_id
                        : passive_order.account_id;
  int sell_account = (aggressive_order.side == Side::SELL)
                         ? aggressive_order.account_id
                         : passive_order.account_id;

  // 3. Create enhanced fill
  bool aggressive_is_buyer = (aggressive_order.side == Side::BUY);
  EnhancedFill enhanced_fill(fill, buy_account, sell_account, symbol,
                             next_fill_id_++, aggressive_is_buyer);

  // 4. Set liquidity flag
  if (aggressive_is_buyer) {
    enhanced_fill.liquidity_flag = EnhancedFill::LiquidityFlag::TAKER;
  } else {
    enhanced_fill.liquidity_flag = EnhancedFill::LiquidityFlag::MAKER;
  }

  // 5. Calculate fees
  if (enable_fees_) {
    calculate_fees(enhanced_fill, aggressive_is_buyer);
  }

  // 6. Store fill
  routed_fills_.push_back(enhanced_fill);
  total_fills_routed_++;

  // 7. Notify callbacks
  notify_callbacks(enhanced_fill);

  return true;
}

bool FillRouter::is_self_trade(const Order &aggressive,
                               const Order &passive) const {
  return aggressive.account_id == passive.account_id;
}

void FillRouter::calculate_fees(EnhancedFill &fill, bool aggressive_is_buyer) {
  double notional = fill.base_fill.price * fill.base_fill.quantity;

  if (aggressive_is_buyer) {
    // Aggressive buyer pays taker fee
    fill.buyer_fee = notional * taker_fee_rate_;
    // Passive seller pays maker fee (or gets rebate)
    fill.seller_fee = notional * maker_fee_rate_;
  } else {
    // Aggressive seller pays taker fee
    fill.seller_fee = notional * taker_fee_rate_;
    // Passive buyer pays maker fee (or gets rebate)
    fill.buyer_fee = notional * maker_fee_rate_;
  }
}

void FillRouter::notify_callbacks(const EnhancedFill &fill) {
  for (const auto &callback : fill_callbacks_) {
    callback(fill);
  }
}

void FillRouter::notify_self_trade(int account_id, const Order &order1,
                                   const Order &order2) {
  for (const auto &callback : self_trade_callbacks_) {
    callback(account_id, order1, order2);
  }
}

std::vector<EnhancedFill>
FillRouter::get_fills_for_account(int account_id) const {
  std::vector<EnhancedFill> result;
  for (const auto &fill : routed_fills_) {
    if (fill.buy_account_id == account_id ||
        fill.sell_account_id == account_id) {
      result.push_back(fill);
    }
  }
  return result;
}

std::vector<EnhancedFill>
FillRouter::get_fills_for_symbol(const std::string &symbol) const {
  std::vector<EnhancedFill> result;
  for (const auto &fill : routed_fills_) {
    if (fill.symbol == symbol) {
      result.push_back(fill);
    }
  }
  return result;
}

EnhancedFill *FillRouter::get_fill_by_id(uint64_t fill_id) {
  auto it = std::find_if(
      routed_fills_.begin(), routed_fills_.end(),
      [fill_id](const EnhancedFill &f) { return f.fill_id == fill_id; });

  return (it != routed_fills_.end()) ? &(*it) : nullptr;
}

void FillRouter::print_statistics() const {
  std::cout << "\n=== Fill Router Statistics ===" << std::endl;
  std::cout << "Total Fills Routed:     " << total_fills_routed_ << std::endl;
  std::cout << "Self-Trades Prevented:  " << self_trades_prevented_
            << std::endl;

  if (enable_fees_) {
    double total_fees = 0.0;
    for (const auto &fill : routed_fills_) {
      total_fees += fill.buyer_fee + fill.seller_fee;
    }
    std::cout << "Total Fees Collected:   $" << std::fixed
              << std::setprecision(2) << total_fees << std::endl;
  }
}