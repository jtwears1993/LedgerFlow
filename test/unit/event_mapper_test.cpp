#include <gtest/gtest.h>

#include "ledgerflow/core/event_mapper.hpp"

#include <stdexcept>
#include <variant>

namespace lf_mapper = ledgerflow::core::event_mapper;
namespace lf_events = ledgerflow::core::events;
namespace lf_enums = ledgerflow::core::enums;

namespace {

EventHeader makeProtoHeader(const EventType type) {
    EventHeader header;
    header.set_ingest_ts_ns(123);
    header.set_schema_version(1);
    header.set_type(type);
    header.set_reserved_value(2);
    return header;
}

ExecutionEvent makeProtoExecutionEvent() {
    ExecutionEvent event;
    event.set_symbol("AAPL");
    *event.mutable_hdr() = makeProtoHeader(EVENT_TYPE_ORDER);
    event.set_order_id(42);
    event.set_side(SIDE_BUY);
    event.set_status(ORDER_STATUS_ACCEPTED);
    event.set_reserved_value(3);
    event.set_signed_order_qty(100);
    event.set_limit_price(10100);
    event.set_avg_entry_price(10050);
    return event;
}

TopOfBook makeProtoTopOfBook() {
    TopOfBook event;
    event.set_symbol("MSFT");
    *event.mutable_hdr() = makeProtoHeader(EVENT_TYPE_MARKET_DATA);
    event.set_best_bid_price(20100);
    event.set_best_ask_price(20200);
    return event;
}

Vwap makeProtoVwap() {
    Vwap event;
    event.set_symbol("NVDA");
    *event.mutable_hdr() = makeProtoHeader(EVENT_TYPE_MARKET_DATA);
    event.set_vwap(30300);
    return event;
}

} // namespace

TEST(EventMapperEnumMapping, MapsEventTypes) {
    EXPECT_EQ(lf_mapper::toInternalEventType(EVENT_TYPE_MARKET_DATA), lf_events::EventType::MarketData);
    EXPECT_EQ(lf_mapper::toInternalEventType(EVENT_TYPE_ORDER), lf_events::EventType::Order);
    EXPECT_EQ(lf_mapper::toInternalEventType(EVENT_TYPE_EXECUTION), lf_events::EventType::Execution);
    EXPECT_THROW(lf_mapper::toInternalEventType(EVENT_TYPE_UNSPECIFIED), std::invalid_argument);
}

TEST(EventMapperEnumMapping, MapsSides) {
    EXPECT_EQ(lf_mapper::toInternalEventSide(SIDE_BUY), lf_events::Side::Buy);
    EXPECT_EQ(lf_mapper::toInternalEventSide(SIDE_SELL), lf_events::Side::Sell);
    EXPECT_EQ(lf_mapper::toInternalCoreSide(SIDE_BUY), lf_enums::Side::BUY);
    EXPECT_EQ(lf_mapper::toInternalCoreSide(SIDE_SELL), lf_enums::Side::SELL);
    EXPECT_THROW(lf_mapper::toInternalEventSide(SIDE_UNSPECIFIED), std::invalid_argument);
    EXPECT_THROW(lf_mapper::toInternalCoreSide(SIDE_UNSPECIFIED), std::invalid_argument);
}

TEST(EventMapperEnumMapping, MapsOrderStatuses) {
    EXPECT_EQ(lf_mapper::toInternalOrderStatus(ORDER_STATUS_ACCEPTED), lf_events::OrderStatus::Accepted);
    EXPECT_EQ(lf_mapper::toInternalOrderStatus(ORDER_STATUS_CANCELED), lf_events::OrderStatus::Canceled);
    EXPECT_EQ(lf_mapper::toInternalOrderStatus(ORDER_STATUS_EXPIRED), lf_events::OrderStatus::Expired);
    EXPECT_EQ(lf_mapper::toInternalOrderStatus(ORDER_STATUS_REJECTED), lf_events::OrderStatus::Rejected);
    EXPECT_THROW(lf_mapper::toInternalOrderStatus(ORDER_STATUS_UNSPECIFIED), std::invalid_argument);
}

TEST(EventMapperMessageMapping, MapsEventHeader) {
    const lf_events::EventHeader mapped = lf_mapper::toInternalEventHeader(makeProtoHeader(EVENT_TYPE_ORDER));

    EXPECT_EQ(mapped.ingest_ts_ns, 123);
    EXPECT_EQ(mapped.schema_version, 1);
    EXPECT_EQ(mapped.type, lf_events::EventType::Order);
    EXPECT_EQ(mapped.reserved, 2);
}

