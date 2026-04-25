//
// Created by jtwears on 4/24/26.
//

#pragma once
#include <iostream>
#include <optional>
#include <ostream>

#include "ledgerflow/core/journal.hpp"
#include "sequencer.hpp"

#include <string>
#include <utility>

#include "ledgerflow/core/ring_buffer.hpp"


namespace ledgerflow::wal {

    enum class FsyncMode {
        Always = 0,
        Time = 1,
        Batch = 2,
        None = 3
    };

    constexpr std::size_t DefaultBatchSize = 10;
    constexpr std::size_t DefaultBatchSizePeriodUS = 1000;
    constexpr std::size_t DefaultWalCapacity = 1000;
    constexpr std::string_view DefaultWalFilePath = "ledgerflow/wal.log";
    constexpr auto DefaultFsyncMode = FsyncMode::Always;

    struct WalRecord {
        std::uint64_t sequence_number;
        core::CommitRecord commit;
    };

    struct WalConfig {
        std::optional<std::size_t> capacity = DefaultWalCapacity;
        std::optional<FsyncMode> fsync_mode = DefaultFsyncMode;
        std::optional<std::size_t> max_batch_size = DefaultBatchSize;
        std::optional<std::size_t> batch_period_us = DefaultBatchSizePeriodUS;
        std::optional<std::size_t> wal_capacity = DefaultWalCapacity;
        std::optional<std::string> wal_file_path = std::nullopt;
    };

    class WriteAheadLog {
    public:
        explicit WriteAheadLog(WalConfig  config) : config_(std::move(config)), writeBuffer_(config.capacity.value_or(DefaultWalCapacity)) {
            if (const auto fd = openLogFile(); fd == -1) {
                throw std::runtime_error("Failed to open wal file");
            }

            if ((config_.fsync_mode.has_value()) && (config_.fsync_mode.value() != FsyncMode::Always)) {
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
        void append(core::CommitRecord& entry);
        void commit();
        void recover();
    private:
        const WalConfig config_;
        Sequencer sequencer;
        core::RingBuffer<WalRecord> writeBuffer_;
        bool walIsOpen_ = false;
        int walFileDescriptor_ = -1;

        int openLogFile();
        int closeLogFile();
    };
}
