#include "order_book.hpp"

#include <iostream>
#include <stdexcept>

// ============================================================================
// CONSTRUCTOR
// ============================================================================

OrderBook::OrderBook(const std::string &symbol)
    : fill_router_(std::make_unique<FillRouter>(true)),
      logging_enabled_(false), last_trade_price_(0), snapshot_counter_(0),
      current_symbol_(symbol) {
  fill_router_->set_self_trade_prevention(true);
}

// ============================================================================
//  HELPERS (for stop triggers & post-match finalization)
// ============================================================================

// ============================================================================
//  CORE ORDER OPERATIONS
// ============================================================================

void OrderBook::add_order(Order o) {
  Timer timer;
  timer.start();

  Order order = o;

  // Handle stop orders (now with trigger-on-placement)
  if (order.is_stop && !order.stop_triggered) {
    // If conditions already meet the stop, trigger immediately (do NOT enqueue)
    if (stop_should_trigger_now(order)) {
      const double ref = current_trigger_price_for_side(order.side);

      // Track as ACTIVE then route
      order.state = OrderState::ACTIVE;
      active_orders_.insert_or_assign(order.id, order);

      trigger_stop_order_immediately(order, ref);

      timer.stop();
      insertion_latencies_ns_.push_back(timer.elapsed_nanoseconds());
      return;
    }

    // Otherwise, enqueue as pending stop (original behavior)
    order.state = OrderState::PENDING;
    active_orders_.insert_or_assign(order.id, order);

    if (order.side == Side::BUY) {
      stop_buys_.insert({order.stop_price, order});
      std::cout << "Stop-buy order " << order.id << " placed at &"
                << order.stop_price << std::endl;
    } else if (order.side == Side::SELL) {
      stop_sells_.insert({order.stop_price, order});
      std::cout << "Stop-sell order " << order.id << " placed at &"
                << order.stop_price << std::endl;
    } else {
      throw std::runtime_error("Invalid order side");
    }

    timer.stop();
    insertion_latencies_ns_.push_back(timer.elapsed_nanoseconds());
    return; // Don't match yet
  }

  // Regular orders (or triggered stops) proceed normally
  order.state = OrderState::ACTIVE;
  active_orders_.insert_or_assign(order.id, order);

  if (logging_enabled_) {
    // Handle market orders with infinite price properly
    double log_price = order.is_market_order() ? 0.0 : order.price;

    if (order.is_iceberg()) {
      event_log_.emplace_back(order.timestamp, order.id, order.side, order.type,
                              order.tif, log_price, order.quantity,
                              order.peak_size, order.account_id);
    } else {
      event_log_.emplace_back(order.timestamp, order.id, order.side, order.type,
                              order.tif, log_price, order.quantity, 0,
                              order.account_id);
    }
  }

  if (order.side == Side::BUY) {
    match_buy_order(order);
  } else if (order.side == Side::SELL) {
    match_sell_order(order);
  } else {
    throw std::runtime_error("Invalid order side");
  }

  // IMPORTANT: finalize states (prevents overwriting IOC remainder =>
  // CANCELLED)
  finalize_after_matching(order);

  timer.stop();
  insertion_latencies_ns_.push_back(timer.elapsed_nanoseconds());
}

// ============================================================================
// ORDER LIFECYCLE MANAGEMENT
// ============================================================================

bool OrderBook::cancel_order(int order_id) {
  Timer timer;
  timer.start();

  auto it = active_orders_.find(order_id);
  if (it == active_orders_.end()) {
    if (logging_enabled_) {
      event_log_.emplace_back(Clock::now(), EventType::CANCEL_ORDER, order_id);
    }
    std::cout << "Order " << order_id << " not found or already processed."
              << '\n';
    return false;
  }
  Order &order = it->second;

  if (logging_enabled_) {
    event_log_.emplace_back(Clock::now(), EventType::CANCEL_ORDER, order_id,
                            order.account_id);
  }

  if (order.is_filled()) {
    std::cout << "Order " << order_id << " is already filled." << '\n';
    return false;
  }

  // Mark as canceled
  order.state = OrderState::CANCELLED;

  // Move to cancelled Orders
  cancelled_orders_.insert({order_id, order});
  active_orders_.erase(it);

  // Priority queues can't efficiently remove mid-queue
  // So leave in queue, but skip during matching
  // The matching logic should check if order is still active

  timer.stop();

  std::cout << "Cancelled order " << order_id
            << " (latency: " << timer.elapsed_nanoseconds() << " ns)" << '\n';

  return true;
}

