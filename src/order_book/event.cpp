#include "event.hpp"
#include <iomanip>
#include <iostream>
#include <sstream>
#include <vector>

std::string OrderEvent::to_string() const {
  std::ostringstream oss;
  oss << event_type_to_string(type) << "order_ids=" << order_id;

  if (type == EventType::NEW_ORDER) {
    oss << "side=" << (side == Side::BUY ? "BUY" : "SELL")
        << " price=" << std::fixed << std::setprecision(2) << price
        << " qty=" << quantity;
    if (peak_size > 0) {
      oss << " peak=" << peak_size;
    }
  } else if (type == EventType::AMEND_ORDER) {
    if (has_new_price)
      oss << " new_price=" << new_price;
    if (has_new_quantity)
      oss << " new_qty" << new_quantity;
  } else if (type == EventType::FILL) {
    oss << "counterparty=" << counterparty_id << " price=" << price
        << " qty=" << fill_quantity;
  }
  return oss.str();
}

std::string OrderEvent::csv_header() {
  return "timestamp,type,order_id,side,order-type,tif,price,quantity,peak_size,"
         "account_id,has_new_price,has_new_qty,new_price,new_qty,counterparty,"
         "fill_qty";
}

std::string OrderEvent::to_csv() const {
  std::ostringstream oss;
  oss << timestamp.time_since_epoch().count() << ","
      << event_type_to_string(type) << ',' << order_id << ",";

  // Side
  if (type == EventType::NEW_ORDER) {
    oss << (side == Side::BUY ? "BUY" : "SELL");
  } else {
    oss << "N/A";
  }
  oss << ",";

  // Order Type
  if (type == EventType::NEW_ORDER) {
    oss << (order_type == OrderType::LIMIT ? "LIMIT" : "MARKET");
  } else {
    oss << "N/A";
  }
  oss << ",";

  // TIF
  if (type == EventType::NEW_ORDER) {
    switch (tif) {
    case TimeInForce::GTC:
      oss << "GTC";
      break;
    case TimeInForce::IOC:
      oss << "IOC";
      break;
    case TimeInForce::FOK:
      oss << "FOK";
      break;
    case TimeInForce::DAY:
      oss << "DAY";
      break;
    }
  } else {
    oss << "N/A";
  }
  oss << ",";

  // Price, quantity, peak_size, account id
  oss << std::fixed << std::setprecision(2) << price << "," << quantity << ","
      << peak_size << "," << account_id << ",";

  // Amendment fields
  oss << (has_new_price ? "1" : "0") << "," << (has_new_quantity ? "1" : "0")
      << "," << std::fixed << std::setprecision(2) << new_price << ","
      << new_quantity << ",";

  // Fill fields
  oss << counterparty_id << "," << fill_quantity;

  return oss.str();
}

OrderEvent OrderEvent::from_csv(const std::string &line) {
  std::istringstream iss(line);
  std::string token;
  std::vector<std::string> tokens;

  while (std::getline(iss, token, ',')) {
    tokens.push_back(token);
  }

  if (tokens.size() < 16) {
    // ADD DEBUG OUTPUT HERE
    std::cerr << "Invalid CSV line (got " << tokens.size()
              << " fields, expected 16+):" << std::endl;
    std::cerr << "Line: " << line << std::endl;
    std::cerr << "Tokens parsed: ";
    for (size_t i = 0; i < tokens.size(); ++i) {
      std::cerr << "[" << i << "]=\"" << tokens[i] << "\" ";
    }
    std::cerr << std::endl;
    throw std::runtime_error("Invalid CSV line: insufficient fields");
  }

  // Parse timestamp
  long long ts_count = std::stoll(tokens[0]);
  TimePoint ts{std::chrono::nanoseconds(ts_count)};

  // Parse event type
  EventType type = string_to_event_type(tokens[1]);
  int order_id = std::stoi(tokens[2]);

  if (type == EventType::NEW_ORDER) {
    Side side = (tokens[3] == "BUY") ? Side::BUY : Side::SELL;
    OrderType ot =
        (tokens[4] == "LIMIT") ? OrderType::LIMIT : OrderType::MARKET;

    TimeInForce tif;
    if (tokens[5] == "GTC") {
      tif = TimeInForce::GTC;
    } else if (tokens[5] == "IOC") {
      tif = TimeInForce::IOC;
    } else if (tokens[5] == "FOK") {
      tif = TimeInForce::FOK;
    } else {
      tif = TimeInForce::DAY;
    }

    double price = std::stod(tokens[6]);
    int quantity = std::stoi(tokens[7]);
    int peak_size = std::stoi(tokens[8]);
    int account_id = std::stoi(tokens[9]);

    OrderEvent event(ts, order_id, side, ot, tif, price, quantity, peak_size,
                     account_id);
    return event;
  } else if (type == EventType::CANCEL_ORDER) {
    int account_id = std::stoi(tokens[9]);
    return OrderEvent(ts, type, order_id, account_id);
  } else if (type == EventType::AMEND_ORDER) {
    int account_id = std::stoi(tokens[9]);
    bool has_new_price = (tokens[10] == "1");
    bool has_new_qty = (tokens[11] == "1");

    std::optional<double> new_price;
    std::optional<int> new_qty;

    if (has_new_price)
      new_price = std::stod(tokens[12]);
    if (has_new_qty)
      new_qty = std::stoi(tokens[13]);

    return OrderEvent(ts, order_id, new_price, new_qty, account_id);
  } else { // FILL
    int account_id = std::stoi(tokens[9]);
    int counterparty = std::stoi(tokens[14]);
    double price = std::stod(tokens[6]);
    int qty = std::stoi(tokens[15]);
    return OrderEvent(ts, order_id, counterparty, price, qty, account_id);
  }
}
