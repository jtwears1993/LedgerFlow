//
// Created by jtwears on 5/2/26.
//

#pragma once

#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <algorithm>

namespace core::calculations {

using i64 = std::int64_t;
using i128 = __int128_t;

// -----------------------------------------------------------------------------
// Internal helpers
// -----------------------------------------------------------------------------

inline i128 abs128(const i128 value) {
    return value < 0 ? -value : value;
}

inline i128 divRoundHalfUpSigned(const i128 numerator, const i128 denominator) {
    if (denominator <= 0) {
        throw std::invalid_argument("denominator must be positive");
    }

    const i128 sign = numerator < 0 ? -1 : 1;
    const i128 absValue = abs128(numerator);

    return sign * ((absValue + denominator / 2) / denominator);
}

inline i64 checkedToI64(const i128 value) {
    if (value > static_cast<i128>(std::numeric_limits<i64>::max()) ||
        value < static_cast<i128>(std::numeric_limits<i64>::min())) {
        throw std::overflow_error("value does not fit in std::int64_t");
    }

    return static_cast<i64>(value);
}

inline void requirePositiveScale(const i64 scale, const char* name) {
    if (scale <= 0) {
        throw std::invalid_argument(std::string(name) + " must be positive");
    }
}

// -----------------------------------------------------------------------------
// Mark price helpers
// -----------------------------------------------------------------------------
//
// Calculates mark price as the bid/ask midpoint:
//
//   mark = (bestBid + bestAsk) / 2
//
// Inputs and return value are all scaled by priceScale.
// Example:
//
//   bestBidInt = 9990   // 99.90
//   bestAskInt = 10010  // 100.10
//   returns    = 10000  // 100.00
//
// Uses __int128 internally to avoid overflow from bestBid + bestAsk.
//
inline i64 markPriceFromBidAskMid(
    const i64 bestBidInt,
    const i64 bestAskInt
) {
    if (bestBidInt <= 0) {
        throw std::invalid_argument("bestBidInt must be positive");
    }

    if (bestAskInt <= 0) {
        throw std::invalid_argument("bestAskInt must be positive");
    }

    if (bestBidInt > bestAskInt) {
        throw std::invalid_argument("bestBidInt cannot be greater than bestAskInt");
    }

    const i128 sum =
        static_cast<i128>(bestBidInt) +
        static_cast<i128>(bestAskInt);

    return checkedToI64(divRoundHalfUpSigned(sum, 2));
}


