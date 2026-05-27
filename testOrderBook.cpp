/**
 * test_orderbook.cpp
 *
 * Each TEST() block is registered automatically and run in main().
 * Output: PASS / FAIL per test, with a summary at the end.
 * 
 * I generated these using claude after telling it what I wanted to test, and manually reviewed all the tests.
 * 
 */

#include <iostream>
#include <stdexcept>
#include <vector>
#include <string>
#include <functional>
#include "orderBook.h"

// ─────────────────────────────────────────────────────────────────────────────
//  Minimal test framework
// ─────────────────────────────────────────────────────────────────────────────

struct TestCase {
    std::string name;
    std::function<void()> fn;
};

static std::vector<TestCase> g_tests;

// Registers a test before main() runs via a static initialiser trick
struct TestRegistrar {
    TestRegistrar(const char* name, std::function<void()> fn) {
        g_tests.push_back({name, std::move(fn)});
    }
};

// Macro to define & auto-register a test
#define TEST(name)                                              \
    static void test_##name();                                  \
    static TestRegistrar reg_##name(#name, test_##name);        \
    static void test_##name()

// Assertion helpers — throw on failure so the rest of the test is skipped
#define ASSERT_TRUE(expr)                                                           \
    do {                                                                            \
        if (!(expr)) {                                                              \
            throw std::runtime_error(                                               \
                std::string("ASSERT_TRUE failed: ") + #expr +                      \
                " (" __FILE__ ":" + std::to_string(__LINE__) + ")");               \
        }                                                                           \
    } while (false)

#define ASSERT_FALSE(expr) ASSERT_TRUE(!(expr))

#define ASSERT_EQ(a, b)                                                             \
    do {                                                                            \
        auto _a = (a); auto _b = (b);                                              \
        if (_a != _b) {                                                             \
            throw std::runtime_error(                                               \
                std::string("ASSERT_EQ failed: ") + #a + " != " + #b +            \
                " (got " + std::to_string(_a) + " vs " + std::to_string(_b) +     \
                ") (" __FILE__ ":" + std::to_string(__LINE__) + ")");              \
        }                                                                           \
    } while (false)

#define ASSERT_THROWS(expr, ExType)                                                 \
    do {                                                                            \
        bool _threw = false;                                                        \
        try { expr; } catch (const ExType&) { _threw = true; }                    \
        if (!_threw) {                                                              \
            throw std::runtime_error(                                               \
                std::string("ASSERT_THROWS failed: ") + #expr +                   \
                " did not throw " #ExType                                          \
                " (" __FILE__ ":" + std::to_string(__LINE__) + ")");              \
        }                                                                           \
    } while (false)

// ─────────────────────────────────────────────────────────────────────────────
//  Small helper: count the orders visible in a view
// ─────────────────────────────────────────────────────────────────────────────

template<typename T>
size_t countOrders(const OrderBookView<T>& view) {
    size_t n = 0;
    for ([[maybe_unused]] const auto& _ : view) ++n;
    return n;
}

// Returns the price of the nth order (0-indexed) when iterating the view.
template<typename T>
Price nthPrice(const OrderBookView<T>& view, size_t n) {
    size_t i = 0;
    for (const auto& order : view) {
        if (i++ == n) return order.getOrderPrice();
    }
    throw std::out_of_range("nthPrice: index out of range");
}

template<typename T>
Quantity nthQuantity(const OrderBookView<T>& view, size_t n) {
    size_t i = 0;
    for (const auto& order : view) {
        if (i++ == n) return order.getOrderQuantity();
    }
    throw std::out_of_range("nthQuantity: index out of range");
}


// =============================================================================
//  1. CONSTRUCTION
// =============================================================================

TEST(EmptyBookHasNoOrders) {
    OrderBook<Quantity> book;
    ASSERT_EQ(countOrders(book.bidsView()), 0u);
    ASSERT_EQ(countOrders(book.asksView()), 0u);
}


// =============================================================================
//  2. ADDING ORDERS — basic
// =============================================================================

