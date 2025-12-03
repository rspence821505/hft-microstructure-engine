#pragma once

#include <chrono>

// Common type aliases
using Clock = std::chrono::steady_clock;
using TimePoint = std::chrono::time_point<Clock>;

// Order side of book
enum class Side { BUY, SELL };

// Order operations
enum class Type { NEW, CANCEL, AMEND };

// Order Type
enum class OrderType {
  LIMIT,  // limit order (has price limit)
  MARKET, // Market order ( no price limit)
};

// Time-in-Force
enum class TimeInForce {
  GTC, // Good-Till-Cancel:: Remains until filled or cancelled
  IOC, // Immediate-Or-Cancel: Fill immediately, cancel remainder
  FOK, // Fill-Or-Kill: Fill completely or cancel entire order
  DAY, // Day order: Valid until end of trading day
};

// Order States to track order lifecycle
enum class OrderState {
  PENDING,          // Just created, not in book yet
  ACTIVE,           // In book, waiting to match
  PARTIALLY_FILLED, // Some quantity filled
  FILLED,           // Completely filled
  CANCELLED,        // Canceled by user
  REJECTED          // Rejected (e.g., invalid parameters)
};
