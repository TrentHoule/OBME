#include <iostream>
#include "orderBook.h"


int main() {
    OrderBook<Quantity> QQQ_OB{};
    Price bidPrice = 2000;
    Price askPrice = 1000;
    Quantity amnt = 10;

    QQQ_OB.addOrder(OrderType::goodTillCanceled, Side::Bid, 1000, 5);
    QQQ_OB.addOrder(OrderType::goodTillCanceled, Side::Ask, 2000, 10);
    Id id = QQQ_OB.addOrder(OrderType::goodTillCanceled, Side::Bid, 1500, 105);
    std::cout << id << std::endl;
    QQQ_OB.cancelOrder(id);
    QQQ_OB.modifyOrder(id, 1100, 12);
    QQQ_OB.addOrder(OrderType::goodTillCanceled, Side::Ask, 2500, 24);
    Id id2 = QQQ_OB.addOrder(OrderType::goodTillCanceled, Side::Ask, 1000, 11);
    QQQ_OB.addOrder(OrderType::goodTillCanceled, Side::Bid, 2100, 6);

    for (int i = 0; i < 10; i ++) {
        QQQ_OB.addOrder(OrderType::goodTillCanceled, Side::Bid, 1000 + (100 * (i % 3)), 5 + i);
        QQQ_OB.addOrder(OrderType::goodTillCanceled, Side::Ask, 1000 + (100 * (i % 7)), 5 + i);
    }
    QQQ_OB.cancelOrder(26);

    
    std::cout << std::endl;
    QQQ_OB.printAsks();
    std::cout << std::endl;
    QQQ_OB.printBids();




    return 0;
}