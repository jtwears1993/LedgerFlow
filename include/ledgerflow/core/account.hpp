//
// Created by jtwears on 4/25/26.
//
#pragma once
#include <cstdint>

namespace ledgerflow::core {
    struct AccountKey {
        uint32_t account_id;
        uint16_t asset_id;

        bool operator==(const AccountKey& o) const {
            return account_id == o.account_id &&
                   asset_id == o.asset_id;
        }
    };

    struct AccountHead {
        uint64_t latest_line_offset;
        int64_t balance;
        uint64_t last_seq;
    };
}