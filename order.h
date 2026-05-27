#ifndef ORDER_H
#define ORDER_H
#include "types.h"



enum class OrderType {goodTillCanceled};
enum class Side {Bid, Ask};


template<typename T>
class Order {
    static_assert(std::is_arithmetic_v<T>, "Order quantity T must be a numeric type");
    public:
    Order(OrderType type, Side side, Price price, T quantity) : 
        type{type},
        side{side},
        price{price},
        quantity{quantity},
        id{0},
        status{true} {}


    uint64_t getOrderId() const { return id; }
    OrderType getOrderType() const { return type; }
    T getOrderQuantity() const { return quantity; }
    Side getOrderSide() const { return side; }
    Price getOrderPrice() const { return price; }
    TS getOrderTimestamp() const {return timestamp; }
    bool isActive() const { return status; }
    bool isFilled() const { return quantity == 0; }

    void cancelOrder() {
        status = false;
    }

    void setOrderTimestamp(TS ts) {
        timestamp = ts;
    }

    void setOrderId(Id newId) {
        id = newId;
    }

    void fillOrder(T amount) {
        if (amount > quantity) {
            throw std::logic_error("Cannot fill with more than remaining quantity");
        }
        quantity -= amount;
    }

    bool operator==(const Order<T> &other) const {
        return id == other.getOrderId();
    }
    
    private: 
    OrderType type;
    Side side;
    Price price; // prices are stored in cents, so that we can use intergers and it makes math more convenient. 
    T quantity; // Some exchanges have fractional shares, but I am not going to handle that (yet)
    Id id; 
    TS timestamp;
    bool status;
    
};


template<typename T>
std::ostream& operator<<(std::ostream& os, const Order<T>& o) {
    os << std::format("Order {} [price={}, side={}, qty={}]",
        o.getOrderId(),
        o.getOrderPrice(),
        o.getOrderSide() == Side::Bid ? "Bid" : "Ask",
        o.getOrderQuantity());
    return os;
}
#endif