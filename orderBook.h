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
#include <memory>
#include <optional>
#include <functional>
#include "types.h"
#include "order.h"
#include "trade.h"

/*
This code was not working, and the only reason I can figure out why it might not be is due to apple c++ compilation differences. I'm leaving it in just to show that I tried to create a Order<T> formatter.
*/

// namespace std {
//     template <typename T>
//     struct std::formatter<Order<T>> {
//         constexpr auto parse(std::format_parse_context& ctx) { return ctx.begin(); }
        
//         auto format(const Order<T>& o, std::format_context& ctx) const {
//             return std::format_to(ctx.out(),
//                 "Order [id={}, side={}, price={}, qty={}]",
//                 o.getOrderId(),
//                 o.getOrderSide() == Side::Bid ? "Bid" : "Ask",
//                 o.getOrderPrice(),
//                 o.getOrderQuantity()
//             );
//         }
//     };
// }

// Inspired by lecture 3, section Iterators - Overkill for now, but simplifies logic that I may want to use in the future for this project so I wanted to include it 
template<typename T>
class OrderBookIterator{
    public:
    using value_type        = Order<T>;
    using reference         = const Order<T>&;
    using pointer           = const Order<T>*;
    using difference_type   = std::ptrdiff_t;
    using iterator_category = std::forward_iterator_tag;

    OrderBookIterator(std::shared_ptr<std::vector<std::shared_ptr<Order<T>>>> sorted, size_t idx) : 
        sortedOrders(std::move(sorted)), 
        index(idx) {};
    
    reference operator*() const {
        return *(*sortedOrders)[index];
    }
    pointer operator->() const {
        return (*sortedOrders)[index].get();
    }
    OrderBookIterator& operator++() {
        ++index;
        return *this;
    }
    OrderBookIterator operator++(int) {
        OrderBookIterator tmp = *this;
        ++(*this);
        return tmp;
    }
    bool operator==(const OrderBookIterator& other) const {
        return index == other.index && sortedOrders == other.sortedOrders;
    }
    // I don't need this with c++20 because the compiler automatically generates it from operator==
    // but I'm leaving it in for now.
    bool operator!=(const OrderBookIterator& other) const {
        return !(*this == other);
    }

    private:
    std::shared_ptr<std::vector<std::shared_ptr<Order<T>>>> sortedOrders;
    size_t index;
};

template<typename T>
struct OrderBookView {
    std::shared_ptr<std::vector<std::shared_ptr<Order<T>>>> sortedOrders;

    OrderBookIterator<T> begin() const {
        return OrderBookIterator<T>(sortedOrders, 0);
    }

    OrderBookIterator<T> end() const {
        return OrderBookIterator<T>(sortedOrders, sortedOrders->size());
    }
};

template<typename T>
class OrderBook {

    private:
    std::vector<std::shared_ptr<Order<T>>> bids;
    std::vector<std::shared_ptr<Order<T>>> asks;
    std::unordered_map<Id, std::shared_ptr<Order<T>>> orderList;
    std::vector<Trades> tradeHistory;
    Id currentId = 0;

