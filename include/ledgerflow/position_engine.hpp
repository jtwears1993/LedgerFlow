//
// Created by jtwears on 4/24/26.
//

#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>


namespace ledgerflow {

    struct Position {
        std::string sym;
        std::int64_t quantity;
        std::int64_t realized_pnl;
        std::int64_t unrealized_pnl;
        std::int64_t average_entry_price;
        std::int64_t average_exit_price;
        std::int64_t last_sequence_number;
    };

    inline Position* find_position(std::vector<Position>& positions, const std::string& sym) {
        for (auto& pos : positions) {
            if (pos.sym == sym) {
                return &pos;
            }
        }
        return nullptr;
    }

    struct Positions {
        std::vector<Position> positions;
        std::int64_t max_exposure;
        std::int64_t current_exposure;
        std::int64_t available_exposure_capacity; // remaining risk capacity (headroom)
        std::int64_t total_realized_pnl;
        std::int64_t total_unrealized_pnl;
    };

    class PositionEngine {
    public:
        PositionEngine() = default;
        virtual ~PositionEngine() = default;
        virtual bool onEvent();
        virtual bool onMarketDataEvent();
        virtual bool onPositionEvent();
    };

    PositionEngine& instance();
}
