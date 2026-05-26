#include <iostream>
#include "orderBook.h"


int main() {
    OrderBook<Quantity> QQQ_OB{};
    Price bidPrice = 2000;
    Price askPrice = 1000;
    Quantity amnt = 10;

    QQQ_OB.addOrder(OrderType::goodTillCanceled, Side::Bid, 1000, 5);
    QQQ_OB.addOrder(OrderType::goodTillCanceled, Side::Ask, 2000, 10);
    QQQ_OB.addOrder(OrderType::goodTillCanceled, Side::Bid, 1500, 15);
    // QQQ_OB.cancelOrder(order3.getOrderId());
    QQQ_OB.addOrder(OrderType::goodTillCanceled, Side::Ask, 2500, 24);
    QQQ_OB.addOrder(OrderType::goodTillCanceled, Side::Ask, 1000, 11);
    QQQ_OB.addOrder(OrderType::goodTillCanceled, Side::Bid, 2100, 6);


    // QQQ_OB.printOrderList();

    
    
    QQQ_OB.printAsks();
    std::cout << std::endl;
    QQQ_OB.printBids();




    return 0;
}