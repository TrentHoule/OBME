#include "order.h"


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