    template<Side S>
    struct OrderComparator {
        constexpr bool operator()(std::shared_ptr<Order<T>> const &a, std::shared_ptr<Order<T>> const &b) const {
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

    void popHeap(Side side) {
        if (side == Side::Bid) {
            std::pop_heap(bids.begin(), bids.end(), OrderComparator<Side::Bid>());
            bids.pop_back();
        } else {
            std::pop_heap(asks.begin(), asks.end(), OrderComparator<Side::Ask>());
            asks.pop_back();
        }
    }

    std::optional<std::reference_wrapper<Order<T>>> getNextOrder(Side side) {
        auto& vec = (side == Side::Bid ? bids : asks);
        // grab the next order from the heap
        if (vec.empty()) {
           return std::nullopt;
        }

        std::shared_ptr<Order<T>> &nextOrder = vec.at(0);
        // While we don't have an active order, delete this one from orderList and the heap, and get the next order
        while (!vec.empty() && (!nextOrder->isActive() || nextOrder->isFilled())) {
            Id id = nextOrder->getOrderId();
            orderList.erase(id);
            popHeap(side);

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
        // Repeat this process until we have no more matching orders
        Side side = order.getOrderSide();
        Side oppSide = (side == Side::Bid) ? Side::Ask : Side::Bid;
        Price price = order.getOrderPrice();
        Trades trades;

        // Continue matching if there is any left over quantity, or if it cannot match then add it to the order book
        while (order.getOrderQuantity() > 0) {

            auto nextOrder = getNextOrder(oppSide);
            // No order to match against, so we break and add the order
            if (!nextOrder.has_value()){ break; }
            Order<T> &matchingOrder = nextOrder->get();

            Price tradePrice = matchingOrder.getOrderPrice();

            bool priceMatches = (side == Side::Bid)
            ? price >= tradePrice
            : price <= tradePrice;

            // Break if there is no matching price
            if (!priceMatches) { break; }

            // subtract the quantity from both orders
            Quantity quantity = (order.getOrderQuantity() < matchingOrder.getOrderQuantity() ? order.getOrderQuantity() : matchingOrder.getOrderQuantity());
            
            // add the trade to our trades
            if (side == Side::Bid) {
                trades.push_back(Trade<Quantity>(tradePrice, quantity, order, matchingOrder));
            } else {
                trades.push_back(Trade<Quantity>(tradePrice, quantity, matchingOrder, order));
            }

            order.fillOrder(quantity);
            matchingOrder.fillOrder(quantity);
            
            // if the order we matched with is empty, remove it from the heap & orderList
            if (matchingOrder.getOrderQuantity() == 0) {
                orderList.erase(matchingOrder.getOrderId());
                popHeap(oppSide);
            }
        }
        return trades;
    }

    void printOrders(const OrderBookView<T> &orders) const {
        for (const auto &order : orders){
            std::cout << order << "\n";   
        }
    }

    Id _addOrder(Order<T> order) {
        order.setOrderTimestamp(std::chrono::system_clock::now());
        Id id = ++currentId;
        Side side = order.getOrderSide();
        order.setOrderId(id);
        
        Trades trades = matchOrder(order);

        // This is just for testing/observation
        for (auto &trade : trades) {
            std::cout << trade << std::endl;
        }
        
        // If the order is not completely filled after matching, add it to the orderBook
        if (!order.isFilled()) {
            auto orderPtr = std::make_shared<Order<T>>(order);
            orderList.emplace(id, orderPtr);
            // Add the order to bids/asks and maintain the heaps
            if (side == Side::Bid) {
                bids.push_back(orderPtr);
                std::push_heap(bids.begin(), bids.end(), OrderComparator<Side::Bid>());
            } else {
                asks.push_back(orderPtr);
                std::push_heap(asks.begin(), asks.end(), OrderComparator<Side::Ask>());
            }
        }
        tradeHistory.push_back(trades);
        return id;
    }

    OrderBookView<T> buildOBView(Side side) const {
        auto sorted = std::make_shared<std::vector<std::shared_ptr<Order<T>>>>();

        const auto &source = (side == Side::Bid) ? bids : asks;
        for (const auto& order : source) {
            // Check that the order is active and not filled before adding it to our view. Lazy deletion means we may have canceled or filled orders in the heaps.
            if (order->isActive() && !order->isFilled()) {
                sorted->push_back(order);
            }
        }

        // Sort using the proper comparator, resued from the heap comparator but using a lambda to flip the arguemnts
        if (side == Side::Bid) {
            std::sort(sorted->begin(), sorted->end(), 
                [](auto const &a, auto const &b){
                    return OrderComparator<Side::Bid>()(b, a);
                }
            );
        } else {
            std::sort(sorted->begin(), sorted->end(), 
                [](auto const &a, auto const &b){
                    return OrderComparator<Side::Ask>()(b, a);
                }
            );
        }

        return OrderBookView<T>{sorted};
    }

    public:
    OrderBook() = default;

    // Helper function for _addOrder(), creates an order and then calls _addOrder on it
    Id addOrder(OrderType type, Side side, Price price, T quantity) {
        if (price == 0 || quantity == 0 ){
            throw std::logic_error("Cannot add order with price or quantity less than or equal to 0");
        }
        Order<T> order(type, side, price, quantity);
        Id id = _addOrder(order);
        return id;
    }

    // Gets the necessary info from the original order, and then cancels the old order and creates a new one. 
    Id modifyOrder(Id id, Price newPrice, Quantity newQuant) {
        auto orderLoc = orderList.find(id);
        if (orderLoc == orderList.end()) {
            throw std::logic_error("Cannot modify non-existing order");
        }
        Order<T> &order = *(orderLoc->second);
        // Tried to edit a non active or filled order, return 0;
        if (!order.isActive() || order.isFilled()) { return 0; }
        OrderType type = order.getOrderType();
        Side side = order.getOrderSide();
        cancelOrder(id);
        Order<T> nOrder(type, side, newPrice, newQuant);
        Id nID = _addOrder(nOrder);
        return nID;
    }
    
    // Marks an order as canceled, so that when it is visited by matchOrder() it will be removed (lazy deletion)
    void cancelOrder(Id id) {
        orderList.at(id)->cancelOrder();
    }

    OrderBookView<T> bidsView() const { return buildOBView(Side::Bid); }
    OrderBookView<T> asksView() const { return buildOBView(Side::Ask); }

    // These now rely on OrderBookView
    void printBids() const {
        std::cout << "Bids:" << std::endl;
        printOrders(bidsView());
    }

    void printAsks() const {
        std::cout << "Asks:" << std::endl;
        printOrders(asksView());
    }

    void printTradeHistory() {};

};


#endif