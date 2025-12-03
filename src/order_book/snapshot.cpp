#include "snapshot.hpp"
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <unordered_set>

void Snapshot::save_to_file(const std::string &filename) const {
  std::ofstream file(filename);
  if (!file.is_open()) {
    throw std::runtime_error("Could not open file for writing: " + filename);
  }

  // Write header
  file << "# Order Book Snapshot\n";
  file << "# Version: " << version << "\n";
  file << "# Snapshot ID: " << snapshot_id << "\n";
  file << "# Timestamp: " << std::chrono::system_clock::to_time_t(snapshot_time)
       << "\n";
  file << "#\n";

  // Write last trade price
  file << "LAST_TRADE_PRICE," << std::fixed << std::setprecision(4)
       << last_trade_price << "\n";

  // Write statistics
  file << "TOTAL_ORDERS," << total_orders_processed << "\n";

  // Write active orders
  file << "\n# Active Orders\n";
  file << "ACTIVE_ORDERS," << active_orders.size() << "\n";
  for (const auto &order : active_orders) {
    file << "ORDER," << order.id << ","
         << (order.side == Side::BUY ? "BUY" : "SELL") << ","
         << (order.type == OrderType::LIMIT ? "LIMIT" : "MARKET") << ","
         << order.price << "," << order.quantity << "," << order.remaining_qty
         << "," << order.display_qty << "," << order.hidden_qty << ","
         << order.peak_size << "," << static_cast<int>(order.state) << ","
         << order.timestamp.time_since_epoch().count() << ","
         << (order.is_stop ? "1" : "0") << "," << order.stop_price << ","
         << (order.stop_triggered ? "1" : "0") << "\n";
  }

  // Write pending stops
  file << "\n# Pending Stop Orders\n";
  file << "PENDING_STOPS," << pending_stops.size() << "\n";
  for (const auto &order : pending_stops) {
    file << "STOP," << order.id << ","
         << (order.side == Side::BUY ? "BUY" : "SELL") << ","
         << order.stop_price << "," << order.price << "," << order.quantity
         << ","
         << (order.stop_becomes == OrderType::MARKET ? "MARKET" : "LIMIT")
         << "\n";
  }

  // Write fills
  file << "\n# Fills\n";
  file << "FILLS," << fills.size() << "\n";
  for (const auto &fill : fills) {
    file << "FILL," << fill.buy_order_id << "," << fill.sell_order_id << ","
         << fill.price << "," << fill.quantity << ","
         << fill.timestamp.time_since_epoch().count() << "\n";
  }

  file.close();
  std::cout << "Snapshot saved to " << filename << std::endl;
}

Snapshot Snapshot::load_from_file(const std::string &filename) {
  std::ifstream file(filename);
  if (!file.is_open()) {
    throw std::runtime_error("Could not open file for reading: " + filename);
  }

  Snapshot snapshot;
  snapshot.snapshot_time = std::chrono::system_clock::now();
  snapshot.version = "1.0";

  std::string line;
  while (std::getline(file, line)) {
    if (line.empty() || line[0] == '#') {
      continue; // Skip comments and empty lines
    }

    std::istringstream iss(line);
    std::string type;
    std::getline(iss, type, ',');

    if (type == "LAST_TRADE_PRICE") {
      iss >> snapshot.last_trade_price;
    } else if (type == "TOTAL_ORDERS") {
      iss >> snapshot.total_orders_processed;
    } else if (type == "ORDER") {
      // Parse active order
      int id, qty, remaining, display, hidden, peak, state_int;
      std::string side_str, type_str;
      double price;
      long long ts;
      int is_stop, stop_triggered;
      double stop_price;

      char comma;
      iss >> id >> comma;
      std::getline(iss, side_str, ',');
      std::getline(iss, type_str, ',');
      iss >> price >> comma >> qty >> comma >> remaining >> comma >> display >>
          comma >> hidden >> comma >> peak >> comma >> state_int >> comma >>
          ts >> comma >> is_stop >> comma >> stop_price >> comma >>
          stop_triggered;

      Side side = (side_str == "BUY") ? Side::BUY : Side::SELL;
      OrderType ot =
          (type_str == "LIMIT") ? OrderType::LIMIT : OrderType::MARKET;

      // Create order with basic constructor
      Order order(id, -1, side, price, qty, TimeInForce::GTC);
      order.remaining_qty = remaining;
      order.display_qty = display;
      order.hidden_qty = hidden;
      order.peak_size = peak;
      order.state = static_cast<OrderState>(state_int);
      order.timestamp = TimePoint(std::chrono::nanoseconds(ts));
      order.is_stop = (is_stop == 1);
      order.stop_price = stop_price;
      order.stop_triggered = (stop_triggered == 1);
      order.type = ot;

      snapshot.active_orders.push_back(order);
    } else if (type == "STOP") {
      // Parse pending stop
      int id, qty;
      std::string side_str, becomes_str;
      double stop_price, limit_price;

      char comma;
      iss >> id >> comma;
      std::getline(iss, side_str, ',');
      iss >> stop_price >> comma >> limit_price >> comma >> qty >> comma;
      std::getline(iss, becomes_str, ',');

      Side side = (side_str == "BUY") ? Side::BUY : Side::SELL;

      Order order(id, -1, side, stop_price, limit_price, qty,
                  TimeInForce::GTC);
      snapshot.pending_stops.push_back(order);
    } else if (type == "FILL") {
      // Parse fill
      int buy_id, sell_id, qty;
      double price;
      long long ts;

      char comma;
      iss >> buy_id >> comma >> sell_id >> comma >> price >> comma >> qty >>
          comma >> ts;

      Fill fill(buy_id, sell_id, price, qty);
      fill.timestamp = TimePoint(std::chrono::nanoseconds(ts));
      snapshot.fills.push_back(fill);
    }
  }

  file.close();
  std::cout << "Snapshot loaded from " << filename << std::endl;
  snapshot.print_summary();

  return snapshot;
}