TEST(AddBidAppearsInBids) {
    OrderBook<Quantity> book;
    book.addOrder(OrderType::goodTillCanceled, Side::Bid, 100, 10);
    ASSERT_EQ(countOrders(book.bidsView()), 1u);
    ASSERT_EQ(countOrders(book.asksView()), 0u);
}

TEST(AddAskAppearsInAsks) {
    OrderBook<Quantity> book;
    book.addOrder(OrderType::goodTillCanceled, Side::Ask, 100, 10);
    ASSERT_EQ(countOrders(book.asksView()), 1u);
    ASSERT_EQ(countOrders(book.bidsView()), 0u);
}

TEST(AddOrderReturnsUniqueIds) {
    OrderBook<Quantity> book;
    Id id1 = book.addOrder(OrderType::goodTillCanceled, Side::Bid, 100, 10);
    Id id2 = book.addOrder(OrderType::goodTillCanceled, Side::Bid, 101, 10);
    Id id3 = book.addOrder(OrderType::goodTillCanceled, Side::Ask, 105, 10);
    // All three IDs should be distinct
    ASSERT_TRUE(id1 != id2);
    ASSERT_TRUE(id2 != id3);
    ASSERT_TRUE(id1 != id3);
}

TEST(AddOrderWithZeroPriceThrows) {
    OrderBook<Quantity> book;
    ASSERT_THROWS(
        book.addOrder(OrderType::goodTillCanceled, Side::Bid, 0, 10),
        std::logic_error
    );
}

TEST(AddOrderWithZeroQuantityThrows) {
    OrderBook<Quantity> book;
    ASSERT_THROWS(
        book.addOrder(OrderType::goodTillCanceled, Side::Bid, 100, 0),
        std::logic_error
    );
}


// =============================================================================
//  3. HEAP / PRIORITY ORDERING
//
//  Bids: highest price at the front of the view.
//  Asks: lowest price at the front of the view.
//  Tie-break: lower Id (i.e. earlier order) has priority.
// =============================================================================

TEST(BidsOrderedHighestPriceFirst) {
    OrderBook<Quantity> book;
    book.addOrder(OrderType::goodTillCanceled, Side::Bid, 100, 10);
    book.addOrder(OrderType::goodTillCanceled, Side::Bid, 103, 10);
    book.addOrder(OrderType::goodTillCanceled, Side::Bid, 101, 10);

    auto view = book.bidsView();
    ASSERT_EQ(countOrders(view), 3u);
    ASSERT_EQ(nthPrice(view, 0), Price(103));
    ASSERT_EQ(nthPrice(view, 1), Price(101));
    ASSERT_EQ(nthPrice(view, 2), Price(100));
}

TEST(AsksOrderedLowestPriceFirst) {
    OrderBook<Quantity> book;
    book.addOrder(OrderType::goodTillCanceled, Side::Ask, 105, 10);
    book.addOrder(OrderType::goodTillCanceled, Side::Ask, 102, 10);
    book.addOrder(OrderType::goodTillCanceled, Side::Ask, 107, 10);

    auto view = book.asksView();
    ASSERT_EQ(countOrders(view), 3u);
    ASSERT_EQ(nthPrice(view, 0), Price(102));
    ASSERT_EQ(nthPrice(view, 1), Price(105));
    ASSERT_EQ(nthPrice(view, 2), Price(107));
}

TEST(SamePriceTieBrokenByEarlierId) {
    // Two bids at the same price — the earlier one (lower Id) should come first.
    OrderBook<Quantity> book;
    Id first  = book.addOrder(OrderType::goodTillCanceled, Side::Bid, 100, 10);
    Id second = book.addOrder(OrderType::goodTillCanceled, Side::Bid, 100, 10);

    auto view = book.bidsView();
    ASSERT_EQ(countOrders(view), 2u);

    // The first order added should appear first in the view.
    size_t i = 0;
    Id frontId = 0;
    for (const auto& order : view) {
        if (i++ == 0) frontId = order.getOrderId();
    }
    // first < second because IDs increment
    ASSERT_TRUE(first < second);
    ASSERT_EQ(frontId, first);
}


// =============================================================================
//  4. ORDER MATCHING
// =============================================================================

