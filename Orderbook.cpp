#include "OrderBook.h"
#include <algorithm>

std::vector<Trade> OrderBook::addOrder(Order order) {
    // The engine is the single source of truth for arrival sequence —
    order.timestamp = nextTimestamp_++;

    std::vector<Trade> trades;
    match(order, trades);

    // Whatever's left after matching: Limit rests, Market/IOC is discarded.
    if (order.quantity > 0 && order.type == OrderType::Limit) {
        rest(order);
    }

    return trades;
}

void OrderBook::match(Order& incoming, std::vector<Trade>& trades) {
    if (incoming.side == Side::Buy) {
        // A buy crosses if it's a Market order, or its price >= best ask.
        while (incoming.quantity > 0 && !asks_.empty()) {
            auto levelIt = asks_.begin(); // asks_ sorted ascending -> best ask
            const Price bestAskPrice = levelIt->first;

            const bool crosses = (incoming.type == OrderType::Market) ||
                                  (incoming.price >= bestAskPrice);
            if (!crosses) break;

            std::deque<Order>& level = levelIt->second;
            Order& resting = level.front(); // FIFO: oldest order at this price matches first

            const Quantity fill = std::min(incoming.quantity, resting.quantity);
            trades.emplace_back(resting.id, incoming.id, resting.price, fill, incoming.timestamp);

            resting.quantity -= fill;
            incoming.quantity -= fill;

            if (resting.quantity == 0) {
                locations_.erase(resting.id);
                level.pop_front();
                if (level.empty()) {
                    asks_.erase(levelIt);
                }
            }
        }
    } else { // Side::Sell
        // A sell crosses if it's a Market order, or its price <= best bid.
        while (incoming.quantity > 0 && !bids_.empty()) {
            auto levelIt = bids_.begin(); // bids_ sorted descending -> best bid
            const Price bestBidPrice = levelIt->first;

            const bool crosses = (incoming.type == OrderType::Market) ||
                                  (incoming.price <= bestBidPrice);
            if (!crosses) break;

            std::deque<Order>& level = levelIt->second;
            Order& resting = level.front();

            const Quantity fill = std::min(incoming.quantity, resting.quantity);
            trades.emplace_back(resting.id, incoming.id, resting.price, fill, incoming.timestamp);

            resting.quantity -= fill;
            incoming.quantity -= fill;

            if (resting.quantity == 0) {
                locations_.erase(resting.id);
                level.pop_front();
                if (level.empty()) {
                    bids_.erase(levelIt);
                }
            }
        }
    }
}

void OrderBook::rest(const Order& order) {
    if (order.side == Side::Buy) {
        bids_[order.price].push_back(order);
    } else {
        asks_[order.price].push_back(order);
    }
    locations_[order.id] = OrderLocation{order.side, order.price};
}

bool OrderBook::cancelOrder(OrderId id) {
    auto locIt = locations_.find(id);
    if (locIt == locations_.end()) {
        return false; // never existed, already filled, or already cancelled
    }

    const Side side = locIt->second.side;
    const Price price = locIt->second.price;

    auto eraseFrom = [&](auto& book) {
        auto levelIt = book.find(price);
        if (levelIt == book.end()) return false;
        auto& level = levelIt->second;
        auto orderIt = std::find_if(level.begin(), level.end(),
                                     [id](const Order& o) { return o.id == id; });
        if (orderIt == level.end()) return false;
        level.erase(orderIt);
        if (level.empty()) {
            book.erase(levelIt);
        }
        return true;
    };

    const bool removed = (side == Side::Buy) ? eraseFrom(bids_) : eraseFrom(asks_);
    locations_.erase(locIt);
    return removed;
}

bool OrderBook::bestBid(Price& outPrice, Quantity& outQty) const {
    if (bids_.empty()) return false;
    const auto& [price, level] = *bids_.begin(); // bids_ sorted descending
    outPrice = price;
    outQty = level.empty() ? 0 : level.front().quantity;
    return true;
}

bool OrderBook::bestAsk(Price& outPrice, Quantity& outQty) const {
    if (asks_.empty()) return false;
    const auto& [price, level] = *asks_.begin(); // asks_ sorted ascending
    outPrice = price;
    outQty = level.empty() ? 0 : level.front().quantity;
    return true;
}