bool OrderBook::amend_order(int order_id, std::optional<double> new_price,
                            std::optional<int> new_quantity) {
  Timer timer;
  timer.start();

  // Check if order exists
  auto it = active_orders_.find(order_id);
  if (it == active_orders_.end()) {
    if (logging_enabled_) {
      event_log_.emplace_back(Clock::now(), order_id, new_price, new_quantity);
    }
    std::cout << "Order " << order_id << " not found." << '\n';
    return false;
  }
  Order &order = it->second;

  if (logging_enabled_) {
    event_log_.emplace_back(Clock::now(), order_id, new_price, new_quantity,
                            order.account_id);
  }

  // Can't amend filled orders
  if (order.is_filled()) {
    std::cout << "Order " << order_id << " is already filled." << '\n';
    return false;
  }

  // Extract order details
  Side side = order.side;
  double price = new_price.value_or(order.price);
  int quantity = new_quantity.value_or(order.remaining_qty);

  // Cancel old order
  cancel_order(order_id);

  // Create new order with same ID
  Order amended_order(order_id, order.account_id, side, price, quantity,
                      order.tif);

  // CRITICAL: Use add_order() to trigger matching logic
  add_order(amended_order);

  timer.stop();

  std::cout << "✓ Amended order " << order_id
            << " (latency: " << timer.elapsed_nanoseconds() << " ns)" << '\n';

  return true;
}

std::optional<Order> OrderBook::get_order(int order_id) const {
  auto it = active_orders_.find(order_id);
  if (it != active_orders_.end()) {
    return it->second;
  }

  // Check cancelled orders
  auto cancelled_it = cancelled_orders_.find(order_id);
  if (cancelled_it != cancelled_orders_.end()) {
    return cancelled_it->second;
  }

  return std::nullopt;
}

size_t OrderBook::active_bids_count() const {
  auto bids_copy = bids_;
  size_t count = 0;

  while (!bids_copy.empty()) {
    Order order = bids_copy.top();
    bids_copy.pop();

    // Only count if still active
    auto it = active_orders_.find(order.id);
    if (it != active_orders_.end() &&
        it->second.state != OrderState::CANCELLED &&
        it->second.state != OrderState::FILLED) {
      count++;
    }
  }

  return count;
}

size_t OrderBook::active_asks_count() const {
  auto asks_copy = asks_;
  size_t count = 0;

  while (!asks_copy.empty()) {
    Order order = asks_copy.top();
    asks_copy.pop();

    auto it = active_orders_.find(order.id);
    if (it != active_orders_.end() &&
        it->second.state != OrderState::CANCELLED &&
        it->second.state != OrderState::FILLED) {
      count++;
    }
  }

  return count;
}

// Get fills with account information
const std::vector<AccountFill> &OrderBook::get_account_fills() const {
  return account_fills_;
}

// Get fills for a specific account
std::vector<AccountFill>
OrderBook::get_fills_for_account(int account_id) const {
  std::vector<AccountFill> result;

  for (const auto &af : account_fills_) {
    if (af.buy_account_id == account_id || af.sell_account_id == account_id) {
      result.push_back(af);
    }
  }

  return result;
}

// Get order's account
std::optional<int> OrderBook::get_order_account(int order_id) const {
  auto it = active_orders_.find(order_id);
  if (it != active_orders_.end()) {
    return it->second.account_id;
  }

  auto cancelled_it = cancelled_orders_.find(order_id);
  if (cancelled_it != cancelled_orders_.end()) {
    return cancelled_it->second.account_id;
  }

  return std::nullopt;
}

std::optional<Order> OrderBook::get_best_bid() const {
  if (bids_.empty()) {
    return std::nullopt;
  }
  return bids_.top();
}

std::optional<Order> OrderBook::get_best_ask() const {
  if (asks_.empty()) {
    return std::nullopt;
  }
  return asks_.top();
}

std::optional<double> OrderBook::get_spread() const {
  if (bids_.empty() || asks_.empty()) {
    return std::nullopt;
  }
  return asks_.top().price - bids_.top().price;
}

const std::vector<Fill> &OrderBook::get_fills() const { return fills_; }
