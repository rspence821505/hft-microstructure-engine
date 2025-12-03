#pragma once

#include "types.hpp"
#include <optional>
#include <string>

enum class EventType {
  NEW_ORDER,
  CANCEL_ORDER,
  AMEND_ORDER,
  FILL,
};

struct OrderEvent {
  TimePoint timestamp;
  EventType type;
  int order_id;
  Side side;
  OrderType order_type;
  TimeInForce tif;
  double price;
  int quantity;
  int account_id; // Track which account owns this order

  // For iceberg orders
  int peak_size;

  // For amendments
  bool has_new_price;
  bool has_new_quantity;
  double new_price;
  int new_quantity;

  // For fills
  int counterparty_id;
  int fill_quantity;

  // Constructor for NEW orders
  OrderEvent(TimePoint ts, int id, Side s, OrderType ot, TimeInForce tif_,
             double p, int q, int peak = 0, int acct_id = -1)
      : timestamp(ts), type(EventType::NEW_ORDER), order_id(id), side(s),
        order_type(ot), tif(tif_), price(p), quantity(q), account_id(acct_id),
        peak_size(peak), has_new_price(false), has_new_quantity(false),
        new_price(0), new_quantity(0), counterparty_id(0), fill_quantity(0) {}

  // Constructor for CANCEL
  OrderEvent(TimePoint ts, EventType t, int id, int acct_id = -1)
      : timestamp(ts), type(t), order_id(id), side(Side::BUY),
        order_type(OrderType::LIMIT), tif(TimeInForce::GTC), price(0),
        quantity(0), account_id(acct_id), peak_size(0), has_new_price(false),
        has_new_quantity(false), new_price(0), new_quantity(0),
        counterparty_id(0), fill_quantity(0) {}

  // Constructor for AMEND
  OrderEvent(TimePoint ts, int id, std::optional<double> new_p,
             std::optional<int> new_q, int acct_id = -1)
      : timestamp(ts), type(EventType::AMEND_ORDER), order_id(id),
        side(Side::BUY), order_type(OrderType::LIMIT), tif(TimeInForce::GTC),
        price(0), quantity(0), account_id(acct_id), peak_size(0),
        has_new_price(new_p.has_value()), has_new_quantity(new_q.has_value()),
        new_price(new_p.value_or(0)), new_quantity(new_q.value_or(0)),
        counterparty_id(0), fill_quantity(0) {}

  // Constructor for FILL
  OrderEvent(TimePoint ts, int buy_id, int sell_id, double p, int q,
             int acct_id = -1)
      : timestamp(ts), type(EventType::FILL), order_id(buy_id), side(Side::BUY),
        order_type(OrderType::LIMIT), tif(TimeInForce::GTC), price(p),
        quantity(q), account_id(acct_id), peak_size(0), has_new_price(false),
        has_new_quantity(false), new_price(0), new_quantity(0),
        counterparty_id(sell_id), fill_quantity(q) {}

  std::string to_string() const;
  std::string to_csv() const;
  static OrderEvent from_csv(const std::string &line);
  static std::string csv_header();
};

inline std::string event_type_to_string(EventType type) {
  switch (type) {
  case EventType::NEW_ORDER:
    return "NEW";
  case EventType::CANCEL_ORDER:
    return "CANCEL";
  case EventType::AMEND_ORDER:
    return "AMEND";
  case EventType::FILL:
    return "FILL";
  default:
    return "UNKNOWN";
  }
}

inline EventType string_to_event_type(const std::string &str) {
  if (str == "NEW")
    return EventType::NEW_ORDER;
  if (str == "CANCEL")
    return EventType::CANCEL_ORDER;
  if (str == "AMEND")
    return EventType::AMEND_ORDER;
  if (str == "FILL")
    return EventType::FILL;
  throw std::runtime_error("Unknown event type: " + str);
}