TEST(EventMapperMessageMapping, MapsTrade) {
    Trade trade;
    trade.set_symbol("TSLA");
    *trade.mutable_hdr() = makeProtoHeader(EVENT_TYPE_MARKET_DATA);
    trade.set_side(SIDE_SELL);
    trade.set_price(40400);
    trade.set_quantity(10);

    const lf_events::Trade_t mapped = lf_mapper::toInternalTrade(trade);

    EXPECT_EQ(mapped.symbol, "TSLA");
    EXPECT_EQ(mapped.hdr.type, lf_events::EventType::MarketData);
    EXPECT_EQ(mapped.side, lf_enums::Side::SELL);
    EXPECT_EQ(mapped.price, 40400);
    EXPECT_EQ(mapped.quantity, 10);
}

TEST(EventMapperMessageMapping, MapsTopOfBook) {
    const lf_events::TopOfBookEvent_t mapped = lf_mapper::toInternalTopOfBook(makeProtoTopOfBook());

    EXPECT_EQ(mapped.symbol, "MSFT");
    EXPECT_EQ(mapped.hdr.type, lf_events::EventType::MarketData);
    EXPECT_EQ(mapped.best_bid_price, 20100);
    EXPECT_EQ(mapped.best_ask_price, 20200);
}

TEST(EventMapperMessageMapping, MapsVwap) {
    const lf_events::VwapEvent_t mapped = lf_mapper::toInternalVwap(makeProtoVwap());

    EXPECT_EQ(mapped.symbol, "NVDA");
    EXPECT_EQ(mapped.hdr.type, lf_events::EventType::MarketData);
    EXPECT_EQ(mapped.vwap, 30300);
}

TEST(EventMapperMessageMapping, MapsExecutionEventToOrderEvent) {
    const lf_events::OrderEvent mapped = lf_mapper::toInternalOrderEvent(makeProtoExecutionEvent());

    EXPECT_EQ(mapped.symbol, "AAPL");
    EXPECT_EQ(mapped.hdr.type, lf_events::EventType::Order);
    EXPECT_EQ(mapped.order_id, 42);
    EXPECT_EQ(mapped.side, lf_events::Side::Buy);
    EXPECT_EQ(mapped.status, lf_events::OrderStatus::Accepted);
    EXPECT_EQ(mapped.reserved, 3);
    EXPECT_EQ(mapped.signed_order_qty, 100);
    EXPECT_EQ(mapped.limit_price, 10100);
    EXPECT_EQ(mapped.avg_entry_price, 10050);
}

TEST(EventMapperIngressMapping, MapsExecutionRequestToEventVariant) {
    IngressRequest request;
    request.set_type(REQUEST_TYPE_EXECUTION_EVENT);
    *request.mutable_execution_event() = makeProtoExecutionEvent();

    const lf_events::Event mapped = lf_mapper::toInternalEvent(request);
    const auto* order = std::get_if<lf_events::OrderEvent>(&mapped);

    ASSERT_NE(order, nullptr);
    EXPECT_EQ(order->symbol, "AAPL");
    EXPECT_EQ(order->order_id, 42);
}

TEST(EventMapperIngressMapping, MapsTopOfBookRequestToEventVariant) {
    IngressRequest request;
    request.set_type(REQUEST_TYPE_TOP_OF_BOOK);
    *request.mutable_top_of_book() = makeProtoTopOfBook();

    const lf_events::Event mapped = lf_mapper::toInternalEvent(request);
    const auto* marketData = std::get_if<lf_events::MarketDataEvent>(&mapped);
    ASSERT_NE(marketData, nullptr);

    const auto* topOfBook = std::get_if<lf_events::TopOfBookEvent_t>(marketData);
    ASSERT_NE(topOfBook, nullptr);
    EXPECT_EQ(topOfBook->symbol, "MSFT");
    EXPECT_EQ(topOfBook->best_bid_price, 20100);
}

TEST(EventMapperIngressMapping, RejectsNonEventRequests) {
    IngressRequest getPosition;
    getPosition.set_type(REQUEST_TYPE_GET_POSITION);
    getPosition.mutable_get_position_request()->set_symbol("AAPL");

    IngressRequest getAll;
    getAll.set_type(REQUEST_TYPE_GET_ALL_POSITIONS);
    getAll.mutable_get_all_positions_request();

    EXPECT_THROW(lf_mapper::toInternalEvent(getPosition), std::invalid_argument);
    EXPECT_THROW(lf_mapper::toInternalEvent(getAll), std::invalid_argument);
}
