#pragma once

#include "fill.hpp"
#include "order.hpp"
#include <chrono>
#include <string>
#include <vector>

struct Snapshot {
  // Metadata
  std::chrono::system_clock::time_point snapshot_time;
  size_t snapshot_id;
  std::string version; // Schema version for compatibility

  // Order book state
  std::vector<Order> active_orders;
  std::vector<Order> pending_stops;
  std::vector<Fill> fills;
  double last_trade_price;

  // Statistics
  size_t total_orders_processed;
  std::vector<long long> latencies;

  // Serialization
  void save_to_file(const std::string &filename) const;
  static Snapshot load_from_file(const std::string &filename);

  // Binary format (more efficient)
  void save_to_binary(const std::string &filename) const;
  static Snapshot load_from_binary(const std::string &filename);

  // Human-readable JSON format
  std::string to_json() const;
  static Snapshot from_json(const std::string &json);

  // Validation
  bool validate() const;
  void print_summary() const;
};
