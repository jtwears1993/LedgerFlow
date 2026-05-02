#include <gtest/gtest.h>

#include <stdexcept>
#include <string>
#include <vector>

#include "ledgerflow/core/calculations.hpp"
#include "../../src/ledgerflow/position_engine.cpp"

namespace ledgerflow {
bool PositionEngine::onEvent(const core::events::Event&) { return false; }
bool PositionEngine::onMarketDataEvent(const core::events::MarketDataEvent&) { return false; }
void PositionEngine::onOrderEvent(const core::events::OrderEvent&) {}
Position* PositionEngine::getPosition(const std::string&) { return nullptr; }
Positions* PositionEngine::getPositions() { return nullptr; }
} // namespace ledgerflow

using namespace ledgerflow;

namespace lf_events = ledgerflow::core::events;
namespace lf_enums = ledgerflow::core::enums;

namespace {

constexpr std::int64_t kScale = 100;

lf_events::EventHeader makeHeader(const lf_events::EventType type) {
    return lf_events::EventHeader{
        .ingest_ts_ns = 1,
        .schema_version = 1,
        .type = type,
        .reserved = 0,
    };
}

lf_events::OrderEvent makeOrder(
    const std::string& symbol,
    const std::int64_t signedQty,
    const std::int64_t limitPrice,
    const std::int64_t avgEntryPrice
) {
    return lf_events::OrderEvent{
        .symbol = symbol,
        .hdr = makeHeader(lf_events::EventType::Order),
        .order_id = 42,
        .side = signedQty >= 0 ? lf_events::Side::Buy : lf_events::Side::Sell,
        .status = lf_events::OrderStatus::Accepted,
        .reserved = 0,
        .signed_order_qty = signedQty,
        .limit_price = limitPrice,
        .avg_entry_price = avgEntryPrice,
    };
}

lf_events::TopOfBookEvent_t makeTopOfBook(
    const std::string& symbol,
    const std::int64_t bid,
    const std::int64_t ask
) {
    return lf_events::TopOfBookEvent_t{
        .symbol = symbol,
        .hdr = makeHeader(lf_events::EventType::MarketData),
        .best_bid_price = bid,
        .best_ask_price = ask,
    };
}

lf_events::VwapEvent_t makeVwap(
    const std::string& symbol,
    const std::int64_t vwap
) {
    return lf_events::VwapEvent_t{
        .symbol = symbol,
        .hdr = makeHeader(lf_events::EventType::MarketData),
        .vwap = vwap,
    };
}

BasicPositionEngine makeEngine() {
    return BasicPositionEngine{lf_enums::ProductType::SPOT, kScale};
}

std::int64_t expectedUnrealizedPnl(
    const std::int64_t qty,
    const std::int64_t mark,
    const std::int64_t avgEntry
) {
    return ::core::calculations::unrealisedPnlFromAverageEntry(
        qty,
        mark,
        avgEntry,
        kScale,
        kScale,
        kScale
    );
}

} // namespace

TEST(PositionEngineFindPosition, EmptyVectorReturnsNullptr) {
    std::vector<Position> positions;

    EXPECT_EQ(find_position(positions, "AAPL"), nullptr);
}

TEST(PositionEngineFindPosition, MissingSymbolReturnsNullptr) {
    std::vector<Position> positions{
        Position{.sym = "MSFT", .quantity = 100},
    };

    EXPECT_EQ(find_position(positions, "AAPL"), nullptr);
}

TEST(PositionEngineFindPosition, MatchingSymbolReturnsPointer) {
    std::vector<Position> positions{
        Position{.sym = "MSFT", .quantity = 100},
        Position{.sym = "AAPL", .quantity = 200},
    };

    Position* pos = find_position(positions, "AAPL");

    ASSERT_NE(pos, nullptr);
    EXPECT_EQ(pos->sym, "AAPL");
    EXPECT_EQ(pos->quantity, 200);
}

TEST(PositionEngineFindPosition, ReturnedPointerAliasesVectorElement) {
    std::vector<Position> positions{
        Position{.sym = "AAPL", .quantity = 100},
    };

    Position* pos = find_position(positions, "AAPL");
    ASSERT_NE(pos, nullptr);
    pos->quantity = 250;

    EXPECT_EQ(positions[0].quantity, 250);
}