TEST(NoMatchWhenBidBelowAsk) {
    // Bid at 99, Ask at 100 — prices don't cross, nothing should trade.
    OrderBook<Quantity> book;
    book.addOrder(OrderType::goodTillCanceled, Side::Ask, 100, 10);
    book.addOrder(OrderType::goodTillCanceled, Side::Bid,  99, 10);

    // Both orders should remain on the book
    ASSERT_EQ(countOrders(book.asksView()), 1u);
    ASSERT_EQ(countOrders(book.bidsView()), 1u);
}

TEST(ExactMatchRemovesBothOrders) {
    // Bid == Ask price, equal quantity → both fully filled, book is empty afterwards.
    OrderBook<Quantity> book;
    book.addOrder(OrderType::goodTillCanceled, Side::Ask, 100, 10);
    book.addOrder(OrderType::goodTillCanceled, Side::Bid, 100, 10);

    ASSERT_EQ(countOrders(book.asksView()), 0u);
    ASSERT_EQ(countOrders(book.bidsView()), 0u);
}

TEST(BidAboveAskMatchesAtAskPrice) {
    // Bid at 105 vs Ask at 100 — should match; the remaining bid resting quantity is 0.
    OrderBook<Quantity> book;
    book.addOrder(OrderType::goodTillCanceled, Side::Ask, 100, 10);
    book.addOrder(OrderType::goodTillCanceled, Side::Bid, 105, 10);

    ASSERT_EQ(countOrders(book.asksView()), 0u);
    ASSERT_EQ(countOrders(book.bidsView()), 0u);
}

TEST(PartialFillLeavesResidualOnBook) {
    // Ask for 10 shares, Bid for only 6 — Ask should have 4 left on the book.
    OrderBook<Quantity> book;
    book.addOrder(OrderType::goodTillCanceled, Side::Ask, 100, 10);
    book.addOrder(OrderType::goodTillCanceled, Side::Bid, 100,  6);

    ASSERT_EQ(countOrders(book.bidsView()), 0u);  // bid fully consumed
    ASSERT_EQ(countOrders(book.asksView()), 1u);  // ask still live
    ASSERT_EQ(nthQuantity(book.asksView(), 0), Quantity(4));
}

TEST(LargeBidConsumesMultipleAsks) {
    // Incoming bid at 110 qty=30 should sweep three asks of 10 each at 100/102/104.
    OrderBook<Quantity> book;
    book.addOrder(OrderType::goodTillCanceled, Side::Ask, 100, 10);
    book.addOrder(OrderType::goodTillCanceled, Side::Ask, 102, 10);
    book.addOrder(OrderType::goodTillCanceled, Side::Ask, 104, 10);

    book.addOrder(OrderType::goodTillCanceled, Side::Bid, 110, 30);

    ASSERT_EQ(countOrders(book.asksView()), 0u);
    ASSERT_EQ(countOrders(book.bidsView()), 0u);
}

TEST(LargeBidPartiallyConsumesAsks) {
    // Bid qty=25 against three asks of qty=10 each — first two fully consumed, third has 5 left.
    OrderBook<Quantity> book;
    book.addOrder(OrderType::goodTillCanceled, Side::Ask, 100, 10);
    book.addOrder(OrderType::goodTillCanceled, Side::Ask, 102, 10);
    book.addOrder(OrderType::goodTillCanceled, Side::Ask, 104, 10);

    book.addOrder(OrderType::goodTillCanceled, Side::Bid, 110, 25);

    ASSERT_EQ(countOrders(book.asksView()), 1u);
    ASSERT_EQ(nthPrice(book.asksView(), 0), Price(104));
    ASSERT_EQ(nthQuantity(book.asksView(), 0), Quantity(5));
    ASSERT_EQ(countOrders(book.bidsView()), 0u);
}

TEST(UnfilledResidualOfIncomingOrderRestOnBook) {
    // Bid qty=15 against one Ask qty=10 — 5 units of the bid should rest on the book.
    OrderBook<Quantity> book;
    book.addOrder(OrderType::goodTillCanceled, Side::Ask, 100, 10);
    book.addOrder(OrderType::goodTillCanceled, Side::Bid, 100, 15);

    ASSERT_EQ(countOrders(book.asksView()), 0u);
    ASSERT_EQ(countOrders(book.bidsView()), 1u);
    ASSERT_EQ(nthQuantity(book.bidsView(), 0), Quantity(5));
}