    inline i64 avgEntryPriceSameDirectionUnchecked(
        const i64 currentSignedQtyInt,
        const i64 currentAvgEntryPriceInt,
        const i64 fillSignedQtyInt,
        const i64 fillPriceInt
    ) noexcept {
    // Assumes:
    // - fillSignedQtyInt != 0
    // - fillPriceInt > 0
    // - currentSignedQtyInt == 0 OR same sign as fillSignedQtyInt
    // - currentAvgEntryPriceInt > 0 when currentSignedQtyInt != 0

    if (currentSignedQtyInt == 0) {
        return fillPriceInt;
    }

    const i128 oldQtyAbs =
        abs128(currentSignedQtyInt);

    const i128 fillQtyAbs =
        abs128(fillSignedQtyInt);

    const i128 numerator =
        oldQtyAbs * static_cast<i128>(currentAvgEntryPriceInt) +
        fillQtyAbs * static_cast<i128>(fillPriceInt);

    const i128 denominator =
        oldQtyAbs + fillQtyAbs;

    return static_cast<i64>(
        divRoundHalfUpSigned(numerator, denominator)
    );
}

// -----------------------------------------------------------------------------
// Core PnL from average entry
// -----------------------------------------------------------------------------
//
// Inputs:
//
// signedQtyInt:
//   Signed quantity scaled by qtyScale.
//   Example: 1.25 units with qtyScale = 1e8 -> 125000000
//   Long position: positive.
//   Short position: negative.
//
// markPriceInt:
//   Mark price scaled by priceScale.
//   Example: 101.25 with priceScale = 1e2 -> 10125
//
// avgEntryPriceInt:
//   Average entry price scaled by priceScale.
//   Example: 100.10 with priceScale = 1e2 -> 10010
//
// targetScale:
//   Desired output scale.
//   Example: cents -> 100
//   Example: 6 decimals -> 1000000
//
// Formula:
//
//   PnL = signed_qty * (mark_price - avg_entry_price)
//
// Returns:
//
//   PnL scaled by targetScale.
//
// Example:
//
//   return value 144 with targetScale 100 means 1.44
//
inline i64 unrealisedPnlFromAverageEntry(
    const i64 signedQtyInt,
    const i64 markPriceInt,
    const i64 avgEntryPriceInt,
    const i64 qtyScale,
    const i64 priceScale,
    const i64 targetScale
) {
    requirePositiveScale(qtyScale, "qtyScale");
    requirePositiveScale(priceScale, "priceScale");
    requirePositiveScale(targetScale, "targetScale");

    const i128 priceDiff =
        static_cast<i128>(markPriceInt) -
        static_cast<i128>(avgEntryPriceInt);

    const i128 raw =
        static_cast<i128>(signedQtyInt) * priceDiff;

    const i128 numerator =
        raw * static_cast<i128>(targetScale);

    const i128 denominator =
        static_cast<i128>(qtyScale) *
        static_cast<i128>(priceScale);

    const i128 rounded =
        divRoundHalfUpSigned(numerator, denominator);

    return checkedToI64(rounded);
}

// -----------------------------------------------------------------------------
// Core PnL from open cost basis
// -----------------------------------------------------------------------------
//
// This is often better than using avgEntryPriceInt because it avoids average-entry
// rounding drift.
//
// openCostRaw:
//   Signed open cost basis in qtyScale * priceScale units.
//
// For a long:
//
//   openCostRaw = qtyInt * avgEntryPriceInt
//
// For a short:
//
//   openCostRaw should also be signed consistently with qty.
//   Example: short qtyInt = -100000000, avgEntry = 10010
//   openCostRaw = -100000000 * 10010
//
// Formula:
//
//   marketValueRaw = signedQtyInt * markPriceInt
//   unrealisedRaw  = marketValueRaw - openCostRaw
//
// Returns:
//
//   PnL scaled by targetScale.
//
inline i64 unrealisedPnlFromCostBasis(
    const i64 signedQtyInt,
    const i64 markPriceInt,
    const i128 openCostRaw,
    const i64 qtyScale,
    const i64 priceScale,
    const i64 targetScale
) {
    requirePositiveScale(qtyScale, "qtyScale");
    requirePositiveScale(priceScale, "priceScale");
    requirePositiveScale(targetScale, "targetScale");

    const i128 marketValueRaw =
        static_cast<i128>(signedQtyInt) *
        static_cast<i128>(markPriceInt);

    const i128 unrealisedRaw =
        marketValueRaw - openCostRaw;

    const i128 numerator =
        unrealisedRaw * static_cast<i128>(targetScale);

    const i128 denominator =
        static_cast<i128>(qtyScale) *
        static_cast<i128>(priceScale);

    const i128 rounded =
        divRoundHalfUpSigned(numerator, denominator);

    return checkedToI64(rounded);
}

// -----------------------------------------------------------------------------
// Cost-basis helpers
// -----------------------------------------------------------------------------

inline i128 rawCost(
    const i64 signedQtyInt,
    const i64 priceInt
) {
    return static_cast<i128>(signedQtyInt) *
           static_cast<i128>(priceInt);
}

inline i128 addFillToOpenCostRaw(
    const i128 currentOpenCostRaw,
    const i64 fillSignedQtyInt,
    const i64 fillPriceInt
) {
    return currentOpenCostRaw + rawCost(fillSignedQtyInt, fillPriceInt);
}

// -----------------------------------------------------------------------------
// Fixed-point formatting helpers
// -----------------------------------------------------------------------------

inline std::string toString(i128 value) {
    if (value == 0) {
        return "0";
    }

    const bool negative = value < 0;
    if (negative) {
        value = -value;
    }

    std::string result;

    while (value > 0) {
        const int digit = static_cast<int>(value % 10);
        result.push_back(static_cast<char>('0' + digit));
        value /= 10;
    }

    if (negative) {
        result.push_back('-');
    }

    std::ranges::reverse(result);
    return result;
}

inline int decimalPlacesFromScale(i64 scale) {
    requirePositiveScale(scale, "scale");

    int decimals = 0;

    while (scale > 1) {
        if (scale % 10 != 0) {
            throw std::invalid_argument("scale must be a power of 10");
        }

        scale /= 10;
        decimals++;
    }

    return decimals;
}

inline std::string formatFixed(
    const i64 value,
    const i64 scale
) {
    requirePositiveScale(scale, "scale");

    const int decimals = decimalPlacesFromScale(scale);

    const bool negative = value < 0;
    i128 absValue = static_cast<i128>(value);

    if (negative) {
        absValue = -absValue;
    }

    const i128 whole = absValue / static_cast<i128>(scale);
    const i128 fraction = absValue % static_cast<i128>(scale);

    std::string result = toString(whole);

    if (decimals > 0) {
        std::string frac = toString(fraction);

        while (static_cast<int>(frac.size()) < decimals) {
            frac = "0" + frac;
        }

        result += "." + frac;
    }

    if (negative) {
        result = "-" + result;
    }

    return result;
}

// -----------------------------------------------------------------------------
// Example constants
// -----------------------------------------------------------------------------

constexpr i64 SCALE_2  = 100LL;
constexpr i64 SCALE_4  = 10000LL;
constexpr i64 SCALE_6  = 1000000LL;
constexpr i64 SCALE_8  = 100000000LL;
constexpr i64 SCALE_18 = 1000000000000000000LL;

} // namespace pnl