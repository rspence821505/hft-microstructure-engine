#pragma once

#include "types.hpp"
#include <iostream>
#include <string>

struct Order {
  int id;
  int account_id; // NEW: Track which account owns this order
  Side side;
  OrderType type;    // LIMIT or MARKET
  TimeInForce tif;   // Time-in-Force
  double price;      // For Limit orders (infinity for market)
  int quantity;      // Total original quantity
  int remaining_qty; // Total remaining (visible + hidden)
  int display_qty;   // Currently visible quantity
  int hidden_qty;    // Hidden reserve quantity
  int peak_size;     // How much to reveal at refresh
  TimePoint timestamp;
  OrderState state;

  // Stop order fields
  bool is_stop;           // Is this a stop order?
  double stop_price;      // Trigger price
  bool stop_triggered;    // Has stop been triggered?
  OrderType stop_becomes; // Becomes LIMIT or MARKET when triggered

  // Constructor for LIMIT orders
  Order(int id_, int account_id_, Side side_, double price_, int qty_,
        TimeInForce tif_ = TimeInForce::GTC);

  // Constructor for MARKET orders
  Order(int id_, int account_id_, Side side_, OrderType type_, int qty_,
        TimeInForce tif_ = TimeInForce::IOC);

  // Constructor for ICEBERG orders
  Order(int id_, int account_id_, Side side_, double price_, int total_qty,
        int peak_size_, TimeInForce tif_ = TimeInForce::GTC);

  // Constructor for STOP-MARKET orders
  Order(int id_, int account_id_, Side side_, double stop_price_, int qty_,
        bool is_stop_market, TimeInForce tif_ = TimeInForce::GTC);

  // Constructor for STOP-LIMIT orders
  Order(int id_, int account_id_, Side side_, double stop_price_,
        double limit_price_, int qty_, TimeInForce tif_ = TimeInForce::GTC);

  bool is_filled() const;
  bool is_active() const;
  bool is_market_order() const;
  bool is_iceberg() const;
  bool is_stop_order() const { return is_stop; }
  bool can_rest_in_book() const;
  bool needs_refresh() const; // check if display exhausted
  void refresh_display();     // reveal more quantity
  void trigger_stop();        // convert stop to active order

  std::string side_to_string() const;
  std::string type_to_string() const;
  std::string tif_to_string() const;
  std::string state_to_string() const;

  friend std::ostream &operator<<(std::ostream &os, const Order &o);
};

// Comparators
struct BidComparator {
  bool operator()(const Order &a, const Order &b) const;
};

struct AskComparator {
  bool operator()(const Order &a, const Order &b) const;
};