TEST(AskMatchesBestBidFirst) {
    // Two bids at different prices; incoming ask should match the higher one first.
    OrderBook<Quantity> book;
    book.addOrder(OrderType::goodTillCanceled, Side::Bid, 105, 10);  // best bid
    book.addOrder(OrderType::goodTillCanceled, Side::Bid, 100, 10);  // second bid

    // Ask qty=10 at 100 — only matches the best bid (105), leaving the 100-bid alive.
    book.addOrder(OrderType::goodTillCanceled, Side::Ask, 100, 10);

    ASSERT_EQ(countOrders(book.asksView()), 0u);
    ASSERT_EQ(countOrders(book.bidsView()), 1u);
    ASSERT_EQ(nthPrice(book.bidsView(), 0), Price(100));  // 105 bid was consumed
}

TEST(ViewIsASnapshotNotALiveReference) {
    OrderBook<Quantity> book;
    book.addOrder(OrderType::goodTillCanceled, Side::Bid, 100, 10);

    // Capture the view *before* the second order is added
    auto snapshot = book.bidsView();

    book.addOrder(OrderType::goodTillCanceled, Side::Bid, 101, 10);

    // The snapshot should still only see 1 order
    ASSERT_EQ(countOrders(snapshot), 1u);
    // But a fresh view sees 2
    ASSERT_EQ(countOrders(book.bidsView()), 2u);
}

// =============================================================================
//  5. CANCEL ORDER
// =============================================================================

TEST(CancelOrderRemovesFromView) {
    OrderBook<Quantity> book;
    Id id = book.addOrder(OrderType::goodTillCanceled, Side::Bid, 100, 10);

    book.cancelOrder(id);
    ASSERT_EQ(countOrders(book.bidsView()), 0u);
}

TEST(CancelNonExistentOrderDoesNotThrow) {
    // Cancelling an ID that was never added should be a no-op, not a crash.
    OrderBook<Quantity> book;
    book.cancelOrder(9999);  // should not throw
}

TEST(CancelledOrderNotMatchedByIncomingOrder) {
    // Add an ask, cancel it, then add a crossing bid — no match should occur.
    OrderBook<Quantity> book;
    Id askId = book.addOrder(OrderType::goodTillCanceled, Side::Ask, 100, 10);
    book.cancelOrder(askId);

    book.addOrder(OrderType::goodTillCanceled, Side::Bid, 100, 10);

    // Ask is gone, bid should rest on the book unfilled
    ASSERT_EQ(countOrders(book.asksView()), 0u);
    ASSERT_EQ(countOrders(book.bidsView()), 1u);
}

TEST(CancelOneOfManyOrders) {
    OrderBook<Quantity> book;
    book.addOrder(OrderType::goodTillCanceled, Side::Bid, 100, 10);
    Id target = book.addOrder(OrderType::goodTillCanceled, Side::Bid, 101, 10);
    book.addOrder(OrderType::goodTillCanceled, Side::Bid, 102, 10);

    book.cancelOrder(target);
    ASSERT_EQ(countOrders(book.bidsView()), 2u);

    // The two remaining bids should be at 102 and 100 (sorted high→low)
    ASSERT_EQ(nthPrice(book.bidsView(), 0), Price(102));
    ASSERT_EQ(nthPrice(book.bidsView(), 1), Price(100));
}

