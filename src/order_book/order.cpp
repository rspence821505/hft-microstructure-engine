#include "order.hpp"
#include <iomanip>
#include <iostream>
#include <stdexcept>

// Constructor for LIMIT orders
Order::Order(int id_, int account_id_, Side side_, double price_, int qty_,
             TimeInForce tif_)
    : id(id_), account_id(account_id_), side(side_), type(OrderType::LIMIT),
      tif(tif_), price(price_), quantity(qty_), remaining_qty(qty_),
      display_qty(qty_), hidden_qty(0), peak_size(0), timestamp(Clock::now()),
      state(OrderState::PENDING), is_stop(false), stop_price(0),
      stop_triggered(false), stop_becomes(OrderType::LIMIT) {}

// Constructor for MARKET orders
Order::Order(int id_, int account_id_, Side side_, OrderType type_, int qty_,
             TimeInForce tif_)
    : id(id_), account_id(account_id_), side(side_), type(type_), tif(tif_),
      price(side == Side::BUY ? std::numeric_limits<double>::infinity() : 0.0),
      quantity(qty_), remaining_qty(qty_), display_qty(qty_), hidden_qty(0),
      peak_size(0), timestamp(Clock::now()), state(OrderState::PENDING),
      is_stop(false), stop_price(0), stop_triggered(false),
      stop_becomes(OrderType::MARKET) {
  if (type_ != OrderType::MARKET) {
    throw std::runtime_error("Use the other constructor for limit orders");
  }
  if (tif == TimeInForce::GTC) {
    tif = TimeInForce::IOC;
  }
}

// Constructor for ICEBERG orders
Order::Order(int id_, int account_id_, Side side_, double price_, int total_qty,
             int peak_size_, TimeInForce tif_)
    : id(id_), account_id(account_id_), side(side_), type(OrderType::LIMIT),
      tif(tif_), price(price_), quantity(total_qty), remaining_qty(total_qty),
      display_qty(std::min(peak_size_, total_qty)),
      hidden_qty(std::max(0, total_qty - peak_size_)), peak_size(peak_size_),
      timestamp(Clock::now()), state(OrderState::PENDING), is_stop(false),
      stop_price(0), stop_triggered(false), stop_becomes(OrderType::LIMIT) {

  if (peak_size_ <= 0) {
    throw std::runtime_error("Peak size must be positive");
  }
  if (peak_size_ > total_qty) {
    display_qty = total_qty;
    hidden_qty = 0;
    peak_size = 0;
  }
}

// Constructor for STOP-MARKET orders
Order::Order(int id_, int account_id_, Side side_, double stop_price_, int qty_,
             bool is_stop_market, TimeInForce tif_)
    : id(id_), account_id(account_id_), side(side_), type(OrderType::MARKET),
      tif(tif_),
      price(side == Side::BUY ? std::numeric_limits<double>::infinity() : 0.0),
      quantity(qty_), remaining_qty(qty_), display_qty(qty_), hidden_qty(0),
      peak_size(0), timestamp(Clock::now()), state(OrderState::PENDING),
      is_stop(true), stop_price(stop_price_), stop_triggered(false),
      stop_becomes(OrderType::MARKET) {

  if (!is_stop_market) {
    throw std::runtime_error(
        "Use this constructor only for stop-market orders");
  }
}

// Constructor for STOP-LIMIT orders
Order::Order(int id_, int account_id_, Side side_, double stop_price_,
             double limit_price_, int qty_, TimeInForce tif_)
    : id(id_), account_id(account_id_), side(side_), type(OrderType::LIMIT),
      tif(tif_), price(limit_price_), quantity(qty_), remaining_qty(qty_),
      display_qty(qty_), hidden_qty(0), peak_size(0), timestamp(Clock::now()),
      state(OrderState::PENDING), is_stop(true), stop_price(stop_price_),
      stop_triggered(false), stop_becomes(OrderType::LIMIT) {}

// Trigger the stop order
void Order::trigger_stop() {
  if (!is_stop || stop_triggered) {
    return; // Already triggered or not a stop
  }

  stop_triggered = true;
  state = OrderState::ACTIVE;
  timestamp = Clock::now(); // New timestamp when triggered
  type = stop_becomes;      // Becomes MARKET or LIMIT

  std::cout << "Stop order " << id << " (Account " << account_id
            << ") triggered at $" << std::fixed << std::setprecision(2)
            << stop_price << " → "
            << (type == OrderType::MARKET ? "MARKET" : "LIMIT") << " "
            << side_to_string() << std::endl;
}

