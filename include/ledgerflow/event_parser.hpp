//
// Created by jtwears on 5/6/26.
//

#pragma once
#include "ingress.pb.h"

namespace ledgerflow {

    #pragma pack(push, 1)
    struct  Header {
        std::uint64_t timestampNS;
        std::uint32_t proto_size;
        std::uint32_t idempotencyKey;
    };
    #pragma pack(pop)

    enum class ParseResult {
        Ok,
        Truncated,
        Corrupted,
    };


    static ParseResult read_exact(const std::vector<char>& buffer, std::size_t offset, std::size_t size, const char** out) {
        if (buffer.size() < offset + size) return ParseResult::Truncated;
        *out = buffer.data() + offset;
        return ParseResult::Ok;
    }

    static ParseResult read_event(const std::vector<char>& buffer, IngressRequest* out) {
        const char* ptr = nullptr;

        if (const auto r = read_exact(buffer, 0, sizeof(Header), &ptr); r != ParseResult::Ok)
            return r;
        const auto header = reinterpret_cast<const Header*>(ptr);

        if (const auto r = read_exact(buffer, sizeof(Header), header->proto_size, &ptr); r != ParseResult::Ok)
            return r;

        return out->ParseFromArray(ptr, header->proto_size) ? ParseResult::Ok : ParseResult::Corrupted;
    }

    static void write_header(const Header& header, char* out) {
        std::memcpy(out, &header, sizeof(Header));
    }

    static ParseResult write_event(const IngressResponse& response, std::vector<char>& out) {
        const std::size_t proto_size = response.ByteSizeLong();
        out.resize(sizeof(Header) + proto_size);
        return response.SerializeToArray(out.data() + sizeof(Header), proto_size)
            ? ParseResult::Ok : ParseResult::Corrupted;
    }
}
