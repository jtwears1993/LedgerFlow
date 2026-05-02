//
// Created by jtwears on 4/28/26.
//

#include "ledgerflow/position_engine.hpp"
#include "ledgerflow/core/events.hpp"
#include "ledgerflow/core/enums.hpp"
#include "ledgerflow/core/calculations.hpp"

#include <string>
#include <cstdint>


namespace ledgerflow {
    class BasicPositionEngine final : public PositionEngine {
    public:
        explicit BasicPositionEngine(const core::enums::ProductType productType, const std::int64_t minorUnits) : productType(productType),
         minorUnits(minorUnits) {}
        ~BasicPositionEngine() override = default;

        bool onEvent(const core::events::Event& event) override {
            if (const auto mdEvent = std::get_if<core::events::MarketDataEvent>(&event)) {
                return onMarketDataEvent(*mdEvent);
            }
            if (const auto orderEvent = std::get_if<core::events::OrderEvent>(&event)) {
                onOrderEvent(*orderEvent);
                return true;
            }
            return false;
        }

        bool onMarketDataEvent(const core::events::MarketDataEvent& event) override {
            const auto mdEvent = std::get_if<core::events::TopOfBookEvent_t>(&event);
            if (mdEvent == nullptr) {
                return false;
            }

            const auto pos = find_position(positions.positions, mdEvent->symbol);
            if (pos != nullptr) {
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
                // update the total portfolios unreal pnl
                positions.total_unrealized_pnl += pos->unrealized_pnl;
                return true;
            }
            return false;
        }

        void onOrderEvent(const core::events::OrderEvent& event) override {
            const auto pos = find_position(positions.positions, event.symbol);
            if (pos == nullptr) {
                // create new position
                Position newPos = {};
                newPos.sym = event.symbol;
                newPos.quantity = event.signed_order_qty;
                newPos.average_entry_price = event.avg_entry_price;
                const auto unrealPnl = ::core::calculations::unrealisedPnlFromAverageEntry(
                    event.signed_order_qty,
                    event.limit_price,
                     event.avg_entry_price, // avg entry price is needed as this could be for a market order
                    minorUnits,
                    minorUnits,
                    minorUnits
                );
                newPos.unrealized_pnl = unrealPnl;
                positions.total_unrealized_pnl += unrealPnl;
                positions.positions.push_back(newPos);
                return;
            }
            // position exists, we can update the position by += the qty
            pos->quantity += event.signed_order_qty;
            const auto currentDirection = core::enums::sideFromSign(pos->quantity);
            if (const auto orderDirection = core::enums::sideFromSign(event.signed_order_qty); currentDirection == orderDirection) {
                pos->average_entry_price = ::core::calculations::avgEntryPriceSameDirectionUnchecked(
                    pos->quantity,
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
            positions.total_unrealized_pnl += pos->unrealized_pnl;
            pos->unrealized_pnl = ::core::calculations::unrealisedPnlFromAverageEntry(
                pos->quantity,
                pos->average_entry_price,
                event.limit_price,
                event.avg_entry_price,
                minorUnits,
                minorUnits);
        }

        Position* getPosition(const std::string& sym) override {
            return find_position(positions.positions, sym);
        }

        Positions* getPositions() override {
            return &positions;
        }

        // NOTE - SO I Don't forget
        // Add in methods for:
        // 1. removing a position
        // 2. Adding max and available capacity
        // 3. calculating current exposure

    private:
        Positions positions = {};
        core::enums::ProductType productType;
        std::int64_t minorUnits;

    };
}