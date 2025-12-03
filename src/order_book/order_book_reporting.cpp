#include "order_book.hpp"

#include <algorithm>
#include <iomanip>
#include <iostream>
#include <map>
#include <set>
#include <sstream>

void OrderBook::print_account_fills() const {
  std::cout << "\n=== Fills with Account Information ===" << std::endl;

  if (account_fills_.empty()) {
    std::cout << "No fills yet." << std::endl;
    return;
  }

  std::cout << std::string(90, '-') << std::endl;
  std::cout << std::left << std::setw(8) << "Buy ID" << std::setw(8) << "B.Acct"
            << std::setw(8) << "Sell ID" << std::setw(8) << "S.Acct"
            << std::right << std::setw(10) << "Price" << std::setw(10)
            << "Quantity" << std::setw(12) << "Symbol" << std::endl;
  std::cout << std::string(90, '-') << std::endl;

  for (const auto &af : account_fills_) {
    std::cout << std::left << std::setw(8) << af.fill.buy_order_id
              << std::setw(8) << af.buy_account_id << std::setw(8)
              << af.fill.sell_order_id << std::setw(8) << af.sell_account_id
              << std::right << std::setw(10) << std::fixed
              << std::setprecision(2) << af.fill.price << std::setw(10)
              << af.fill.quantity << std::setw(12) << af.symbol << std::endl;
  }

  std::cout << std::string(90, '-') << std::endl;
}

std::vector<OrderBook::PriceLevel>
OrderBook::get_bid_levels(int max_levels) const {
  std::vector<PriceLevel> levels;

  if (bids_.empty()) {
    return levels;
  }

  auto bids_copy = bids_;
  std::map<double, std::pair<int, int>> price_map;

  while (!bids_copy.empty()) {
    Order order = bids_copy.top();
    bids_copy.pop();

    price_map[order.price].first += order.remaining_qty;
    price_map[order.price].second += 1;
  }

  int count = 0;
  for (auto it = price_map.rbegin();
       it != price_map.rend() && count < max_levels; ++it, ++count) {
    PriceLevel level;
    level.price = it->first;
    level.total_quantity = it->second.first;
    level.num_orders = it->second.second;
    levels.push_back(level);
  }

  return levels;
}

std::vector<OrderBook::PriceLevel>
OrderBook::get_ask_levels(int max_levels) const {
  std::vector<PriceLevel> levels;

  if (asks_.empty()) {
    return levels;
  }

  auto asks_copy = asks_;
  std::map<double, std::pair<int, int>> price_map;

  while (!asks_copy.empty()) {
    Order order = asks_copy.top();
    asks_copy.pop();

    price_map[order.price].first += order.remaining_qty;
    price_map[order.price].second += 1;
  }

  int count = 0;
  for (auto it = price_map.begin(); it != price_map.end() && count < max_levels;
       ++it, ++count) {
    PriceLevel level;
    level.price = it->first;
    level.total_quantity = it->second.first;
    level.num_orders = it->second.second;
    levels.push_back(level);
  }

  return levels;
}

void OrderBook::print_fills() const {
  std::cout << "\n=== Fills Generated ===" << std::endl;
  if (fills_.empty()) {
    std::cout << "No fills yet." << std::endl;
    return;
  }
  for (const auto &fill : fills_) {
    std::cout << fill << std::endl;
  }
}

void OrderBook::print_top_of_book() const {
  std::cout << "--- Top of Book ---" << std::endl;

  auto best_bid = get_best_bid();
  if (best_bid) {
    std::cout << "Best Bid: " << best_bid->price
              << " (qty: " << best_bid->quantity << ")" << std::endl;
  } else {
    std::cout << "Best Bid: N/A" << std::endl;
  }

  auto best_ask = get_best_ask();
  if (best_ask) {
    std::cout << "Best Ask: " << best_ask->price
              << " (qty: " << best_ask->quantity << ")" << std::endl;
  } else {
    std::cout << "Best Ask: N/A" << std::endl;
  }

  auto spread = get_spread();
  if (spread) {
    std::cout << "Spread: " << *spread << std::endl;
  } else {
    std::cout << "Spread: N/A" << std::endl;
  }
  std::cout << std::endl;
}

