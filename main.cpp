#include <iostream>
#include "OrderBook.h"


// If every "ok" line prints and nothing fails, the implementation matches
// the design.

static void printTrade(const Trade& t) {
    std::cout << "  Trade: resting=" << t.restingOrderId
              << " incoming=" << t.incomingOrderId
              << " price=" << t.price
              << " qty=" << t.quantity << "\n";
}

#define CHECK(cond) do { \
    if (!(cond)) { \
        std::cerr << "CHECK FAILED (line " << __LINE__ << "): " #cond "\n"; \
        return 1; \
    } else { \
        std::cout << "  ok: " #cond "\n"; \
    } \
} while (0)

int main() {
    std::cout << "=== design_notes.md Section 5 worked example: matching ===\n\n";
    OrderBook book;
    Price p; Quantity q;

    std::cout << "Step 1: Order A - Buy Limit price=100 qty=10\n";
    auto t1 = book.addOrder(Order(1, Side::Buy, OrderType::Limit, 100, 10));
    CHECK(t1.empty());
    CHECK(book.bestBid(p, q) && p == 100 && q == 10);
    CHECK(!book.bestAsk(p, q));

    std::cout << "\nStep 2: Order B - Sell Limit price=101 qty=5\n";
    auto t2 = book.addOrder(Order(2, Side::Sell, OrderType::Limit, 101, 5));
    CHECK(t2.empty());
    CHECK(book.bestAsk(p, q) && p == 101 && q == 5);

    std::cout << "\nStep 3: Order C - Buy Limit price=102 qty=8 (crosses B)\n";
    auto t3 = book.addOrder(Order(3, Side::Buy, OrderType::Limit, 102, 8));
    for (const auto& t : t3) printTrade(t);
    CHECK(t3.size() == 1);
    CHECK(t3[0].restingOrderId == 2 && t3[0].incomingOrderId == 3);
    CHECK(t3[0].price == 101 && t3[0].quantity == 5); // price improvement: resting price, not 102
    CHECK(!book.bestAsk(p, q)); // B fully consumed, asks_ empty
    CHECK(book.bestBid(p, q) && p == 102 && q == 3);  // C's remainder rests

    std::cout << "\nStep 4: Order D - Sell Market qty=20 (sweeps both bid levels)\n";
    auto t4 = book.addOrder(Order(4, Side::Sell, OrderType::Market, 0, 20));
    for (const auto& t : t4) printTrade(t);
    CHECK(t4.size() == 2);
    CHECK(t4[0].restingOrderId == 3 && t4[0].price == 102 && t4[0].quantity == 3);
    CHECK(t4[1].restingOrderId == 1 && t4[1].price == 100 && t4[1].quantity == 10);
    CHECK(!book.bestBid(p, q) && !book.bestAsk(p, q)); // book fully empty
    // D wanted 20, only 13 were available (3 + 10) -> remaining 7 is a Market
    // order, so it's discarded rather than resting. Book being empty proves it.

    std::cout << "\n=== design_notes.md Section 6 worked example: cancel ===\n\n";
    OrderBook book2;
    book2.addOrder(Order(10, Side::Buy, OrderType::Limit, 100, 10)); // like A
    book2.addOrder(Order(11, Side::Sell, OrderType::Limit, 101, 5)); // like B
    book2.addOrder(Order(12, Side::Buy, OrderType::Limit, 102, 8));  // like C, matches B, rests qty=3

    std::cout << "Cancelling order 10 (the Buy@100 resting order)\n";
    CHECK(book2.cancelOrder(10) == true);
    CHECK(book2.bestBid(p, q) && p == 102 && q == 3); // only order 12's remainder left
    CHECK(book2.cancelOrder(10) == false); // already gone, second cancel fails

    std::cout << "\nAll checks passed - implementation matches design_notes.md.\n";
    return 0;
}