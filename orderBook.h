#ifndef ORDERBOOK_H
#   define ORDERBOOK_H

#include <iostream>
#include <string>
#include <vector>
#include <list>
#include <algorithm>
#include <chrono>
#include <format>
#include <unordered_map>

using Price = int32_t; 
using Id = uint64_t;
using Quantity = uint32_t;
using TS = std::chrono::system_clock::time_point;

enum class OrderType {goodTillCanceled};
enum class Side {Bid, Ask};


template<typename T>
class Order {
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

    bool isFilled() const {
        return quantity == 0;
    }

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
            throw std::logic_error(std::format("Cannot fill with more than remaining quantity"));
        }
        quantity -= amount;
    }

    // Dead code from when this was used for heaps, keeping for now incase I need it for some reason
    // auto operator<=>(const Order<T> &other) const {
    //     if (auto cmp = price <=> other.getOrderPrice(); cmp != 0) {
    //         return cmp;
    //     }
    //     return other.getOrderId() <=> id;
    // }

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
struct Trade {
    Price price;
    T amount;
    Order<T> bidSide;
    Order<T> askSide;

    void printTrade(){
        std::cout << std::format("Trade info:\nPrice: {} - Shares: {}", price, amount) << std::endl;
    }
};

using Trades = std::vector<Trade<Quantity>>;


template<typename T, Side S>
struct OrderComparator {
    bool operator()(std::shared_ptr<Order<T>> const &a, std::shared_ptr<Order<T>> const &b) const {
        // First compare by price, if price is the same fall back to id
        if (a->getOrderPrice() != b->getOrderPrice()){
            // If it is a bid, we want to check if the price is greater (ie who is willing to pay the most)
            if constexpr (S == Side::Bid) {
                return a->getOrderPrice() < b->getOrderPrice();
            } else {
                return a->getOrderPrice() > b->getOrderPrice();
            }
        } else {
            return a->getOrderId() > b->getOrderId();
        }
    }
};

template<typename T>
class OrderBook {

    private:
    std::vector<std::shared_ptr<Order<T>>> bids;
    std::vector<std::shared_ptr<Order<T>>> asks;
    std::unordered_map<Id, std::shared_ptr<Order<T>>> OrderList;
    Id currentId = 0;

    std::optional<std::reference_wrapper<Order<T>>> getNextOrder(Side side) {
        auto& vec = (side == Side::Bid ? bids : asks);
        // grab the next order from the heap
        if (vec.empty()) {
           return std::nullopt;
        }

        std::shared_ptr<Order<T>> &nextOrder = vec.at(0);
        // While we don't have an active order, delete this one from OrderList and the heap and get the next order
        while (!vec.empty() && !nextOrder->isActive()) {
            Id id = nextOrder->getOrderId();
            OrderList.erase(id);
            if (side == Side::Bid) {
                std::pop_heap(vec.begin(), vec.end(), OrderComparator<T, Side::Bid>());
            } else {
                std::pop_heap(vec.begin(), vec.end(), OrderComparator<T, Side::Ask>());
            }
            vec.pop_back();

            // no more active orders
            if (vec.empty()) {
                return std::nullopt;
            }
        }
        return std::ref(*nextOrder);
    }

    Trades matchOrder(Order<T> &order) {
        // To match an order we need to:
        // know that it can match √
        // get the top order to match with
        Side side = order.getOrderSide();
        Side oppSide = (side == Side::Bid) ? Side::Ask : Side::Bid;
        Price price = order.getOrderPrice();
        Trades trades;

        while (order.getOrderQuantity() > 0) {

            auto nextOrder = getNextOrder(oppSide);
            // No order to match against, so we break and add the order
            if (!nextOrder.has_value()){ break; }
            Order<T> &matchingOrder = nextOrder->get();

            bool priceMatches = (side == Side::Bid)
            ? price >= matchingOrder.getOrderPrice()
            : price <= matchingOrder.getOrderPrice();

            // The price doesnt match, just add it to the book
            if (!priceMatches) { break; }

            std::cout << std::format("matching order {} with {}", order.getOrderId(), matchingOrder.getOrderId()) << std::endl;
            // subtract the quantity from both orders
            Quantity quantity = (order.getOrderQuantity() < matchingOrder.getOrderQuantity() ? order.getOrderQuantity() : matchingOrder.getOrderQuantity());
            order.fillOrder(quantity);
            matchingOrder.fillOrder(quantity);

            // add the trade to our trades
            if (side == Side::Bid) {
                trades.push_back(Trade<Quantity>(price, quantity, order, matchingOrder));
            } else {
                trades.push_back(Trade<Quantity>(price, quantity, matchingOrder, order));
            }
            
            if (matchingOrder.getOrderQuantity() == 0) {
                OrderList.erase(matchingOrder.getOrderId());
                Side mSide = matchingOrder.getOrderSide();
                if (mSide == Side::Bid) {
                    std::pop_heap(bids.begin(), bids.end(), OrderComparator<T, Side::Bid>());
                    bids.pop_back();
                } else {
                    std::pop_heap(asks.begin(), asks.end(), OrderComparator<T, Side::Ask>());
                    asks.pop_back();
                }
            }

            // continue matching if there is any left over quantity, or if it cannot match then add it to the order book
        }
        return trades;
    }

    void printOrder(const std::vector<std::shared_ptr<Order<T>>> &orders) const { 
        for (const auto &order : orders){
            std::cout << std::format("Price: {} - Quantity: {} - Timestamp: {} - Internal id: {}", 
                order->getOrderPrice(), order->getOrderQuantity(), order->getOrderTimestamp(), order->getOrderId()) << std::endl;
        }
    }

    public:
    OrderBook() = default;

    void printBids() const { 
        std::cout << "Bids:" << std::endl;
        printOrder(bids); 
    }
    void printAsks() const { 
        std::cout << "Asks:" << std::endl;
        printOrder(asks); 
    }

    void printOrderList() const {
        for (auto const& [id, order] : OrderList){
            std::cout << std::format("\nPrice: {}\nQuantity: {}\nTimestamp: {}\nMap id: {}\nInternal id: {}", 
                order.getOrderPrice(), order.getOrderQuantity(), order.getOrderTimestamp(), id, order.getOrderId()) << std::endl;
        }
    }

    void addOrder(Order<T> order) {
        order.setOrderTimestamp(std::chrono::system_clock::now());
        Id id = ++currentId;
        Side side = order.getOrderSide();
        Price price = order.getOrderPrice();
        order.setOrderId(id);

        Trades trades = matchOrder(order);
        // for (auto &trade : trades) {
        //     trade.Trade<T>::printTrade();
        // }

        if (!order.isFilled()) {
            auto orderPtr = std::make_shared<Order<T>>(order);
            OrderList.emplace(id, orderPtr);
            // Add the order to bids/asks and maintain the heaps
            if (side == Side::Bid) {
                bids.push_back(orderPtr);
                std::push_heap(bids.begin(), bids.end(), OrderComparator<T, Side::Bid>());
            } else {
                asks.push_back(orderPtr);
                std::push_heap(asks.begin(), asks.end(), OrderComparator<T, Side::Ask>());
            }
        }
    }

    void cancelOrder(Id id) {
        OrderList.at(id)->cancelOrder();
    }
};


#endif