TEST(LazyDeletionSkipsMultipleConsecutiveStaleBids) {
    // Add 4 bids at different prices, cancel the top 3.
    // The surviving bid should still be matched correctly.
    OrderBook<Quantity> book;
    Id id1 = book.addOrder(OrderType::goodTillCanceled, Side::Bid, 104, 10);
    Id id2 = book.addOrder(OrderType::goodTillCanceled, Side::Bid, 103, 10);
    Id id3 = book.addOrder(OrderType::goodTillCanceled, Side::Bid, 102, 10);
              book.addOrder(OrderType::goodTillCanceled, Side::Bid, 101, 10); // survivor

    book.cancelOrder(id1);
    book.cancelOrder(id2);
    book.cancelOrder(id3);

    // An ask at 101 should find and match the surviving 101-bid,
    // walking past all three stale heap entries in the process.
    book.addOrder(OrderType::goodTillCanceled, Side::Ask, 101, 10);

    ASSERT_EQ(countOrders(book.bidsView()), 0u);
    ASSERT_EQ(countOrders(book.asksView()), 0u);
}

TEST(DoubleCancelIsNoOp) {
    OrderBook<Quantity> book;
    Id id = book.addOrder(OrderType::goodTillCanceled, Side::Bid, 100, 10);

    book.cancelOrder(id); // first cancel
    book.cancelOrder(id); // second cancel — should not crash or double-count

    ASSERT_EQ(countOrders(book.bidsView()), 0u);
}


// =============================================================================
//  6. MODIFY ORDER
// =============================================================================

TEST(ModifyOrderChangesPrice) {
    OrderBook<Quantity> book;
    Id id = book.addOrder(OrderType::goodTillCanceled, Side::Bid, 100, 10);

    Id newId = book.modifyOrder(id, 105, 10);
    ASSERT_TRUE(newId != 0);

    auto view = book.bidsView();
    ASSERT_EQ(countOrders(view), 1u);
    ASSERT_EQ(nthPrice(view, 0), Price(105));
}

TEST(ModifyOrderChangesQuantity) {
    OrderBook<Quantity> book;
    Id id = book.addOrder(OrderType::goodTillCanceled, Side::Bid, 100, 10);

    book.modifyOrder(id, 100, 25);

    auto view = book.bidsView();
    ASSERT_EQ(countOrders(view), 1u);
    ASSERT_EQ(nthQuantity(view, 0), Quantity(25));
}

TEST(ModifyNonExistentOrderThrows) {
    OrderBook<Quantity> book;
    ASSERT_THROWS(book.modifyOrder(9999, 100, 10), std::logic_error);
}

TEST(ModifyCancelledOrderReturnsZero) {
    // Modifying a cancelled order should return Id 0 (the sentinel for "nothing done").
    OrderBook<Quantity> book;
    Id id = book.addOrder(OrderType::goodTillCanceled, Side::Bid, 100, 10);
    book.cancelOrder(id);

    Id result = book.modifyOrder(id, 105, 10);
    ASSERT_EQ(result, Id(0));
}

TEST(ModifiedOrderGetsNewId) {
    // After a modify the old ID should no longer refer to a live order.
    OrderBook<Quantity> book;
    Id oldId = book.addOrder(OrderType::goodTillCanceled, Side::Bid, 100, 10);
    Id newId = book.modifyOrder(oldId, 105, 10);

    ASSERT_TRUE(newId != oldId);

    // Cancelling the old ID should be a no-op (it no longer exists)
    book.cancelOrder(oldId);
    ASSERT_EQ(countOrders(book.bidsView()), 1u);  // new order still live
}

TEST(ModifyPriceCanTriggerMatch) {
    // An existing bid at 99 (below ask at 100) shouldn't match.
    // Modifying it up to 100 should trigger a match and clear both sides.
    OrderBook<Quantity> book;
    book.addOrder(OrderType::goodTillCanceled, Side::Ask, 100, 10);
    Id bidId = book.addOrder(OrderType::goodTillCanceled, Side::Bid, 99, 10);

    ASSERT_EQ(countOrders(book.asksView()), 1u);
    ASSERT_EQ(countOrders(book.bidsView()), 1u);

    book.modifyOrder(bidId, 100, 10);

    ASSERT_EQ(countOrders(book.asksView()), 0u);
    ASSERT_EQ(countOrders(book.bidsView()), 0u);
}

TEST(ModifyAskOrderChangesPrice) {
    OrderBook<Quantity> book;
    Id id = book.addOrder(OrderType::goodTillCanceled, Side::Ask, 105, 10);
    book.modifyOrder(id, 110, 10);

    ASSERT_EQ(countOrders(book.asksView()), 1u);
    ASSERT_EQ(nthPrice(book.asksView(), 0), Price(110));
}

