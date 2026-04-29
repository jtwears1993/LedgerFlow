#pragma once
#include <string>
#include <vector>
#include <fcntl.h>
#include <unistd.h>

#include "ledgerflow/wal/wal.hpp"

namespace ledgerflow::test {

inline std::string makeTempPath() {
    char tmpl[] = "/tmp/wal_test_XXXXXX";
    int fd = ::mkstemp(tmpl);
    if (fd != -1) ::close(fd);
    return std::string(tmpl);
}

struct TempFile {
    std::string path;
    explicit TempFile(std::string p) : path(std::move(p)) {}
    ~TempFile() { ::unlink(path.c_str()); }
    TempFile(const TempFile&) = delete;
    TempFile& operator=(const TempFile&) = delete;
};

// Reads WAL records from disk using the proper frame parser.
// Stops at EoF or the first non-Ok status (e.g. Corrupted due to Bug A).
inline std::vector<wal::WalRecord> readWalRecords(const std::string& path) {
    const int fd = ::open(path.c_str(), O_RDONLY);
    if (fd == -1) return {};
    std::vector<wal::WalRecord> out;
    wal::WalRecord rec{};
    while (wal::readNextWalRecord(fd, &rec) == wal::ReadStatus::Ok) {
        out.push_back(rec);
        rec = wal::WalRecord{};
    }
    ::close(fd);
    return out;
}

inline wal::WalConfig makeConfig(const std::string& path) {
    wal::WalConfig cfg;
    cfg.wal_file_path = path;
    return cfg;
}

} // namespace ledgerflow::test