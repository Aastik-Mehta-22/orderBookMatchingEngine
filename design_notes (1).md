# OrderBook Engine — Design Document

A single-symbol, single-threaded limit order book with price-time priority
matching, in C++17. This document is the full design: what we're building,
the data structures, and exactly how data flows through the system, with
worked examples. Code comes after this is settled.

---

## 1. What we're building

A matching engine that behaves like the core of a stock exchange for **one
trading symbol**. It accepts incoming orders, matches them against resting
orders when prices cross, and produces trades.

**In scope:**
- `Limit` orders — rest in the book if they don't fully execute
- `Market` orders — execute immediately at best available price(s), never rest
- `Cancel` — remove a resting order by id
- `IOC` (Immediate-Or-Cancel) — match what's possible now, discard the rest

**Explicitly out of scope (for now):** multiple symbols, stop orders, iceberg
orders, multithreading. Good "what would you add next" talking points later —
not things we build yet.

---

## 2. Core concepts, defined with an example

- **Order** — one instruction: buy or sell, some quantity, at some price (or
  "market" = any price).
- **Price level** — all resting orders sitting at the exact same price. If
  three people all have buy orders resting at price 100, that's one price
  level with three orders in it, in the order they arrived.
- **The book** — two sides: **bids** (resting buy orders) and **asks**
  (resting sell orders). Bids sorted so the *highest* price is "best" (a
  buyer offering more gets matched first). Asks sorted so the *lowest* price
  is "best" (a seller asking less gets matched first).
- **Crossing** — when an incoming order's price would satisfy the best price
  on the opposite side. A crossing order **triggers a trade** instead of
  resting.
- **Trade** — the result of a match: a resting order and an incoming order
  agree on a price and quantity.

---

## 3. Data structures

### `Order` (one instruction, incoming or resting)
| Field | Type | Purpose |
|---|---|---|
| `id` | `OrderId` (uint64) | unique identifier, used for cancel lookup |
| `side` | `Side` (Buy/Sell) | which book it belongs to |
| `type` | `OrderType` (Limit/Market/IOC) | how it behaves when it doesn't fully match |
| `price` | `Price` (int64, ticks) | ignored for Market orders |
| `quantity` | `Quantity` (int64) | **remaining** quantity — shrinks as it gets partially filled |
| `timestamp` | `Timestamp` (uint64) | arrival sequence number, used for FIFO tie-breaking |

Price and quantity are integers (ticks/lots), not `double` — floating point
price comparisons are a classic correctness bug in matching engines.

### `Trade` (one execution)
| Field | Type | Purpose |
|---|---|---|
| `restingOrderId` | `OrderId` | the order that was already in the book |
| `incomingOrderId` | `OrderId` | the order that arrived and caused the match |
| `price` | `Price` | **always the resting order's price** (see §5, price improvement) |
| `quantity` | `Quantity` | how much was filled in this specific match |
| `timestamp` | `Timestamp` | when the match happened |

### `OrderBook` (the engine's internal state)
| Field | Type | Purpose |
|---|---|---|
| `bids_` | `std::map<Price, std::deque<Order>, std::greater<Price>>` | resting buy orders, sorted highest price first |
| `asks_` | `std::map<Price, std::deque<Order>>` | resting sell orders, sorted lowest price first |
| `locations_` | `std::unordered_map<OrderId, {Side, Price}>` | so `cancelOrder(id)` doesn't need to scan the whole book |

**Why `std::map` for price levels?** It keeps prices sorted automatically
(O(log n) insert/erase), so "what's the best price right now?" is just
reading the first entry — no manual sorting needed.

**Why `std::deque` inside each level?** New orders go to the back, matches
come from the front — that's FIFO, which is exactly time priority. We get
correct ordering from the container itself, no extra bookkeeping.

**Why `locations_`?** Without it, cancelling order #12345 would mean
scanning every price level on both sides looking for it — O(n). With it,
it's roughly O(1) to find where the order lives, then O(log n) to remove it.

---

## 4. Data flow — the big picture

