#pragma once

#include "types.hpp"
#include <iostream>

struct Fill {
  int buy_order_id;
  int sell_order_id;
  double price;
  int quantity;
  TimePoint timestamp;

  Fill(int buy_id, int sell_id, double px, int qty);

  friend std::ostream &operator<<(std::ostream &os, const Fill &f);
};
