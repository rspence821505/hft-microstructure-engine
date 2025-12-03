#include "fill.hpp"

Fill::Fill(int buy_id, int sell_id, double px, int qty)
    : buy_order_id(buy_id), sell_order_id(sell_id), price(px), quantity(qty),
      timestamp(Clock::now()) {}

std::ostream &operator<<(std::ostream &os, const Fill &f) {
  os << "FILL: Buy #" << f.buy_order_id << " x Sell #" << f.sell_order_id
     << " | " << f.quantity << " @ $" << f.price
     << " | ts=" << f.timestamp.time_since_epoch().count();
  return os;
}