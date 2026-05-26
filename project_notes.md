## Motivation and Problem Statement
I wanted to create an order book and matching engine so that I could better understand the technology behind a tool that I interact with frequently. I also wanted to work on something for my final project that would be both complicated yet feasable, as well as something I can iteratively improve on and make faster.

## Process
1) Learn about order books and matching engines
 - I spent a lot of time reviewing different sources and understanding how order books, orders, and the matching algorithm all come together to form the complete architecture.
2) write my findings here and start thinking about architecture
 - Most of my initial notes ended up in a physical notebook as I was reading about order books and matching engines, and I may transcribe them here later.

## System Design and Architecture
We need objects to represent orders (bid and ask), the order book, and the order history.

I am currently planning to only tackle "good till canceled" orders, although I may expand on this in the future with other types such as "fill or kill" THIS DOESNT MAKE SENSE >>>> and "immediate or cancel". I also am only dealing with integer shares at the moment, but the orders are templatized so handling fractional shares should be an obvious next step.

We start with an order book. The order book has a min_heap for ask orders, and a max_heap for bid orders. This allows us to keep track of the lowest prices users are willing to sell at, as well as the highest prices users are willing to pay. We also keep a unordered_map connecting order ids to orders.

When an order is created it initially has no id or timestamp. These only become properly initialized (id is initiallized with a sentinal value for security) when the order is added to the order book. 

When an order is added to the order book, an id and timestamp are added to it. The id is monotonically increasing, and represents the order in which orders are added to the book. We can use this to compare orders for priority, with earlier orders taking precidence. 

After initializing id and timestamp, the order book checks to see if it is possible to match the new order with an existing order. If it is possible to match the order then we call matchOrder(), and get back a vector of trades. If there is any remaining quantity to the current order, it is then added to the book. If the order cannot match, it is simply added to the correct side.

I have read online about different approaches to data structures for order storage. At the moment I am using vectors as heaps, but I may change this in the future. 


## Matching Algorithm

I am planning to use a price time priority algorithm, or FIFO. This means that orders will be prioritized by price first, and then order time (represented by id). For example: if a sell order comes in that can match, and two buy orders are both at the lowest price, the buy order with the smaller id will be used.

## TODO

This was created after I had made significant progress in the orders, orderbook, trades, and matching engine. It is not a complete list of tasks.

- [x] cancel order function
- [x] match order function
- [x] can match function
- [x] fix comparator for bid/ask heaps
- [x] finish trades struct
- [x] get next order function
- [x] add modifyOrder() function
- [x] change addOrder() to take in args and construct the order, rather than requiring a new order
- [ ] fix main.c to use the new addOrder() syntax
- [ ] write unit tests
- [ ] level 1 data available
- [ ] level 2 data available
- [ ] get testing data / implement data pipeline
- [ ] do testing
- [ ] benchmark/profiling and project write up 
- [ ] add different types of orders
- [ ] level 3 data available  / Maybe not, we will see

## Evolution of the Design

There are many fully built examples of order books and matching engines online, but I would like to approach it without using them as a framework. I will look to examples if I get stuck, but that means I will probably be making some mistakes / inefficient design choices. 

I will document changes here.

1. Added timestamp to represent when an order was added.
2. Changed from using a list to using vectors to represent the orderbook sides. I decided to use vectors and heapify them.
3. Changed id to be created when an order was added, instead of an argument to the order constructor
4. Changed id to be the main comparison of time instead of timestamp, because timestamp was finicky
5. I was originally using a spaceship operator overload for comparison between orders, but I switched to a templatized struct comparator for bids and asks because this was not working for the min_heap comparisons.
6. I'm trying to decide if matchOrder() should use cancel order or have its own logic for removing the top order. I think it is fundamentally different from cancelOrder so I am leaning towards using its own logic. 
7. I decided to keep the logic seperate, and matchOrder has its own remove. I also read online and found a stack overflow post where someone recommended keeping a set of deleted elements, and if you try to access the top element in the heap and it has previously been deleted, you remove it and keep going. This avoids having to remove arbitrary elements from the heaps. (stack post is linked #7 in references)
8. Creating a getNextOrder() function that is responsible for getting the top order and handling deleted orders in the heap.
9. Added status field to Order, initialized to 1. Added cancelOrder() Order member method which sets status to 0, as well as isActive() which returns status. This allows us to check if orders are active or canceled. When an order is canceled we just set active to 0, and then if we ever run into that order again it is removed from the system at that time.
10. Changed getNextOrder() to return an optional<\reference_wrapper<\Order<\T>>> so that if there is no correct next value that information propogates up instead of throwing a logic error like it did before.
11. Removed canMatch() because it is now redundant, getNextOrder() does the error checking and we can just check for a price match in matchOrder()
12. Added tradeHistory member var to OrderBook, so that we have a complete list of trades. 
13. Changed addOrder() return type from Trades to Id, now it returns the id of the added order (so users can get that info and then modify/cancel orders). Trades completed in addOrder() are added to tradeHistory.
14. As I was working on modify order, I decided the best way to handle this was to just cancel the existing order and create a new one. This avoids a lot of the complex logic related to modifying the heap to deal with a price change, matching the order if it can now match, etc. 
15. Finished modifyOrder(), now I'm thinking I should change addOrder() to only require the args to construct an order, rather than take in a constructed order. I might make a helper function that is exposed to users create an order with their order info and then that helper passes addOrder() a constructed order


 
## Performance and Testing




## What I would change if I did it again / Future Work

Anything on the Todo list that has not been completed is part of future work. 


## References

1. https://cppforquants.com/whats-a-trading-order-book/
2. https://www.binance.com/en/academy/articles/understanding-matching-engines-in-trading
3. https://en.wikipedia.org/wiki/Order_matching_system
4. https://www.youtube.com/watch?v=XeLWe0Cx_Lg&t=1989s
5. https://gist.github.com/halfelf/db1ae032dc34278968f8bf31ee999a25
6. https://stackoverflow.com/questions/77116565/how-to-overload-spaceship-operator-with-reversed-order
7. https://stackoverflow.com/questions/12570981/how-to-remove-an-arbitrary-element-from-a-standard-heap-in-c