TEST(ModifyAskCanTriggerMatch) {
    // Ask at 105 won't match a bid at 100. Modify it down to 100, should match.
    OrderBook<Quantity> book;
    Id askId = book.addOrder(OrderType::goodTillCanceled, Side::Ask, 105, 10);
    book.addOrder(OrderType::goodTillCanceled, Side::Bid, 100, 10);

    ASSERT_EQ(countOrders(book.asksView()), 1u);

    book.modifyOrder(askId, 100, 10);

    ASSERT_EQ(countOrders(book.asksView()), 0u);
    ASSERT_EQ(countOrders(book.bidsView()), 0u);
}

TEST(ModifyOrderThatImmediatelyFullyMatchesReturnsValidIdButLeavesNoResidue) {
    OrderBook<Quantity> book;
    // Ask at 100 waiting to be filled
    book.addOrder(OrderType::goodTillCanceled, Side::Ask, 100, 10);
    // Bid at 99 — doesn't cross yet
    Id bidId = book.addOrder(OrderType::goodTillCanceled, Side::Bid, 99, 10);

    // Modify the bid up to 100 — it should fully match and vanish
    Id newId = book.modifyOrder(bidId, 100, 10);

    // A valid (non-zero) ID was returned even though the order was fully consumed
    ASSERT_TRUE(newId != Id(0));
    // Book should be completely empty
    ASSERT_EQ(countOrders(book.bidsView()), 0u);
    ASSERT_EQ(countOrders(book.asksView()), 0u);
    // The returned ID has no resting order — cancelling it is a no-op, not a throw
    book.cancelOrder(newId); // must not crash
}

TEST(ChainedModifiesProduceCorrectFinalState) {
    OrderBook<Quantity> book;
    Id id  = book.addOrder(OrderType::goodTillCanceled, Side::Bid, 100, 10);
    Id id2 = book.modifyOrder(id,  102, 20);
    Id id3 = book.modifyOrder(id2, 104, 30);

    ASSERT_EQ(countOrders(book.bidsView()), 1u);
    ASSERT_EQ(nthPrice(book.bidsView(),    0), Price(104));
    ASSERT_EQ(nthQuantity(book.bidsView(), 0), Quantity(30));

    // The final ID should be cancellable
    book.cancelOrder(id3);
    ASSERT_EQ(countOrders(book.bidsView()), 0u);
}

TEST(ModifyPartiallyFilledOrderWorks) {
    OrderBook<Quantity> book;

    // Ask for 20, bid for only 5 — ask has 15 remaining, still in orderList
    Id askId = book.addOrder(OrderType::goodTillCanceled, Side::Ask, 100, 20);
    book.addOrder(OrderType::goodTillCanceled, Side::Bid, 100, 5);

    ASSERT_EQ(nthQuantity(book.asksView(), 0), Quantity(15));

    // Modify the partially-filled ask to a new price and quantity
    Id newAskId = book.modifyOrder(askId, 102, 30);
    ASSERT_TRUE(newAskId != Id(0));

    ASSERT_EQ(countOrders(book.asksView()), 1u);
    ASSERT_EQ(nthPrice(book.asksView(),    0), Price(102));
    ASSERT_EQ(nthQuantity(book.asksView(), 0), Quantity(30));
}

// =============================================================================
//  7. ITERATOR
// =============================================================================

TEST(IteratorVisitsEveryOrder) {
    OrderBook<Quantity> book;
    book.addOrder(OrderType::goodTillCanceled, Side::Bid, 100, 10);
    book.addOrder(OrderType::goodTillCanceled, Side::Bid, 101, 20);
    book.addOrder(OrderType::goodTillCanceled, Side::Bid, 102, 30);

    size_t count = 0;
    Quantity totalQty = 0;
    for (const auto& order : book.bidsView()) {
        ++count;
        totalQty += order.getOrderQuantity();
    }

    ASSERT_EQ(count, 3u);
    ASSERT_EQ(totalQty, Quantity(60));
}

