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

    enum class AccountType : std::uint8_t {
        TREASURY_FUNDS,
        TRADABLE_FUNDS,
        RESERVED_FOR_ORDERS,
        OPEN_POSITION,
        FEE_PAYABLE,
        CLEARING_RECEIVABLE,
        CLEARING_PAYABLE
    };


    enum class AccountCategory : std::uint8_t {
        ASSET,
        LIABILITY,
        EQUITY
    };

    constexpr AccountCategory categoryOf(const AccountType t) noexcept {
        switch (t) {
            case AccountType::TRADABLE_FUNDS:   return AccountCategory::ASSET;
            case AccountType::RESERVED_FOR_ORDERS:return AccountCategory::ASSET;

            // Unsettled funds while the exchange position is open:
            // If this account can go negative, the true category depends on balance sign.
            case AccountType::OPEN_POSITION:      return AccountCategory::ASSET;

            case AccountType::FEE_PAYABLE:        return AccountCategory::LIABILITY;

            // Can be receivable or payable depending on sign.
            case AccountType::CLEARING_RECEIVABLE:           return AccountCategory::ASSET;
            case AccountType::CLEARING_PAYABLE:           return AccountCategory::LIABILITY;
            default: return AccountCategory::ASSET; // Default to ASSET for any future account types
        }
    }

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