TEST(PositionEngineOrderEvent, FirstOrderViaOnEventCreatesPosition) {
    auto engine = makeEngine();
    const auto order = makeOrder("AAPL", 100, 10100, 10000);

    EXPECT_TRUE(engine.onEvent(lf_events::Event{order}));

    const Position* pos = engine.getPosition("AAPL");
    ASSERT_NE(pos, nullptr);
    EXPECT_EQ(pos->sym, "AAPL");
    EXPECT_EQ(pos->quantity, 100);
    EXPECT_EQ(pos->average_entry_price, 10000);
    EXPECT_EQ(pos->unrealized_pnl, expectedUnrealizedPnl(100, 10100, 10000));
}

TEST(PositionEngineOrderEvent, SameSymbolOrderUpdatesExistingPosition) {
    auto engine = makeEngine();

    EXPECT_TRUE(engine.onEvent(lf_events::Event{makeOrder("AAPL", 100, 10000, 10000)}));
    EXPECT_TRUE(engine.onEvent(lf_events::Event{makeOrder("AAPL", 50, 10200, 10200)}));

    const Positions* positions = engine.getPositions();
    ASSERT_NE(positions, nullptr);
    ASSERT_EQ(positions->positions.size(), 1u);
    EXPECT_EQ(positions->positions[0].sym, "AAPL");
    EXPECT_EQ(positions->positions[0].quantity, 150);
}

TEST(PositionEngineOrderEvent, SameDirectionOrderRecalculatesAverageEntryPrice) {
    auto engine = makeEngine();
    const auto first = makeOrder("AAPL", 100, 10000, 10000);
    const auto second = makeOrder("AAPL", 50, 10200, 10200);

    EXPECT_TRUE(engine.onEvent(lf_events::Event{first}));
    EXPECT_TRUE(engine.onEvent(lf_events::Event{second}));

    const auto expectedAvg = ::core::calculations::avgEntryPriceSameDirectionUnchecked(
        first.signed_order_qty,
        first.avg_entry_price,
        second.signed_order_qty,
        second.avg_entry_price
    );
    const Position* pos = engine.getPosition("AAPL");
    ASSERT_NE(pos, nullptr);
    EXPECT_EQ(pos->average_entry_price, expectedAvg);
    EXPECT_EQ(pos->unrealized_pnl, expectedUnrealizedPnl(150, second.limit_price, expectedAvg));
}

TEST(PositionEngineOrderEvent, MultipleSymbolsCreateSeparatePositions) {
    auto engine = makeEngine();

    EXPECT_TRUE(engine.onEvent(lf_events::Event{makeOrder("AAPL", 100, 10100, 10000)}));
    EXPECT_TRUE(engine.onEvent(lf_events::Event{makeOrder("MSFT", 200, 9900, 10000)}));

    const Positions* positions = engine.getPositions();
    ASSERT_NE(positions, nullptr);
    EXPECT_EQ(positions->positions.size(), 2u);
    EXPECT_NE(engine.getPosition("AAPL"), nullptr);
    EXPECT_NE(engine.getPosition("MSFT"), nullptr);
    EXPECT_EQ(engine.getPosition("TSLA"), nullptr);
}

TEST(PositionEngineOrderEvent, GetPositionsReturnsPortfolioTotals) {
    auto engine = makeEngine();

    const auto first = makeOrder("AAPL", 100, 10100, 10000);
    const auto second = makeOrder("MSFT", 200, 9900, 10000);
    EXPECT_TRUE(engine.onEvent(lf_events::Event{first}));
    EXPECT_TRUE(engine.onEvent(lf_events::Event{second}));

    const Positions* positions = engine.getPositions();
    ASSERT_NE(positions, nullptr);
    EXPECT_EQ(positions->positions.size(), 2u);
    EXPECT_EQ(
        positions->total_unrealized_pnl,
        expectedUnrealizedPnl(100, 10100, 10000) + expectedUnrealizedPnl(200, 9900, 10000)
    );
    EXPECT_EQ(positions->total_realized_pnl, 0);
}

