#pragma once
#include<map>
#include<deque>
#include<unordered_map>
#include<vector>
#include<functional>
#include "Order.h"
#include "Trade.h"

class OrderBook
{
public:
    OrderBook() = default;
    std::vector<Trade> addOrder(Order order);
    bool cancelOrder(OrderId orderId);
    bool bestBid(Price& outPrice,Quantity& outQty) const;
    bool bestAsk(Price& outPrice,Quantity& outQty) const;

private:
// Inserts `order` (whatever quantity remains) into its own side's book
    // at the back of its price level's deque, and records its location in
    // locations_. Only called for Limit orders with quantity > 0 remaining.
    void rest(const Order& order);
 
    // Bids sorted descending (best bid = highest price) -> begin() is best.
    // Asks sorted ascending (best ask = lowest price) -> begin() is best.
    // Each price level is a FIFO queue -> time priority within a level.
    std::map<Price, std::deque<Order>, std::greater<Price>> bids_;
    std::map<Price, std::deque<Order>>                      asks_;
 
    // For O(1)-ish cancel lookup: which side + price level an order id lives in.
    struct OrderLocation {
        Side  side;
        Price price;
    };
    std::unordered_map<OrderId, OrderLocation> locations_;
 
    // Single source of truth for arrival sequence — see Order.h. Assigned
    // to order.timestamp at the very start of addOrder(), then incremented.
    Timestamp nextTimestamp_ = 0;
};