#pragma once
#include<cstdint>

enum class Side
{
    Buy,
    Sell
};

enum class OrderType
{
    Limit,
    Market,
    IOC // Immediate or Cancel
};

using OrderId = std::uint64_t;
using Price = std::int64_t;
using Quantity = std::int64_t;
using Timestamp = std::uint64_t;
// A single order, either incoming (about to be matched) or resting in the book.
//
// Ownership of fields:
//  - Caller sets: id, side, type, price, quantity
//  - OrderBook sets: timestamp (OVERWRITTEN on arrival inside addOrder() —
//    the engine is the single source of truth for arrival sequence, not
//    the caller;

struct Order
{
    OrderId id;
    OrderType type;
    Side side;
    Price price; // ignored for Market orders
    Quantity quantity;
    Timestamp timestamp; // set by OrderBook on arrival

    Order(OrderId id, OrderType type, Side side, Price price, Quantity quantity)
        : id(id), type(type), side(side), price(price), quantity(quantity), timestamp(0) {}
};



