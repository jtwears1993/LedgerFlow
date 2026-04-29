//
// Created by jtwears on 4/29/26.
//

#pragma once

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
        std::uint64_t sequence_number;   // global monotonic sequence at ingest
        std::uint64_t ingest_ts_ns;      // local ingest timestamp
        std::uint16_t schema_version;    // start at 1
        EventType type;                  // tagged payload selector
        std::uint8_t reserved = 0;
    };

    struct MarketDataEvent {
        std::string symbol;
        EventHeader hdr;
        MarketDataKind kind;
        std::uint8_t reserved_a = 0;
        std::uint16_t reserved_b = 0;

        // Trade: price + quantity
        // Vwap : vwap_price + window_ns (quantity optional/zero)
        std::int64_t price_or_vwap;
        std::int64_t quantity;
        std::uint64_t window_ns;
    };

    struct OrderEvent {
        std::string symbol;
        EventHeader hdr;
        std::uint64_t order_id;
        Side side;
        OrderStatus status;
        std::uint16_t reserved = 0;
        std::int64_t order_qty;
        std::int64_t limit_price;
    };

    struct ExecutionEvent {
        std::string symbol;
        EventHeader hdr;
        std::uint64_t order_id;
        std::uint64_t exec_id;       // dedupe/idempotency key
        Side side;
        std::uint8_t reserved_a = 0;
        std::uint16_t reserved_b = 0;
        std::int64_t last_qty;
        std::int64_t last_price;
        std::int64_t cumulative_qty;
        std::int64_t leaves_qty;
    };

    using Event = std::variant<MarketDataEvent, OrderEvent, ExecutionEvent>;

} // namespace ledgerflow::core::events