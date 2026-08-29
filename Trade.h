#pragma once
#include "Order.h"

//A single execution of a trade took place when an incoming order was matched with a resting order

struct Trade
{
    OrderId restingOrderId;
    OrderId incomingOrderId;
    Price price; // trades executes at resting order price
    Quantity quantity;
    Timestamp timestamp; 

    Trade(OrderId restingOrderId_, OrderId incomingOrderId_, Price price_, Quantity quantity_, Timestamp timestamp_)
        : restingOrderId(restingOrderId_), incomingOrderId(incomingOrderId_), price(price_), quantity(quantity_), timestamp(timestamp_) {}
};