```
                    ┌───────────────────┐
                    │   New Order (in)   │
                    └─────────┬───────────┘
                              ▼
                 ┌─────────────────────────────┐
                 │   OrderBook::addOrder()      │
                 └─────────┬─────────────────────┘
                            ▼
        ┌────────────────────────────────────────────┐
        │ Look at the OPPOSITE side's best price       │
        │  incoming Buy  -> check asks_ (lowest price)  │
        │  incoming Sell -> check bids_ (highest price) │
        └───────────┬────────────────────┬──────────────┘
             crosses?│                    │doesn't cross
                  yes▼                    ▼no
        ┌───────────────────────┐   ┌───────────────────────────┐
        │ MATCH LOOP:            │   │ If Limit: rest remaining   │
        │ - take front order of  │   │ qty in own side's book,    │
        │   best opposite level  │   │ record in locations_       │
        │ - fill = min(both qty) │   │                             │
        │ - emit Trade            │   │ If Market/IOC: discard     │
        │ - reduce both qty's     │   │ remaining qty (no resting) │
        │ - remove resting order  │   └───────────────────────────┘
        │   if it hit 0           │
        │ - repeat while incoming │
        │   qty > 0 AND still     │
        │   crosses                │
        └───────────┬───────────────┘
                     │ incoming qty may still be > 0 after loop
                     ▼
        (falls through to the "rest or discard" box above)
                     ▼
        ┌─────────────────────────────┐
        │  Return all Trades produced   │
        └─────────────────────────────┘
```

---

## 5. Worked example — adding orders step by step

Book starts **empty**. All prices in ticks for simplicity (e.g. 100 = $100.00).

**Step 1:** Order A arrives — `Buy, Limit, price=100, qty=10`
- Opposite side (asks_) is empty → nothing to cross.
- Type is Limit → rests. `bids_ = { 100: [A(qty=10)] }`
- Trades produced: none.

**Step 2:** Order B arrives — `Sell, Limit, price=101, qty=5`
- Opposite side (bids_) best price is 100. Does 101 cross 100? A sell only
  crosses if its price ≤ best bid. 101 > 100 → **no cross**.
- Rests. `asks_ = { 101: [B(qty=5)] }`
- Trades produced: none.

**Step 3:** Order C arrives — `Buy, Limit, price=102, qty=8`
- Opposite side (asks_) best price is 101. A buy crosses if its price ≥ best
  ask. 102 ≥ 101 → **crosses**.