TEST(IteratorOnEmptyViewIsEmpty) {
    OrderBook<Quantity> book;
    size_t count = 0;
    for ([[maybe_unused]] const auto& _ : book.bidsView()) ++count;
    ASSERT_EQ(count, 0u);
}

TEST(IteratorArrowOperatorWorks) {
    OrderBook<Quantity> book;
    book.addOrder(OrderType::goodTillCanceled, Side::Ask, 105, 7);

    auto it = book.asksView().begin();
    ASSERT_EQ(it->getOrderPrice(), Price(105));
    ASSERT_EQ(it->getOrderQuantity(), Quantity(7));
}

TEST(IteratorPreAndPostIncrementAgree) {
    OrderBook<Quantity> book;
    book.addOrder(OrderType::goodTillCanceled, Side::Bid, 100, 1);
    book.addOrder(OrderType::goodTillCanceled, Side::Bid, 101, 2);

    auto view  = book.bidsView();
    auto preIt = view.begin();
    ++preIt;   // pre-increment

    auto postIt = view.begin();
    postIt++;  // post-increment (advances to same position)

    // Both should now point at the second element
    ASSERT_EQ(preIt->getOrderPrice(), postIt->getOrderPrice());
}


// =============================================================================
//  8. STRESS / EDGE CASES
// =============================================================================

TEST(ManyOrdersAllCancelled) {
    // Add many orders, cancel them all, the views should be empty.
    OrderBook<Quantity> book;
    std::vector<Id> ids;
    for (int i = 1; i <= 50; ++i) {
        ids.push_back(book.addOrder(OrderType::goodTillCanceled, Side::Bid, 100 + i, 10));
    }
    for (Id id : ids) book.cancelOrder(id);

    ASSERT_EQ(countOrders(book.bidsView()), 0u);
}

TEST(InterleavedBidsAndAsksMatchCorrectly) {
    // Build up a book with a spread, then add a crossing order and verify.
    OrderBook<Quantity> book;
    book.addOrder(OrderType::goodTillCanceled, Side::Bid, 99,  5);
    book.addOrder(OrderType::goodTillCanceled, Side::Bid, 98,  5);
    book.addOrder(OrderType::goodTillCanceled, Side::Ask, 101, 5);
    book.addOrder(OrderType::goodTillCanceled, Side::Ask, 102, 5);

    // A bid at 101 should match exactly the 101-ask; the 102-ask should remain.
    book.addOrder(OrderType::goodTillCanceled, Side::Bid, 101, 5);

    ASSERT_EQ(countOrders(book.asksView()), 1u);
    ASSERT_EQ(nthPrice(book.asksView(), 0), Price(102));
    ASSERT_EQ(countOrders(book.bidsView()), 2u);  // original 99 and 98 still live
}

TEST(OrderBookHandlesHighVolumeWithoutCorruption) {
    // Add 100 bids and 100 asks, all matching, book should end up empty.
    OrderBook<Quantity> book;
    for (int i = 0; i < 100; ++i) {
        book.addOrder(OrderType::goodTillCanceled, Side::Ask, 100, 1);
    }
    for (int i = 0; i < 100; ++i) {
        book.addOrder(OrderType::goodTillCanceled, Side::Bid, 100, 1);
    }
    ASSERT_EQ(countOrders(book.bidsView()), 0u);
    ASSERT_EQ(countOrders(book.asksView()), 0u);
}


// =============================================================================
//  Test runner
// =============================================================================

int main() {
    int passed = 0, failed = 0;

    for (const auto& t : g_tests) {
        try {
            t.fn();
            std::cout << "  PASS  " << t.name << "\n";
            ++passed;
        } catch (const std::exception& e) {
            std::cout << "  FAIL  " << t.name << "\n"
                      << "        " << e.what() << "\n";
            ++failed;
        }
    }

    std::cout << "\n────────────────────────────────\n"
              << "  Results: " << passed << " passed, " << failed << " failed"
              << " (total " << (passed + failed) << ")\n";

    return (failed == 0) ? 0 : 1;
}