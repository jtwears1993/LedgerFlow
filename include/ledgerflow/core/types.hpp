//
// Created by jtwears on 4/12/26.
//

#ifndef LEDGERFLOW_TYPES_H
#define LEDGERFLOW_TYPES_H

#include <cstdint>
#include <string>

namespace ledgerflow::core::types {
    // Alias integer-based money type. Use std::int32_t for compact fixed-width storage.
    using money = std::int32_t;
    using quantity = std::int32_t;
    using account_id = std::uint64_t;
    using trader_id = std::uint16_t;
    using trade_id = std::string;
    using symbol = std::string;
    using exchange = std::string;
    using idempotancy_key = std::string;
}
#endif //LEDGERFLOW_TYPES_H