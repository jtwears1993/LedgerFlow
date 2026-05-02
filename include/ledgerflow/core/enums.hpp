//
// Created by jtwears on 4/12/26.
//

#pragma once
#include <cstdint>
#include <stdexcept>

namespace ledgerflow::core::enums {

    enum class Side : std::uint8_t {
        BUY,
        SELL
    };

    inline Side sideFromSign(const std::int64_t signedQtyInt) {
      if (signedQtyInt > 0) {
          return Side::BUY;
      }
      if (signedQtyInt < 0) {
          return Side::SELL;
      }
      throw std::invalid_argument("signedQtyInt cannot be zero");
    }

    enum class ProductType : std::uint8_t {
        SPOT,
        FUTURES,
        OPTIONS
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