void OrderBook::print_book_summary() const {
  std::cout << "\n=== Current Book State ===" << std::endl;

  std::cout << "Orders in book: " << (bids_.size() + asks_.size()) << std::endl;
  std::cout << "  Bids: " << bids_.size() << std::endl;
  std::cout << "  Asks: " << asks_.size() << std::endl;

  auto best_bid = get_best_bid();
  auto best_ask = get_best_ask();
  auto spread = get_spread();

  if (best_bid && best_ask) {
    std::cout << "\nTop of Book:" << std::endl;
    std::cout << "  Best Bid: $" << std::fixed << std::setprecision(2)
              << best_bid->price << " (" << best_bid->remaining_qty
              << " shares)" << std::endl;
    std::cout << "  Best Ask: $" << std::fixed << std::setprecision(2)
              << best_ask->price << " (" << best_ask->remaining_qty
              << " shares)" << std::endl;
    std::cout << "  Spread: $" << std::fixed << std::setprecision(4) << *spread;

    if (*spread < 0) {
      std::cout << "CROSSED BOOK!" << std::endl;
    } else if (*spread == 0) {
      std::cout << " (locked)" << std::endl;
    } else if (*spread < 0.10) {
      std::cout << " (tight)" << std::endl;
    } else {
      std::cout << " (wide)" << std::endl;
    }
  } else if (best_bid) {
    std::cout << "\nBid-only market:" << std::endl;
    std::cout << "  Best Bid: $" << best_bid->price << std::endl;
    std::cout << "  No asks available" << std::endl;
  } else if (best_ask) {
    std::cout << "\nAsk-only market:" << std::endl;
    std::cout << "  Best Ask: $" << best_ask->price << std::endl;
    std::cout << "  No bids available" << std::endl;
  } else {
    std::cout << "\nEmpty book (no orders)" << std::endl;
  }
}

void OrderBook::print_market_depth(int levels) const {
  auto bid_levels = get_bid_levels(levels);
  auto ask_levels = get_ask_levels(levels);

  std::cout << "\n=== Market Depth (" << levels << " levels) ===" << std::endl;
  std::cout << std::string(70, '=') << std::endl;

  // Header
  std::cout << std::setw(25) << std::right << "BIDS"
            << " | " << std::setw(10) << "PRICE"
            << " | " << std::setw(25) << std::left << "ASKS" << std::endl;
  std::cout << std::string(70, '-') << std::endl;

  // Determine how many rows to display
  size_t max_rows = std::max(bid_levels.size(), ask_levels.size());

  // Calculate totals
  int total_bid_qty = 0;
  int total_ask_qty = 0;
  for (const auto &level : bid_levels)
    total_bid_qty += level.total_quantity;
  for (const auto &level : ask_levels)
    total_ask_qty += level.total_quantity;

  // Display rows (asks in reverse order - highest first)
  for (size_t i = 0; i < max_rows; ++i) {
    // Determine indices
    size_t ask_idx =
        (ask_levels.size() > i) ? (ask_levels.size() - 1 - i) : SIZE_MAX;
    size_t bid_idx = i;

    // Format bid side
    std::ostringstream bid_str;
    if (bid_idx < bid_levels.size()) {
      bid_str << bid_levels[bid_idx].total_quantity << " ("
              << bid_levels[bid_idx].num_orders << " order";
      if (bid_levels[bid_idx].num_orders > 1)
        bid_str << "s";
      bid_str << ")";
    }

    // Format price
    std::ostringstream price_str;
    if (ask_idx != SIZE_MAX && ask_idx < ask_levels.size()) {
      price_str << std::fixed << std::setprecision(2) << "$"
                << ask_levels[ask_idx].price;
    } else if (bid_idx < bid_levels.size()) {
      price_str << std::fixed << std::setprecision(2) << "$"
                << bid_levels[bid_idx].price;
    }

    // Format ask side
    std::ostringstream ask_str;
    if (ask_idx != SIZE_MAX && ask_idx < ask_levels.size()) {
      ask_str << ask_levels[ask_idx].total_quantity << " ("
              << ask_levels[ask_idx].num_orders << " order";
      if (ask_levels[ask_idx].num_orders > 1)
        ask_str << "s";
      ask_str << ")";
    }

    // Print row
    std::cout << std::setw(25) << std::right << bid_str.str() << " | "
              << std::setw(10) << std::left << price_str.str() << " | "
              << std::setw(25) << std::left << ask_str.str() << std::endl;
  }

  // Footer with totals
  std::cout << std::string(70, '-') << std::endl;
  std::cout << std::setw(25) << std::right
            << ("Total: " + std::to_string(total_bid_qty) + " shares") << " | "
            << std::setw(10) << " "
            << " | " << std::setw(25) << std::left
            << ("Total: " + std::to_string(total_ask_qty) + " shares")
            << std::endl;
  std::cout << std::string(70, '=') << std::endl;

  // Additional stats
  auto spread = get_spread();
  if (spread) {
    double spread_bps = (*spread / bid_levels[0].price) * 10000;
    std::cout << "Spread: $" << std::fixed << std::setprecision(4) << *spread
              << " (" << std::fixed << std::setprecision(2) << spread_bps
              << " bps)" << std::endl;
  }
  std::cout << std::endl;
}

