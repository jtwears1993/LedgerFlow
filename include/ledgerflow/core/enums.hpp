//
// Created by jtwears on 4/12/26.
//

#pragma once
#include <cstdint>

namespace ledgerflow::core::enums {

    enum class Side : std::uint8_t {
        BUY,
        SELL
    };

    enum class ProductType : std::uint8_t {
        SPOT,
        FUTURES,
        OPTIONS
    };

    enum class EventType : std::uint8_t {
        POSITION_OPEN_REQUEST,
        ON_TRADE_OPEN,
        ON_TRADE_PARTIAL_FILL,
        ON_TRADE_FILL,
        ON_TRADE_CANCEL,
        ON_SETTLEMENT, // recon end of day settlement
        FUND_ALLOCATION,
    };

   enum class PositionOpenResponseStatus : std::uint8_t {
       INSUFFICIENT_FUNDS,
       IN_BREACH_OF_LIMITS,
       EXPOSURE_TOO_HIGH,
       DUPLICATE_REQUEST,
       INTERNAL_ERROR,
       OK
    };
}