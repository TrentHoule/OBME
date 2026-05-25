#include <iostream>
#include "orderBook.h"


int main() {
    OrderBook<Quantity> QQQ_OB{};
    Price bidPrice = 2000;
    Price askPrice = 1000;
    Quantity amnt = 10;

    Order<Quantity> order1(OrderType::goodTillCanceled, Side::Bid, 1000, 5);
    Order<Quantity> order2(OrderType::goodTillCanceled, Side::Ask, 2000, 10);
    Order<Quantity> order3(OrderType::goodTillCanceled, Side::Bid, 1500, 15);
    Order<Quantity> order4(OrderType::goodTillCanceled, Side::Ask, 2500, 24);
    Order<Quantity> order5(OrderType::goodTillCanceled, Side::Ask, 1000, 11);
    Order<Quantity> order6(OrderType::goodTillCanceled, Side::Bid, 2100, 6);

    QQQ_OB.addOrder(order1);
    QQQ_OB.addOrder(order2);
    QQQ_OB.addOrder(order3);
    // QQQ_OB.cancelOrder(order3.getOrderId());
    QQQ_OB.addOrder(order4);
    QQQ_OB.addOrder(order5);
    QQQ_OB.addOrder(order6);


    // QQQ_OB.printOrderList();

    
    
    QQQ_OB.printAsks();
    std::cout << std::endl;
    QQQ_OB.printBids();




    return 0;
}