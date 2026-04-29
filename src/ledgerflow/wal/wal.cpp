//
// Created by jtwears on 4/24/26.
//

#include <fcntl.h>
#include <unistd.h>

#include "ledgerflow/wal/wal.hpp"

namespace ledgerflow::wal {
    void WriteAheadLog::append(const std::vector<std::byte>& data, const std::uint16_t event_type) {
        WalRecord record{};
        record.header.seq = sequencer.next_sequence_number();
        record.header.event_type = event_type;
        record.header.version = WalVersion;
        record.header.magic = WalMagicBoundaryByte;
        record.header.length = sizeof(WalFrameHeader) + data.size();
        record.header.crc32 = 0; // TODO - create cr32 func
        record.data = data;
        writeBuffer_.push(record);
    }


    void WriteAheadLog::commit() {
        if (std::vector<WalRecord> records; writeBuffer_.drain(records)) {
            for (auto& record : records) {
                if (const auto res = commitRecordToFile(record); res != 0) {
                    throw std::runtime_error("Failed to write to file");
                }
            }
            if (const auto res = fsync(walFileDescriptor_); res != 0) {
                throw std::runtime_error("Failed to fsync wal file");
            }
        }
    }

    void WriteAheadLog::recover(std::vector<WalRecord>& out) {
        if (lseek(walFileDescriptor_, 0, SEEK_SET) < 0) {
            throw std::runtime_error("Failed to seek to beginning of wal file");
        }
        bool loopRun = true;
        WalRecord record{};
        while (loopRun) {
            switch (readNextWalRecord(walFileDescriptor_, &record)) {
                case ReadStatus::Ok:
                    out.push_back(record);
                    sequencer.seed_sequence_number(record.header.seq);
                    record = WalRecord{};
                    continue;
                case ReadStatus::EoF:
                    loopRun = false; break;
                case ReadStatus::Truncated:
                    std::cerr << "Warning: truncated record found during recovery. Ignoring." << std::endl;
                    record = WalRecord{};
                    continue;
                default:
                    std::cerr << "Error: corrupted record found during recovery. Stopping." << std::endl;
                    throw std::runtime_error("Error: corrupted record found during recovery");
            }
        }
        if (lseek(walFileDescriptor_, 0, SEEK_END) < 0) {
            throw std::runtime_error("Failed to seek to end of wal file");
        }
    }


    int WriteAheadLog::openLogFile() {
        const auto path = config_.wal_file_path.value_or(std::string(DefaultWalFilePath));
        const auto fd = open(path.c_str(), O_RDWR | O_CREAT | O_APPEND, 0644);
        if (fd == -1) {
            return -1;
        }
        walFileDescriptor_ = fd;
        return 0;
    }

    int WriteAheadLog::closeLogFile() {
        if (walFileDescriptor_ == -1) {
            // not open. no need to close
            return 0;
        }
        // drain the buffer
        if (std::vector<WalRecord> records; writeBuffer_.drain(records)) {
            for (const auto& record : records) {
               if (const auto res = commitRecordToFile(record); res != 0) {
                   std::cerr << "Failed to write record to wal file during close" << std::endl;
                   return -1;
                }
            }
            fsync(walFileDescriptor_);
        }
        return close(walFileDescriptor_);
    }

    int WriteAheadLog::commitRecordToFile(const WalRecord& record) const {
        if (!writeAll(walFileDescriptor_, &record.header, sizeof(WalFrameHeader))) {
            return -1;
        }
        if (!writeAll(walFileDescriptor_, record.data.data(), record.data.size())) {
            return -1;
        }
        return 0;
    }
}