- Match loop, iteration 1: front of `asks_[101]` is B (qty=5).
  `fill = min(8, 5) = 5`.
  **Trade emitted: `{resting=B, incoming=C, price=101, qty=5}`**
  *(note: trade price is 101 — the resting order's price, not C's 102. This
  is "price improvement": C offered to pay up to 102 but only had to pay
  101, since that's what was available.)*
  B's remaining qty → 0 → removed from `asks_[101]`. Level now empty →
  erased from `asks_`. C's remaining qty → 8 − 5 = 3.
- Match loop, iteration 2: `asks_` is now empty → nothing left to cross →
  loop ends.
- C still has qty=3 remaining, type is Limit → rests on its own side.
  `bids_ = { 102: [C(qty=3)], 100: [A(qty=10)] }`
- Trades produced: `[{B, C, 101, 5}]`

**End state after 3 orders:**
```
bids_: 102 -> [C(3)]
       100 -> [A(10)]
asks_: (empty)
```

**Step 4:** Order D arrives — `Sell, Market, qty=20`
- Market order → matches at whatever's available, ignores its own price.
- Match loop, iteration 1: best bid is 102, front order C (qty=3).
  `fill = min(20, 3) = 3`. **Trade: `{C, D, 102, 3}`**. C removed, level 102
  erased. D remaining = 20 − 3 = 17.
- Match loop, iteration 2: best bid is now 100, front order A (qty=10).
  `fill = min(17, 10) = 10`. **Trade: `{A, D, 100, 10}`**. A removed, level
  100 erased. D remaining = 17 − 10 = 7.
- Match loop, iteration 3: `bids_` now empty → loop ends.
- D still has qty=7 remaining, but type is **Market** → discarded, not
  rested. (This models a real market order that couldn't fully fill because
  there wasn't enough liquidity — it doesn't sit in the book waiting.)
- Trades produced: `[{C, D, 102, 3}, {A, D, 100, 10}]`

**End state after 4 orders:** book is completely empty on both sides.

---

## 6. Worked example — cancel flow

```
CancelOrder(id) called
        │
        ▼
locations_.find(id)  ── not found ──► return false (already filled/gone)
        │ found: {side, price}
        ▼
Go to bids_[price] or asks_[price], find the order in that deque, erase it
        │
        ▼
Is that price level's deque now empty?
   yes ──► erase the whole price level from the map
   no  ──► leave the level, other orders still resting there
        │
        ▼
locations_.erase(id)
        │
        ▼
return true
```

Example: In the state after Step 3 above (`bids_: 102->[C(3)], 100->[A(10)]`),
calling `cancelOrder(A.id)`:
- `locations_[A.id]` → `{Buy, 100}`
- Find A in `bids_[100]`, erase it → deque now empty → erase price level 100
  from `bids_`
- Erase `A.id` from `locations_`
- Result: `bids_: 102 -> [C(3)]` only. Returns `true`.

---

## 7. Invariants (things that must always be true — these become our unit tests)

1. **No crossed book at rest.** After any `addOrder()` call finishes, best
   bid price < best ask price (or one/both sides are empty). If this is ever
   violated, the matching loop has a bug.
2. **Quantity conservation.** For any incoming order: `incoming qty = sum of
   all Trade quantities produced + remaining resting qty (or discarded qty
   for Market/IOC)`. Nothing is created or destroyed.
3. **FIFO within a level is never violated.** If order X arrived before
   order Y at the same price, X always matches before Y.
4. **Cancelling a partially-filled order removes only what's left**, not the
   original quantity (we track *remaining* qty on the `Order`, not original).
5. **Trade price = resting order's price**, always — never the incoming
   order's price. This is what "price improvement" for the taker means and
   it's a rule worth stating explicitly, not leaving implicit.

---

## 8. Decisions (finalized)

- **Timestamp assignment: the engine does it, not the caller.**
  `OrderBook` owns a private `nextTimestamp_` counter, incremented on every
  `addOrder()` call, and it OVERWRITES whatever `order.timestamp` the caller
  set. Reasoning: the engine is the single source of truth for arrival
  sequence — that's what guarantees FIFO/time-priority actually holds. If
  the caller supplied timestamps, correctness would depend on caller
  discipline instead of an engine invariant.
- **`addOrder` takes a caller-built `Order` by value.** Caller sets `id`,
  `side`, `type`, `price`, `quantity`. Engine overwrites `timestamp`
  internally before doing anything else. Mirrors how a real trading system
  splits responsibility: client/gateway assigns the order id, the exchange
  assigns the sequence number.

## 9. Is `OrderBook` a singleton? — No.

Plain instantiable class, not a singleton. Three reasons:

1. **Testability.** Nearly every test starts from "book is empty" (see the
   worked examples in §5-6). A singleton carries state across test cases
   unless you bolt on manual reset methods — fighting the pattern rather
   than using it.
2. **Room to grow.** Single-symbol is in scope now, but the natural way to
   add a second symbol later is a higher-level owner (e.g. an `Exchange`
   class holding `std::unordered_map<Symbol, OrderBook>`) — one instance per
   symbol. A singleton would foreclose that without any benefit today.
3. **No genuine global-state need.** Singleton earns its place when
   something must be globally unique *and* globally reachable (a logger, a
   config store). An `OrderBook` is owned by whoever drives it — `main()`
   today, a feed-replay tool later. Plain composition gives the same "one
   instance" result more simply.

## 10. Finalized signatures

See `include/Order.h`, `include/Trade.h`, `include/OrderBook.h` — these now
reflect the decisions above, fully commented with contracts (pre/post
conditions) for every public method, plus two private helpers (`match()`,
`rest()`) that give `addOrder()`'s implementation an obvious shape:

```cpp
std::vector<Trade> addOrder(Order order);   // caller sets id/side/type/price/qty
bool cancelOrder(OrderId id);
bool bestBid(Price& outPrice, Quantity& outQty) const;
bool bestAsk(Price& outPrice, Quantity& outQty) const;

private:
void match(Order& incoming, std::vector<Trade>& trades);  // the matching loop
void rest(const Order& order);                              // insert remainder into own side
```

Compiles clean against the Day-1 skeleton (`src/OrderBook.cpp` stub bodies
updated to match). Implementation is the next step — go build `match()` and
`rest()` first (they're where all the real logic lives), then wire
`addOrder()` to call them per the contract above.
