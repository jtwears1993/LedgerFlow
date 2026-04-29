//
// Created by jtwears on 4/28/26.
//

#include "ledgerflow/position_engine.hpp"


namespace ledgerflow {
    class Impl final : public PositionEngine {
    public:
        Impl() = default;
        ~Impl() override = default;

        bool onEvent() override {
            return true;
        }

        bool onMarketDataEvent() override {
            return true;
        }

        bool onPositionEvent() override {
            return true;
        }

    private:
        Positions positions = {};

    };

    Impl impl;

    PositionEngine& instance() {
        return impl;
    }
}