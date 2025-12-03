#include "order_book.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <limits>

double OrderBook::current_trigger_price_for_side(Side side) const {
  // Prefer last trade if known
  if (last_trade_price_ > 0.0)
    return last_trade_price_;

  // No trades yet — consider BOTH sides of the book so tests can
  // trigger-on-placement in one-sided markets.
  auto bb = get_best_bid();
  auto ba = get_best_ask();

  if (side == Side::SELL) {
    // A stop-sell should trigger when "the price" is at/through the stop.
    // With no last trade, use the "most conservative" sell reference: the
    // lowest available price signal. If both exist use min(bid, ask); if only
    // one exists, use it.
    if (bb && ba)
      return std::min(bb->price, ba->price);
    if (bb)
      return bb->price;
    if (ba)
      return ba->price;
    // Truly empty market: don't trigger.
    return std::numeric_limits<double>::quiet_NaN();
  } else {
    // BUY stop: trigger when price rises to/through the stop.
    // With no last trade, use the highest available price signal.
    if (bb && ba)
      return std::max(bb->price, ba->price);
    if (ba)
      return ba->price;
    if (bb)
      return bb->price;
    return std::numeric_limits<double>::quiet_NaN();
  }
}

bool OrderBook::stop_should_trigger_now(const Order &o) const {
  if (!o.is_stop || o.stop_triggered)
    return false;

  const double ref = current_trigger_price_for_side(o.side);
  if (std::isnan(ref))
    return false;

  if (o.side == Side::SELL) {
    return ref <= o.stop_price;
  } else {
    return ref >= o.stop_price;
  }
}

void OrderBook::trigger_stop_order_immediately(Order &stop_order,
                                               double ref_price) {
  std::cout << "Stop-" << (stop_order.side == Side::BUY ? "buy" : "sell")
            << " order " << stop_order.id << " triggered at $" << std::fixed
            << std::setprecision(2) << ref_price << std::endl;

  // Mark as triggered & convert type explicitly
  stop_order.stop_triggered = true;
  stop_order.is_stop = false;

  if (stop_order.stop_becomes == OrderType::MARKET) {
    // Stop-Market -> becomes Market (price ignored by matching)
    stop_order.type = OrderType::MARKET;
  } else {
    // Stop-Limit -> becomes Limit at the order's existing limit price.
    // (Your schema uses `price` as the post-trigger limit; no separate field.)
    stop_order.type = OrderType::LIMIT;
    // keep stop_order.price as-is
  }

  stop_order.state = OrderState::ACTIVE;
  active_orders_.insert_or_assign(stop_order.id, stop_order);

  if (stop_order.side == Side::BUY) {
    match_buy_order(stop_order);
  } else {
    match_sell_order(stop_order);
  }
}

void OrderBook::check_stop_triggers(double trade_price) {
  last_trade_price_ = trade_price;

  std::vector<Order> triggered_orders;

  // Check buy stops (trigger when price >= stop_price)
  // Buy stops trigger on price rise
  {
    auto it = stop_buys_.begin();
    while (it != stop_buys_.end()) {
      if (trade_price >= it->first) {
        triggered_orders.push_back(it->second);
        it = stop_buys_.erase(it);
      } else {
        ++it;
      }
    }
  }

  // Check sell stops (trigger when price <= stop_price)
  // Sell stops trigger on price drop
  {
    auto it = stop_sells_.begin();
    while (it != stop_sells_.end()) {
      if (trade_price <= it->first) {
        triggered_orders.push_back(it->second);
        it = stop_sells_.erase(it);
      } else {
        ++it;
      }
    }
  }

  // Process all triggered stops
  for (auto &stop_order : triggered_orders) {
    // Reuse the new helper for consistent routing/logs
    trigger_stop_order_immediately(stop_order, trade_price);
  }
}