bool Order::is_filled() const {
  return remaining_qty == 0 || state == OrderState::FILLED;
}

bool Order::is_active() const {
  return state == OrderState::ACTIVE || state == OrderState::PARTIALLY_FILLED;
}

bool Order::is_market_order() const { return type == OrderType::MARKET; }

bool Order::is_iceberg() const { return peak_size > 0 && hidden_qty > 0; }

bool Order::can_rest_in_book() const {
  // Only GTC and DAY orders can rest in the book
  // IOC and FOK must execute immediately or cancel
  return tif == TimeInForce::GTC || tif == TimeInForce::DAY;
}

bool Order::needs_refresh() const {
  // Needs refresh if display is exhausted but we have hidden quantity
  return display_qty == 0 && hidden_qty > 0;
}

void Order::refresh_display() {
  if (hidden_qty > 0) {
    int reveal = std::min(peak_size, hidden_qty);
    display_qty = reveal;
    hidden_qty -= reveal;
    timestamp = Clock::now(); // Loses time priority!

    std::cout << "Iceberg order " << id << " (Account " << account_id
              << ") refreshed: showing " << display_qty << " more shares ("
              << hidden_qty << " still hidden)" << std::endl;
  }
}

std::string Order::side_to_string() const {
  return side == Side::BUY ? "BUY" : "SELL";
}

std::string Order::type_to_string() const {
  return type == OrderType::LIMIT ? "LIMIT" : "MARKET";
}

std::string Order::tif_to_string() const {
  switch (tif) {
  case TimeInForce::GTC:
    return "GTC";
  case TimeInForce::IOC:
    return "IOC";
  case TimeInForce::FOK:
    return "FOK";
  case TimeInForce::DAY:
    return "DAY";
  default:
    return "UNKNOWN";
  }
}

std::string Order::state_to_string() const {
  switch (state) {
  case OrderState::PENDING:
    return "PENDING";
  case OrderState::ACTIVE:
    return "ACTIVE";
  case OrderState::PARTIALLY_FILLED:
    return "PARTIALLY_FILLED";
  case OrderState::FILLED:
    return "FILLED";
  case OrderState::CANCELLED:
    return "CANCELLED";
  case OrderState::REJECTED:
    return "REJECTED";
  default:
    return "UNKNOWN";
  }
}

std::ostream &operator<<(std::ostream &os, const Order &o) {
  os << "Order{id=" << o.id << ", acct=" << o.account_id; // SHOW ACCOUNT

  // Show if it's a stop order
  if (o.is_stop && !o.stop_triggered) {
    os << ", type=STOP-"
       << (o.stop_becomes == OrderType::MARKET ? "MARKET" : "LIMIT");
  } else {
    os << ", type=" << o.type_to_string();
  }

  os << ", side=" << o.side_to_string() << ", tif=" << o.tif_to_string();

  // Show stop price if applicable
  if (o.is_stop) {
    os << ", stop_price=" << std::fixed << std::setprecision(2) << o.stop_price;
    if (o.stop_triggered) {
      os << " (TRIGGERED)";
    }
  }

  os << ", price=";

  if (o.is_market_order() && !o.is_stop) {
    os << "MARKET";
  } else if (o.type == OrderType::LIMIT) {
    os << std::fixed << std::setprecision(2) << o.price;
  } else {
    os << "MARKET";
  }

  os << ", qty=" << o.remaining_qty << "/" << o.quantity;

  // Show iceberg info if applicable
  if (o.is_iceberg() || (o.peak_size > 0)) {
    os << " [ICEBERG: display=" << o.display_qty << ", hidden=" << o.hidden_qty
       << "]";
  }

  os << ", state=" << o.state_to_string()
     << ", ts=" << o.timestamp.time_since_epoch().count() << "}";
  return os;
}

bool BidComparator::operator()(const Order &a, const Order &b) const {
  if (a.price != b.price) {
    return a.price < b.price;
  }
  return a.timestamp > b.timestamp;
}

bool AskComparator::operator()(const Order &a, const Order &b) const {
  if (a.price != b.price) {
    return a.price > b.price;
  }
  return a.timestamp > b.timestamp;
}