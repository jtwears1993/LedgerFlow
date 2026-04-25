#pragma once
#include <string>
#include <vector>
#include <fstream>
#include <unistd.h>

#include "ledgerflow/wal/wal.hpp"

using namespace ledgerflow;

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

inline std::vector<wal::WalRecord> readWalRecords(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    std::vector<wal::WalRecord> out;
    wal::WalRecord rec{};
    while (f.read(reinterpret_cast<char*>(&rec), sizeof(rec))) {
        out.push_back(rec);
    }
    return out;
}

inline wal::WalConfig makeConfig(const std::string& path) {
    wal::WalConfig cfg;
    cfg.wal_file_path = path;
    return cfg;
}

} // namespace ledgerflow::test