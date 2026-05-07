//
// Created by jtwears on 4/24/26.
//

#pragma once
#include <cerrno>
#include <iostream>
#include <optional>
#include <ostream>
#include <sys/types.h>
#include <unistd.h>

#include "sequencer.hpp"

#include <string>
#include <utility>

#include "ledgerflow/core/ring_buffer.hpp"


namespace ledgerflow::wal {

    /*
     * Wal record stored on disk as:
     * [WalFrameHeader][payload bytes]
     *
     * where the header has:
     * magic -> start of the frame
     * version
     * event_type
     * crc32 -> integrity, catches torn/ corrupted writed
     * seq -> the global monotonic order
     *
     * e.g. file layout:
     *
     * record_1 | record_2 | record_3 | ...
     *
     * length = sizeof(WalFrameHeader) + data.size() ALWAYS
     */

    enum class ReadStatus {
        Ok,
        EoF,
        Truncated,
        Corrupted,
        IoError,
    };

    enum class FsyncMode {
        Always = 0,
        Time = 1,
        Batch = 2,
        None = 3
    };

    constexpr std::uint32_t WalMagicBoundaryByte = 0x4C57414C; // LWAL
    constexpr std::uint16_t WalVersion = 1;
    constexpr std::size_t DefaultBatchSize = 64;
    constexpr std::size_t DefaultBatchSizePeriodUS = 1000;
    constexpr std::size_t DefaultWalCapacity = 1000;
    constexpr std::string_view DefaultWalFilePath = "wal.log";
    constexpr auto DefaultFsyncMode = FsyncMode::Always;

    #pragma pack(push, 1)
    struct WalFrameHeader {
        std::uint32_t magic;
        std::uint16_t version;
        std::uint16_t event_type;
        std::uint32_t length;
        std::uint32_t crc32;
        std::uint64_t seq;
    };
    #pragma pack(pop)

    struct WalRecord {
        WalFrameHeader header{};
        std::vector<std::byte> data;
    };

    struct WalConfig {
        std::optional<std::size_t> capacity = DefaultWalCapacity;
        std::optional<FsyncMode> fsync_mode = DefaultFsyncMode;
        std::optional<std::size_t> max_batch_size = DefaultBatchSize;
        std::optional<std::size_t> batch_period_us = DefaultBatchSizePeriodUS;
        std::optional<std::size_t> wal_capacity = DefaultWalCapacity;
        std::optional<std::string> wal_file_path = std::nullopt;
    };

    static inline  ReadStatus readAll(int fd, void* buf, std::size_t n) {
        auto* p = static_cast<std::byte*>(buf);
        std::size_t total = 0;

        while (total < n) {
            const ssize_t rc = read(fd, p + total, n - total);
            if (rc < 0) {
                if (errno == EINTR) continue;
                return ReadStatus::IoError;
            }
            if (rc == 0) {
                return ReadStatus::EoF; // EOF before n bytes
            }
            total += static_cast<std::size_t>(rc);
        }
        return ReadStatus::Ok;
    }

    static ReadStatus readNextWalRecord(const int fd, WalRecord* out) {
        if (fd == -1) {
            return ReadStatus::Corrupted;
        }
        WalFrameHeader header{};
        if (const auto status = readAll(fd, &header, sizeof(WalFrameHeader)); status != ReadStatus::Ok) {
            return status;
        }

        if (header.magic != WalMagicBoundaryByte) {
            return ReadStatus::Corrupted;
        }

        if (header.version != WalVersion) {
            return ReadStatus::Corrupted;
        }

        if (header.length < sizeof(WalFrameHeader)) {
            return ReadStatus::Truncated;
        }
        
        const std::size_t payload_len = static_cast<std::size_t>(header.length) - sizeof(WalFrameHeader);
        out->data.resize(payload_len);
        if (const auto status = readAll(fd, out->data.data(), payload_len); status != ReadStatus::Ok) {
            return status;
        }
        out->header = header;
        return ReadStatus::Ok;
    }

    static inline bool writeAll(int fd, const void* buf, std::size_t n) {
        const auto* p = static_cast<const std::byte*>(buf);
        std::size_t written = 0;

        while (written < n) {
            const ssize_t rc = write(fd, p + written, n - written);
            if (rc < 0) {
                if (errno == EINTR) {
                    continue;
                }
                return false;
            }
            if (rc == 0) {
                return false; // unexpected for write; avoid infinite loop
            }
            written += static_cast<std::size_t>(rc);
        }
        return true;
    }

    static inline bool writeNextWalRecord(const int fd, const WalRecord& record) {
        if (fd == -1) {
            return false;
        }

        if (!writeAll(fd, &record.header, sizeof(WalFrameHeader))) {
            return false;
        }

        if (!writeAll(fd, record.data.data(), record.data.size())) {
            return false;
        }
        return true;
    }

    class WriteAheadLog {
    public:
        explicit WriteAheadLog(WalConfig  config) : config_(std::move(config)), writeBuffer_(config.capacity.value_or(DefaultWalCapacity)) {
            if (const auto fd = openLogFile(); fd == -1) {
                throw std::runtime_error("Failed to open wal file");
            }

            if (config_.fsync_mode.has_value() && config_.fsync_mode.value() != FsyncMode::Always) {
                throw std::runtime_error("Fsync mode is not supported. V0 supports FsyncMode::Always only.");
            }
        }

        ~WriteAheadLog() noexcept {;
            if (const auto res = closeLogFile()) {
                std::cerr << "error in closing wal file: %d" << res << std::endl;
            }
        }
        // prevent copying of the WAL
        WriteAheadLog(const WriteAheadLog&) = delete;
        WriteAheadLog& operator=(const WriteAheadLog&) = delete;
        void append(const std::vector<std::byte>& data, std::uint16_t event_type);
        void commit();
        void recover(std::vector<WalRecord>& out);
        void flush();

    private:
        const WalConfig config_;
        Sequencer sequencer;
        core::RingBuffer<WalRecord> writeBuffer_;
        int walFileDescriptor_ = -1;
        [[nodiscard]] int commitRecordToFile(const WalRecord& record) const;
        int openLogFile();
        int closeLogFile();
    };
}
