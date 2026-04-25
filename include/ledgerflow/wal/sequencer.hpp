//
// Created by jtwears on 4/24/26.
//

#pragma once

#include <cstdint>

namespace ledgerflow::wal {

    class Sequencer {
    public:
        Sequencer() = default;
        ~Sequencer() = default;

        std::uint64_t next_sequence_number() {
            return next_sequence_number_++;
        }

        void seed_sequence_number(const std::uint64_t sequence_number) {
            next_sequence_number_ = sequence_number;
        }

     private:
        std::uint64_t next_sequence_number_ = 0;
    };
}
