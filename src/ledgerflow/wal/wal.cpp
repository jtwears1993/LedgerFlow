//
// Created by jtwears on 4/24/26.
//

#include <fcntl.h>
#include <unistd.h>

#include "ledgerflow/wal/wal.hpp"



namespace ledgerflow::wal {
    void WriteAheadLog::append(core::CommitRecord& entry) {
        // Serialize the entry and append to the write buffer
        WalRecord record{};
        record.sequence_number = sequencer.next_sequence_number();
        record.commit = entry;
        writeBuffer_.push(record);
    }


    void WriteAheadLog::commit() {
        // v0 we go simple - one mode ALWAYS
    }

    void WriteAheadLog::recover() {
        // read all from the open FD
        // restore the state IN order from the wal
    }


    int WriteAheadLog::openLogFile() {
        const auto path = config_.wal_file_path.value_or(std::string(DefaultWalFilePath));
        const auto fd = open(path.c_str(), O_RDWR | O_CREAT | O_APPEND, 0644);
        if (fd == -1) {
            return fd;
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
        std::vector<WalRecord> records;
        if (writeBuffer_.drain(records)) {
            for (const auto& record : records) {
                // cast the record toa char* and write to file
                const auto charRecord = reinterpret_cast<const char*>(&record);
                const auto bytes_written = write(walFileDescriptor_, charRecord, sizeof(record));
                if (bytes_written != sizeof(record)) {
                    // handle write error
                    std::cerr << "Failed to write record to wal file" << std::endl;
                    return -1;
                }
            }
        }
        // write to the file
        // flush the file buffer to disk before closing
        fsync(walFileDescriptor_);
        return close(walFileDescriptor_);
    }
}
