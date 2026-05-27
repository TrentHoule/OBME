#ifndef TRADES_H
#define TRADES_H
#include "order.h"
#include "types.h"


template<typename T>
struct Trade {
    Price price;
    T quantity;
    Order<T> bidSide;
    Order<T> askSide;
};

using Trades = std::vector<Trade<Quantity>>;

template<typename T>
std::ostream& operator<<(std::ostream& os, const Trade<T>& trade) {
    os << std::format("Trade [Price={}, Qty={}, BidID={}, AskID={}]", 
        trade.price, 
        trade.quantity, 
        trade.bidSide.getOrderId(), 
        trade.askSide.getOrderId());
    return os;
}

#endif