void OrderBook::print_market_depth_compact() const {
  auto bid_levels = get_bid_levels(10); // Get more levels for compact view
  auto ask_levels = get_ask_levels(10);

  std::cout << "\n=== Order Book (Compact) ===" << std::endl;

  // Print asks (top to bottom)
  std::cout << "\n ASKS (sellers):" << std::endl;
  if (ask_levels.empty()) {
    std::cout << "  (no asks)" << std::endl;
  } else {
    for (auto it = ask_levels.rbegin(); it != ask_levels.rend(); ++it) {
      std::cout << "  $" << std::fixed << std::setprecision(2) << std::setw(8)
                << it->price << "  │ " << std::setw(6) << it->total_quantity
                << " (" << it->num_orders << ")" << std::endl;
    }
  }

  // Spread line
  auto spread = get_spread();
  if (spread) {
    std::cout << std::string(30, '-') << std::endl;
    std::cout << "  Spread: $" << std::fixed << std::setprecision(4) << *spread
              << std::endl;
    std::cout << std::string(30, '-') << std::endl;
  } else {
    std::cout << std::string(30, '-') << std::endl;
    std::cout << "  (crossed or one-sided)" << std::endl;
    std::cout << std::string(30, '-') << std::endl;
  }

  // Print bids (top to bottom)
  std::cout << "\n BIDS (buyers):" << std::endl;
  if (bid_levels.empty()) {
    std::cout << "  (no bids)" << std::endl;
  } else {
    for (const auto &level : bid_levels) {
      std::cout << "  $" << std::fixed << std::setprecision(2) << std::setw(8)
                << level.price << "  │ " << std::setw(6) << level.total_quantity
                << " (" << level.num_orders << ")" << std::endl;
    }
  }
  std::cout << std::endl;
}

void OrderBook::print_order_status(int order_id) const {
  auto order = get_order(order_id);
  if (order) {
    std::cout << "\n=== Order Status ===" << std::endl;
    std::cout << *order << std::endl;
  } else {
    std::cout << "Order " << order_id << " not found." << std::endl;
  }
}

void OrderBook::print_pending_stops() const {
  std::cout << "\n=== Pending Stop Orders ===" << std::endl;

  if (stop_buys_.empty() && stop_sells_.empty()) {
    std::cout << "No pending stop orders." << std::endl;
    return;
  }

  if (!stop_buys_.empty()) {
    std::cout << "\nStop-Buy Orders (trigger at or above):" << std::endl;
    for (const auto &[price, order] : stop_buys_) {
      std::cout << "  $" << std::fixed << std::setprecision(2) << price
                << " → Order #" << order.id << " (" << order.quantity
                << " shares)" << std::endl;
    }
  }

  if (!stop_sells_.empty()) {
    std::cout << "\nStop-Sell Orders (trigger at or below):" << std::endl;
    for (const auto &[price, order] : stop_sells_) {
      std::cout << "  $" << std::fixed << std::setprecision(2) << price
                << " → Order #" << order.id << " (" << order.quantity
                << " shares)" << std::endl;
    }
  }

  std::cout << std::endl;
}