bool Snapshot::validate() const {
  // Basic validation checks
  bool valid = true;

  // Check for duplicate order IDs
  std::unordered_set<int> order_ids;
  for (const auto &order : active_orders) {
    if (order_ids.count(order.id)) {
      std::cout << "Duplicate order ID: " << order.id << std::endl;
      valid = false;
    }
    order_ids.insert(order.id);
  }

  // Check order consistency
  for (const auto &order : active_orders) {
    if (order.remaining_qty > order.quantity) {
      std::cout << "Order " << order.id << " has remaining > total"
                << std::endl;
      valid = false;
    }
    if (order.remaining_qty < 0) {
      std::cout << "Order " << order.id << " has negative remaining"
                << std::endl;
      valid = false;
    }
  }

  if (valid) {
    std::cout << "Snapshot validation passed" << std::endl;
  }

  return valid;
}

void Snapshot::print_summary() const {
  std::cout << "\n=== Snapshot Summary ===" << std::endl;
  std::cout << "Version: " << version << std::endl;
  std::cout << "Snapshot ID: " << snapshot_id << std::endl;
  std::cout << "Active orders: " << active_orders.size() << std::endl;
  std::cout << "Pending stops: " << pending_stops.size() << std::endl;
  std::cout << "Fills: " << fills.size() << std::endl;
  std::cout << "Last trade price: $" << std::fixed << std::setprecision(2)
            << last_trade_price << std::endl;
  std::cout << "Total orders processed: " << total_orders_processed
            << std::endl;
  std::cout << std::endl;
}

void Snapshot::save_to_binary(const std::string &filename) const {
  std::ofstream file(filename, std::ios::binary);
  if (!file.is_open()) {
    throw std::runtime_error("Could not open file for binary write: " +
                             filename);
  }

  // Write magic number and version
  uint32_t magic = 0x4F424B53; // "OBKS" = Order Book Snapshot
  file.write(reinterpret_cast<const char *>(&magic), sizeof(magic));

  // Write counts
  size_t active_count = active_orders.size();
  size_t stop_count = pending_stops.size();
  size_t fill_count = fills.size();

  file.write(reinterpret_cast<const char *>(&active_count),
             sizeof(active_count));
  file.write(reinterpret_cast<const char *>(&stop_count), sizeof(stop_count));
  file.write(reinterpret_cast<const char *>(&fill_count), sizeof(fill_count));
  file.write(reinterpret_cast<const char *>(&last_trade_price),
             sizeof(last_trade_price));

  // Write orders (simplified - you'd write all fields in production)
  for (const auto &order : active_orders) {
    file.write(reinterpret_cast<const char *>(&order.id), sizeof(order.id));
    file.write(reinterpret_cast<const char *>(&order.side), sizeof(order.side));
    file.write(reinterpret_cast<const char *>(&order.price),
               sizeof(order.price));
    file.write(reinterpret_cast<const char *>(&order.quantity),
               sizeof(order.quantity));
    file.write(reinterpret_cast<const char *>(&order.remaining_qty),
               sizeof(order.remaining_qty));
    // ... write other fields ...
  }

  file.close();
  std::cout << "Binary snapshot saved to " << filename << std::endl;
}
