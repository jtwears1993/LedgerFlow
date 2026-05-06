//
// Created by jtwears on 4/28/26.
//

#include "ledgerflow/position_engine.hpp"
#include "ledgerflow/core/events.hpp"
#include "ledgerflow/core/enums.hpp"
#include "ledgerflow/core/calculations.hpp"

#include <cstdint>
#include <string>
#include <variant>


namespace ledgerflow {
    BasicPositionEngine::BasicPositionEngine(const core::enums::ProductType productType, const std::int64_t minorUnits)
        : productType(productType), minorUnits(minorUnits) {}

    bool BasicPositionEngine::onEvent(const core::events::Event& event) {
        if (const auto mdEvent = std::get_if<core::events::MarketDataEvent>(&event)) {
            return onMarketDataEvent(*mdEvent);
        }
        if (const auto orderEvent = std::get_if<core::events::OrderEvent>(&event)) {
            onOrderEvent(*orderEvent);
            return true;
        }
        return false;
    }

    bool BasicPositionEngine::onMarketDataEvent(const core::events::MarketDataEvent& event) {
        const auto mdEvent = std::get_if<core::events::TopOfBookEvent_t>(&event);
        if (mdEvent == nullptr) {
            return false;
        }

        const auto pos = find_position(positions.positions, mdEvent->symbol);
        if (pos != nullptr) {
            const auto previousUnrealizedPnl = pos->unrealized_pnl;
            const auto markPrice = ::core::calculations::markPriceFromBidAskMid(
                mdEvent->best_bid_price,
                mdEvent->best_ask_price
            );
            pos->unrealized_pnl  = ::core::calculations::unrealisedPnlFromAverageEntry(
                pos->quantity,
                markPrice,
                pos->average_entry_price,
                minorUnits,
                minorUnits,
                minorUnits
            );
            positions.total_unrealized_pnl += pos->unrealized_pnl - previousUnrealizedPnl;
            return true;
        }
        return false;
    }

    void BasicPositionEngine::onOrderEvent(const core::events::OrderEvent& event) {
        const auto pos = find_position(positions.positions, event.symbol);
        if (pos == nullptr) {
            Position newPos = {};
            newPos.sym = event.symbol;
            newPos.quantity = event.signed_order_qty;
            newPos.average_entry_price = event.avg_entry_price;
            const auto unrealPnl = ::core::calculations::unrealisedPnlFromAverageEntry(
                event.signed_order_qty,
                event.limit_price,
                event.avg_entry_price,
                minorUnits,
                minorUnits,
                minorUnits
            );
            newPos.unrealized_pnl = unrealPnl;
            positions.total_unrealized_pnl += unrealPnl;
            positions.positions.push_back(newPos);
            return;
        }

        const auto previousQuantity = pos->quantity;
        const auto previousUnrealizedPnl = pos->unrealized_pnl;
        pos->quantity += event.signed_order_qty;
        const auto currentDirection = core::enums::sideFromSign(pos->quantity);
        if (const auto orderDirection = core::enums::sideFromSign(event.signed_order_qty); currentDirection == orderDirection) {
            pos->average_entry_price = ::core::calculations::avgEntryPriceSameDirectionUnchecked(
                previousQuantity,
                pos->average_entry_price,
                event.signed_order_qty,
                event.avg_entry_price
            );
            pos->unrealized_pnl = ::core::calculations::unrealisedPnlFromAverageEntry(
                pos->quantity,
                event.limit_price,
                pos->average_entry_price,
                minorUnits,
                minorUnits,
                minorUnits
            );
        }
        pos->unrealized_pnl = ::core::calculations::unrealisedPnlFromAverageEntry(
            pos->quantity,
            event.limit_price,
            pos->average_entry_price,
            minorUnits,
            minorUnits,
            minorUnits);
        positions.total_unrealized_pnl += pos->unrealized_pnl - previousUnrealizedPnl;
    }

    Position* BasicPositionEngine::getPosition(const std::string& sym) {
        return find_position(positions.positions, sym);
    }

    Positions* BasicPositionEngine::getPositions() {
        return &positions;
    }
}