void OrderBook::print_trade_timeline() const {
  std::cout << "\n=== Trade Timeline ===" << std::endl;

  if (fills_.empty()) {
    std::cout << "No trades executed." << std::endl;
    return;
  }

  std::cout << "Displaying " << fills_.size()
            << " fills in chronological order:\n"
            << std::endl;

  for (size_t i = 0; i < fills_.size(); ++i) {
    const auto &fill = fills_[i];
    std::cout << "[" << (i + 1) << "] " << fill << std::endl;
  }
}

void OrderBook::print_latency_stats() const {
  if (insertion_latencies_ns_.empty()) {
    std::cout << "No orders inserted yet!" << std::endl;
    return;
  }

  std::vector<long long> sorted = insertion_latencies_ns_;
  std::sort(sorted.begin(), sorted.end());

  size_t n = sorted.size();
  long long total = 0;
  for (long long lat : sorted) {
    total += lat;
  }

  long long min = sorted.front();
  long long max = sorted.back();
  double avg = static_cast<double>(total) / n;
  long long p50 = sorted[n / 2];
  long long p95 = sorted[static_cast<size_t>(0.95 * n)];
  long long p99 = sorted[static_cast<size_t>(0.99 * n)];

  std::cout << "\n=== Order Insertion Latency ===" << std::endl;
  std::cout << "Total orders: " << n << std::endl;
  std::cout << "Average: " << avg << " ns" << std::endl;
  std::cout << "Min: " << min << " ns" << std::endl;
  std::cout << "Max: " << max << " ns" << std::endl;
  std::cout << "p50: " << p50 << " ns" << std::endl;
  std::cout << "p95: " << p95 << " ns" << std::endl;
  std::cout << "p99: " << p99 << std::endl;
}

void OrderBook::print_match_stats() const {
  std::cout << "\n=== Matching Statistics ===" << std::endl;

  std::cout << "Total orders processed: " << insertion_latencies_ns_.size()
            << std::endl;
  std::cout << "Total fills generated: " << fills_.size() << std::endl;

  if (!fills_.empty()) {
    int total_volume = 0;
    double total_notional = 0.0;
    double min_price = fills_[0].price;
    double max_price = fills_[0].price;

    for (const auto &fill : fills_) {
      total_volume += fill.quantity;
      total_notional += fill.quantity * fill.price;
      min_price = std::min(min_price, fill.price);
      max_price = std::max(max_price, fill.price);
    }

    double avg_fill_size = static_cast<double>(total_volume) / fills_.size();
    double vwap = total_notional / total_volume;

    std::cout << "\n--- Volume Statistics ---" << std::endl;
    std::cout << "Total volume traded: " << total_volume << " shares"
              << std::endl;
    std::cout << "Total notional value: $" << std::fixed << std::setprecision(2)
              << total_notional << std::endl;
    std::cout << "Average fill size: " << std::fixed << std::setprecision(1)
              << avg_fill_size << " shares" << std::endl;
    std::cout << "VWAP: $" << std::fixed << std::setprecision(2) << vwap
              << std::endl;
    std::cout << "Price range: $" << std::fixed << std::setprecision(2)
              << min_price << " - $" << max_price << std::endl;
  }

  print_latency_stats();
}

void OrderBook::print_fill_rate_analysis() const {
  if (insertion_latencies_ns_.empty()) {
    std::cout << "No orders to analyze!" << std::endl;
    return;
  }

  std::cout << "\n=== Fill Rate Analysis ===" << std::endl;

  size_t total_orders = insertion_latencies_ns_.size();

  std::set<int> filled_order_ids;
  for (const auto &fill : fills_) {
    filled_order_ids.insert(fill.buy_order_id);
    filled_order_ids.insert(fill.sell_order_id);
  }
  size_t orders_with_fills = filled_order_ids.size();

  double fill_rate =
      (static_cast<double>(orders_with_fills) / total_orders) * 100.0;

  std::cout << "Orders that generated fills: " << orders_with_fills << " / "
            << total_orders << " (" << std::fixed << std::setprecision(1)
            << fill_rate << "%)" << std::endl;
  std::cout << "Orders added to book (no fill): "
            << (total_orders - orders_with_fills) << std::endl;
}
