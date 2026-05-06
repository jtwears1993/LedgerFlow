#pragma once

#include "ingress.pb.h"
#include "ledgerflow/core/events.hpp"

#include <stdexcept>

namespace ledgerflow::core::event_mapper {

    inline events::EventType toInternalEventType(const ::EventType type) {
        switch (type) {
            case EVENT_TYPE_MARKET_DATA:
                return events::EventType::MarketData;
            case EVENT_TYPE_ORDER:
                return events::EventType::Order;
            case EVENT_TYPE_EXECUTION:
                return events::EventType::Execution;
            case EVENT_TYPE_UNSPECIFIED:
            default:
                throw std::invalid_argument("unsupported proto EventType");
        }
    }

    inline events::Side toInternalEventSide(const ::Side side) {
        switch (side) {
            case SIDE_BUY:
                return events::Side::Buy;
            case SIDE_SELL:
                return events::Side::Sell;
            case SIDE_UNSPECIFIED:
            default:
                throw std::invalid_argument("unsupported proto Side");
        }
    }

    inline enums::Side toInternalCoreSide(const ::Side side) {
        switch (side) {
            case SIDE_BUY:
                return enums::Side::BUY;
            case SIDE_SELL:
                return enums::Side::SELL;
            case SIDE_UNSPECIFIED:
            default:
                throw std::invalid_argument("unsupported proto Side");
        }
    }

    inline events::OrderStatus toInternalOrderStatus(const ::OrderStatus status) {
        switch (status) {
            case ORDER_STATUS_ACCEPTED:
                return events::OrderStatus::Accepted;
            case ORDER_STATUS_CANCELED:
                return events::OrderStatus::Canceled;
            case ORDER_STATUS_EXPIRED:
                return events::OrderStatus::Expired;
            case ORDER_STATUS_REJECTED:
                return events::OrderStatus::Rejected;
            case ORDER_STATUS_UNSPECIFIED:
            default:
                throw std::invalid_argument("unsupported proto OrderStatus");
        }
    }

    inline events::EventHeader toInternalEventHeader(const ::EventHeader& header) {
        return events::EventHeader{
            .ingest_ts_ns = header.ingest_ts_ns(),
            .schema_version = static_cast<std::uint16_t>(header.schema_version()),
            .type = toInternalEventType(header.type()),
            .reserved = static_cast<std::uint8_t>(header.reserved_value()),
        };
    }

    inline events::Trade_t toInternalTrade(const ::Trade& trade) {
        return events::Trade_t{
            .symbol = trade.symbol(),
            .hdr = toInternalEventHeader(trade.hdr()),
            .side = toInternalCoreSide(trade.side()),
            .price = trade.price(),
            .quantity = trade.quantity(),
        };
    }

    inline events::TopOfBookEvent_t toInternalTopOfBook(const ::TopOfBook& topOfBook) {
        return events::TopOfBookEvent_t{
            .symbol = topOfBook.symbol(),
            .hdr = toInternalEventHeader(topOfBook.hdr()),
            .best_bid_price = topOfBook.best_bid_price(),
            .best_ask_price = topOfBook.best_ask_price(),
        };
    }

    inline events::VwapEvent_t toInternalVwap(const ::Vwap& vwap) {
        return events::VwapEvent_t{
            .symbol = vwap.symbol(),
            .hdr = toInternalEventHeader(vwap.hdr()),
            .vwap = vwap.vwap(),
        };
    }

    inline events::OrderEvent toInternalOrderEvent(const ::ExecutionEvent& executionEvent) {
        return events::OrderEvent{
            .symbol = executionEvent.symbol(),
            .hdr = toInternalEventHeader(executionEvent.hdr()),
            .order_id = executionEvent.order_id(),
            .side = toInternalEventSide(executionEvent.side()),
            .status = toInternalOrderStatus(executionEvent.status()),
            .reserved = static_cast<std::uint16_t>(executionEvent.reserved_value()),
            .signed_order_qty = executionEvent.signed_order_qty(),
            .limit_price = executionEvent.limit_price(),
            .avg_entry_price = executionEvent.avg_entry_price(),
        };
    }

    inline events::MarketDataEvent toInternalMarketDataEvent(const ::TopOfBook& topOfBook) {
        return events::MarketDataEvent{toInternalTopOfBook(topOfBook)};
    }

    inline events::MarketDataEvent toInternalMarketDataEvent(const ::Vwap& vwap) {
        return events::MarketDataEvent{toInternalVwap(vwap)};
    }

    inline events::Event toInternalEvent(const ::IngressRequest& request) {
        switch (request.request_case()) {
            case IngressRequest::kExecutionEvent:
                return events::Event{toInternalOrderEvent(request.execution_event())};
            case IngressRequest::kTopOfBook:
                return events::Event{toInternalMarketDataEvent(request.top_of_book())};
            case IngressRequest::kGetPositionRequest:
            case IngressRequest::kGetAllPositionsRequest:
            case IngressRequest::REQUEST_NOT_SET:
            default:
                throw std::invalid_argument("ingress request does not contain an event payload");
        }
    }

} // namespace ledgerflow::core::event_mapper