TEST(PositionEngineOrderEvent, NegativeQuantityCreatesShortPosition) {
    auto engine = makeEngine();
    const auto order = makeOrder("AAPL", -100, 9900, 10000);

    EXPECT_TRUE(engine.onEvent(lf_events::Event{order}));

    const Position* pos = engine.getPosition("AAPL");
    ASSERT_NE(pos, nullptr);
    EXPECT_EQ(pos->quantity, -100);
    EXPECT_EQ(pos->unrealized_pnl, expectedUnrealizedPnl(-100, 9900, 10000));
}

TEST(PositionEngineOrderEvent, ZeroQuantityUpdateThrows) {
    auto engine = makeEngine();

    EXPECT_TRUE(engine.onEvent(lf_events::Event{makeOrder("AAPL", 100, 10000, 10000)}));

    EXPECT_THROW(
        engine.onEvent(lf_events::Event{makeOrder("AAPL", 0, 10000, 10000)}),
        std::invalid_argument
    );
}

TEST(PositionEngineMarketDataEvent, TopOfBookViaOnEventUpdatesExistingPosition) {
    auto engine = makeEngine();
    EXPECT_TRUE(engine.onEvent(lf_events::Event{makeOrder("AAPL", 100, 10000, 10000)}));

    const lf_events::MarketDataEvent md{makeTopOfBook("AAPL", 10100, 10300)};

    EXPECT_TRUE(engine.onEvent(lf_events::Event{md}));

    const Position* pos = engine.getPosition("AAPL");
    ASSERT_NE(pos, nullptr);
    const auto mark = ::core::calculations::markPriceFromBidAskMid(10100, 10300);
    EXPECT_EQ(pos->unrealized_pnl, expectedUnrealizedPnl(100, mark, 10000));
}

TEST(PositionEngineMarketDataEvent, UnknownSymbolReturnsFalseAndDoesNotMutateState) {
    auto engine = makeEngine();
    EXPECT_TRUE(engine.onEvent(lf_events::Event{makeOrder("AAPL", 100, 10000, 10000)}));
    const Positions before = *engine.getPositions();
    const lf_events::MarketDataEvent md{makeTopOfBook("MSFT", 10100, 10300)};

    EXPECT_FALSE(engine.onEvent(lf_events::Event{md}));

    const Positions* after = engine.getPositions();
    ASSERT_NE(after, nullptr);
    EXPECT_EQ(after->positions.size(), before.positions.size());
    EXPECT_EQ(after->total_unrealized_pnl, before.total_unrealized_pnl);
    ASSERT_NE(engine.getPosition("AAPL"), nullptr);
    EXPECT_EQ(engine.getPosition("AAPL")->unrealized_pnl, before.positions[0].unrealized_pnl);
}

TEST(PositionEngineMarketDataEvent, VwapMarketDataReturnsFalse) {
    auto engine = makeEngine();
    EXPECT_TRUE(engine.onEvent(lf_events::Event{makeOrder("AAPL", 100, 10000, 10000)}));
    const lf_events::MarketDataEvent md{makeVwap("AAPL", 10100)};

    EXPECT_FALSE(engine.onEvent(lf_events::Event{md}));
}

TEST(PositionEngineMarketDataEvent, RepeatedMarksShouldNotAccumulatePortfolioTotal) {
    auto engine = makeEngine();
    EXPECT_TRUE(engine.onEvent(lf_events::Event{makeOrder("AAPL", 100, 10000, 10000)}));
    const lf_events::MarketDataEvent md{makeTopOfBook("AAPL", 10100, 10300)};

    EXPECT_TRUE(engine.onEvent(lf_events::Event{md}));
    EXPECT_TRUE(engine.onEvent(lf_events::Event{md}));

    const auto expectedCurrentTotal = expectedUnrealizedPnl(100, 10200, 10000);
    // TODO: If totals are intended to represent current portfolio mark, the
    // implementation should replace the old mark contribution rather than add it.
    EXPECT_EQ(engine.getPositions()->total_unrealized_pnl, expectedCurrentTotal);
}
