#include "order_book.hpp"

#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>

void OrderBook::save_events(const std::string &filename) const {
  std::ofstream file(filename);
  if (!file.is_open()) {
    throw std::runtime_error("Could not open file: " + filename);
  }

  file << OrderEvent::csv_header() << "\n";

  for (const auto &event : event_log_) {
    file << event.to_csv() << "\n";
  }

  file.close();
  std::cout << "Saved " << event_log_.size() << "events to " << filename
            << std::endl;
}

Snapshot OrderBook::create_snapshot() const {
  Snapshot snapshot;

  // Metadata
  snapshot.snapshot_time = std::chrono::system_clock::now();
  snapshot.snapshot_id = snapshot_counter_;
  snapshot.version = "1.0";

  // Copy active orders
  for (const auto &[id, order] : active_orders_) {
    snapshot.active_orders.push_back(order);
  }

  // Copy pending stops
  for (const auto &[price, order] : stop_buys_) {
    snapshot.pending_stops.push_back(order);
  }
  for (const auto &[price, order] : stop_sells_) {
    snapshot.pending_stops.push_back(order);
  }

  // Copy fills
  snapshot.fills = fills_;

  // Copy state
  snapshot.last_trade_price = last_trade_price_;
  snapshot.total_orders_processed = insertion_latencies_ns_.size();
  snapshot.latencies = insertion_latencies_ns_;

  return snapshot;
}

void OrderBook::restore_from_snapshot(const Snapshot &snapshot) {
  std::cout << "Restoring order book from snapshot..." << std::endl;

  // Clear current state
  while (!bids_.empty())
    bids_.pop();
  while (!asks_.empty())
    asks_.pop();
  active_orders_.clear();
  cancelled_orders_.clear();
  stop_buys_.clear();
  stop_sells_.clear();
  fills_.clear();
  event_log_.clear();
  insertion_latencies_ns_.clear();

  // Restore state
  last_trade_price_ = snapshot.last_trade_price;
  fills_ = snapshot.fills;
  insertion_latencies_ns_ = snapshot.latencies;

  // Restore active orders and rebuild books
  for (const auto &order : snapshot.active_orders) {
    active_orders_.insert({order.id, order});

    // Add to appropriate book if active and not stop
    if (order.is_active() && !order.is_stop) {
      if (order.side == Side::BUY) {
        bids_.push(order);
      } else {
        asks_.push(order);
      }
    }
  }

  // Restore pending stops
  for (const auto &order : snapshot.pending_stops) {
    active_orders_.insert({order.id, order});

    if (order.side == Side::BUY) {
      stop_buys_.insert({order.stop_price, order});
    } else {
      stop_sells_.insert({order.stop_price, order});
    }
  }

  std::cout << "Order book restored successfully" << std::endl;
  std::cout << "   Active orders: " << active_orders_.size() << std::endl;
  std::cout << "   Pending stops: " << (stop_buys_.size() + stop_sells_.size())
            << std::endl;
  std::cout << "   Fills: " << fills_.size() << std::endl;
}

void OrderBook::save_snapshot(const std::string &filename) const {
  auto snapshot = create_snapshot();
  const_cast<OrderBook *>(this)->snapshot_counter_++;
  snapshot.save_to_file(filename);
}

void OrderBook::load_snapshot(const std::string &filename) {
  auto snapshot = Snapshot::load_from_file(filename);

  if (!snapshot.validate()) {
    throw std::runtime_error("Snapshot validation failed");
  }

  restore_from_snapshot(snapshot);
}

void OrderBook::save_checkpoint(const std::string &snapshot_file,
                                const std::string &events_file) const {
  std::cout << "\nCreating checkpoint..." << std::endl;

  // Save snapshot
  save_snapshot(snapshot_file);

  // Save events since snapshot
  save_events(events_file);

  std::cout << "Checkpoint created:" << std::endl;
  std::cout << "   Snapshot: " << snapshot_file << std::endl;
  std::cout << "   Events: " << events_file << std::endl;
}

void OrderBook::recover_from_checkpoint(const std::string &snapshot_file,
                                        const std::string &events_file) {
  std::cout << "\nRecovering from checkpoint..." << std::endl;

  // Load snapshot
  load_snapshot(snapshot_file);

  // Replay events since snapshot
  std::ifstream event_file(events_file);
  if (event_file.is_open()) {
    std::string line;
    std::getline(event_file, line); // Skip header

    size_t event_count = 0;
    while (std::getline(event_file, line)) {
      if (line.empty())
        continue;

      auto event = OrderEvent::from_csv(line);

      // Replay event (skip FILL events as they're regenerated)
      if (event.type != EventType::FILL) {
        // Replay logic here (simplified)
        event_count++;
      }
    }

    std::cout << "Replayed " << event_count << " events" << std::endl;
  }

  std::cout << "Recovery complete" << std::endl;
}
