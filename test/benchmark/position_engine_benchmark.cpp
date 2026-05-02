#include <benchmark/benchmark.h>

#include <cstdint>
#include <string>
#include <vector>

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

BasicPositionEngine makeEngine() {
    return BasicPositionEngine{lf_enums::ProductType::SPOT, kScale};
}

std::string makeSymbol(const int i) {
    return "SYM" + std::to_string(i);
}

void seedPositions(BasicPositionEngine& engine, const int count) {
    for (int i = 0; i < count; ++i) {
        engine.onOrderEvent(makeOrder(makeSymbol(i), 100, 10000, 10000));
    }
}

} // namespace

static void BM_PositionEngine_CreatePositionFromOrder(benchmark::State& state) {
    const auto order = makeOrder("AAPL", 100, 10100, 10000);
    for (auto _ : state) {
        state.PauseTiming();
        auto engine = makeEngine();
        state.ResumeTiming();

        engine.onOrderEvent(order);
        benchmark::DoNotOptimize(engine.getPositions());
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_PositionEngine_CreatePositionFromOrder);

static void BM_PositionEngine_UpdateExistingPositionFromOrder(benchmark::State& state) {
    auto engine = makeEngine();
    engine.onOrderEvent(makeOrder("AAPL", 100, 10000, 10000));
    const auto order = makeOrder("AAPL", 1, 10000, 10000);

    for (auto _ : state) {
        engine.onOrderEvent(order);
        benchmark::DoNotOptimize(engine.getPosition("AAPL"));
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_PositionEngine_UpdateExistingPositionFromOrder);

static void BM_PositionEngine_ApplyTopOfBookMarketData(benchmark::State& state) {
    auto engine = makeEngine();
    engine.onOrderEvent(makeOrder("AAPL", 100, 10000, 10000));
    const lf_events::MarketDataEvent md{makeTopOfBook("AAPL", 10100, 10300)};

    for (auto _ : state) {
        const bool updated = engine.onMarketDataEvent(md);
        benchmark::DoNotOptimize(updated);
        benchmark::DoNotOptimize(engine.getPosition("AAPL"));
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_PositionEngine_ApplyTopOfBookMarketData);

static void BM_PositionEngine_DispatchOrderEvent(benchmark::State& state) {
    auto engine = makeEngine();
    engine.onOrderEvent(makeOrder("AAPL", 100, 10000, 10000));
    const lf_events::Event event{makeOrder("AAPL", 1, 10000, 10000)};

    for (auto _ : state) {
        const bool handled = engine.onEvent(event);
        benchmark::DoNotOptimize(handled);
        benchmark::DoNotOptimize(engine.getPosition("AAPL"));
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_PositionEngine_DispatchOrderEvent);

static void BM_PositionEngine_DispatchMarketDataEvent(benchmark::State& state) {
    auto engine = makeEngine();
    engine.onOrderEvent(makeOrder("AAPL", 100, 10000, 10000));
    const lf_events::MarketDataEvent md{makeTopOfBook("AAPL", 10100, 10300)};
    const lf_events::Event event{md};

    for (auto _ : state) {
        const bool handled = engine.onEvent(event);
        benchmark::DoNotOptimize(handled);
        benchmark::DoNotOptimize(engine.getPosition("AAPL"));
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_PositionEngine_DispatchMarketDataEvent);

static void BM_PositionEngine_GetPositionHit(benchmark::State& state) {
    const auto size = static_cast<int>(state.range(0));
    auto engine = makeEngine();
    seedPositions(engine, size);
    const auto symbol = makeSymbol(size - 1);

    for (auto _ : state) {
        Position* pos = engine.getPosition(symbol);
        benchmark::DoNotOptimize(pos);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_PositionEngine_GetPositionHit)->Arg(1)->Arg(10)->Arg(100)->Arg(1000)->Arg(10000);

static void BM_PositionEngine_GetPositionMiss(benchmark::State& state) {
    const auto size = static_cast<int>(state.range(0));
    auto engine = makeEngine();
    seedPositions(engine, size);
    const std::string symbol = "UNKNOWN";

    for (auto _ : state) {
        Position* pos = engine.getPosition(symbol);
        benchmark::DoNotOptimize(pos);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_PositionEngine_GetPositionMiss)->Arg(1)->Arg(10)->Arg(100)->Arg(1000)->Arg(10000);

static void BM_PositionEngine_MixedDispatch90MarketData10Order(benchmark::State& state) {
    auto engine = makeEngine();
    engine.onOrderEvent(makeOrder("AAPL", 100, 10000, 10000));

    const lf_events::MarketDataEvent md{makeTopOfBook("AAPL", 10100, 10300)};
    const lf_events::Event marketDataEvent{md};
    const lf_events::Event orderEvent{makeOrder("AAPL", 1, 10000, 10000)};
    std::vector<lf_events::Event> events;
    events.reserve(10);
    for (int i = 0; i < 9; ++i) {
        events.push_back(marketDataEvent);
    }
    events.push_back(orderEvent);

    std::size_t i = 0;
    for (auto _ : state) {
        const bool handled = engine.onEvent(events[i]);
        i = (i + 1) % events.size();
        benchmark::DoNotOptimize(handled);
        benchmark::DoNotOptimize(engine.getPosition("AAPL"));
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()));
}
BENCHMARK(BM_PositionEngine_MixedDispatch90MarketData10Order);
