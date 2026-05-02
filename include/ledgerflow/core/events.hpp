//
// Created by jtwears on 4/29/26.
//

#pragma once

#include "ledgerflow/core/enums.hpp"

#include <cstdint>
#include <string>
#include <variant>

namespace ledgerflow::core::events {

    enum class EventType : std::uint8_t {
        MarketData = 1,
        Order = 2,
        Execution = 3
    };

    enum class Side : std::uint8_t {
        Buy = 1,
        Sell = 2
    };

    enum class MarketDataKind : std::uint8_t {
        Trade = 1,
        Vwap = 2
    };

    enum class OrderStatus : std::uint8_t {
        Accepted = 1,
        Canceled = 2,
        Expired = 3,
        Rejected = 4
    };

    // Common envelope for deterministic replay ordering.
    struct EventHeader {
        std::uint64_t ingest_ts_ns;      // local ingest timestamp
        std::uint16_t schema_version;    // start at 1
        EventType type;                  // tagged payload selector
        std::uint8_t reserved = 0;
    };

    struct Trade_t {
        std::string symbol;
        EventHeader hdr;
        enums::Side side;
        std::int64_t price;
        std::int64_t quantity;
    };

    struct TopOfBookEvent_t {
        std::string symbol;
        EventHeader hdr;
        std::int64_t best_bid_price;
        std::int64_t best_ask_price;
    };

    struct VwapEvent_t {
        std::string symbol;
        EventHeader hdr;
        std::int64_t vwap;
    };

    struct OrderEvent {
        std::string symbol;
        EventHeader hdr;
        std::uint64_t order_id;
        Side side;
        OrderStatus status;
        std::uint16_t reserved = 0;
        std::int64_t signed_order_qty;
        std::int64_t limit_price;
        std::int64_t avg_entry_price;
    };


    using MarketDataEvent = std::variant<VwapEvent_t, TopOfBookEvent_t>;
    using Event = std::variant<MarketDataEvent, OrderEvent>;
} // namespace ledgerflow::core::events