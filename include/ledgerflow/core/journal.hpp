//
// Created by jtwears on 4/25/26.
//

#pragma once
#include <cstdint>

namespace ledgerflow::core {

    #pragma pack(push, 1)
    struct RecordHeader {
        uint32_t magic;     // 'REC1'
        uint16_t type;      // 1=header, 2=line, 3=commit
        uint16_t version;

        uint32_t length;    // sizeof(full record)
        uint32_t crc32;

        uint64_t seq;       // transaction sequence
    };
    #pragma pack(pop)

    struct TxnHeaderRecord {
        RecordHeader hdr;

        uint64_t client_msg_id;
        uint64_t timestamp_ns;

        uint16_t line_count;
        uint16_t tx_type;

        uint64_t first_line_offset;

        std::string config_hash;
        uint16_t config_version;
    };

    struct TxnLineRecord {
        RecordHeader hdr;

        uint16_t line_no;

        uint32_t account_id;
        uint16_t asset_id;

        int64_t delta;
        int64_t balance_after;

        uint64_t prev_account_line_offset;
        uint8_t template_line_number;
    };

    struct CommitRecord {
        RecordHeader hdr;

        uint16_t line_count;
        uint16_t reserved;

        uint64_t header_offset;
    };

}
