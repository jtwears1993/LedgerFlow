#include <gtest/gtest.h>

#include "wal_test_helpers.hpp"

using namespace ledgerflow::wal;
using namespace ledgerflow::test;

TEST(WalAppend, AppendAcceptsCommitRecord) {
    TempFile tmp{makeTempPath()};
    WriteAheadLog wal{makeConfig(tmp.path)};
    core::CommitRecord rec{};
    EXPECT_NO_THROW(wal.append(rec));
}

TEST(WalAppend, SequenceNumbersIncrease) {
    TempFile tmp{makeTempPath()};
    {
        WriteAheadLog wal{makeConfig(tmp.path)};
        core::CommitRecord rec{};
        for (int i = 0; i < 5; ++i) {
            rec.hdr.seq = static_cast<uint64_t>(i);
            wal.append(rec);
        }
    }
    auto records = readWalRecords(tmp.path);
    ASSERT_EQ(records.size(), 5u);
    for (std::size_t i = 0; i < records.size(); ++i) {
        EXPECT_EQ(records[i].sequence_number, i);
    }
}

TEST(WalAppend, RecordsPersistAfterDestruction) {
    TempFile tmp{makeTempPath()};
    constexpr int N = 10;
    {
        WriteAheadLog wal{makeConfig(tmp.path)};
        for (int i = 0; i < N; ++i) {
            core::CommitRecord rec{};
            rec.hdr.seq = static_cast<uint64_t>(i * 100);
            rec.line_count = static_cast<uint16_t>(i + 1);
            wal.append(rec);
        }
    }
    auto records = readWalRecords(tmp.path);
    ASSERT_EQ(records.size(), static_cast<std::size_t>(N));
    for (int i = 0; i < N; ++i) {
        EXPECT_EQ(records[i].commit.hdr.seq, static_cast<uint64_t>(i * 100));
        EXPECT_EQ(records[i].commit.line_count, static_cast<uint16_t>(i + 1));
    }
}

TEST(WalAppend, OrderPreserved) {
    TempFile tmp{makeTempPath()};
    constexpr int N = 8;
    {
        WriteAheadLog wal{makeConfig(tmp.path)};
        for (uint64_t i = 0; i < N; ++i) {
            core::CommitRecord rec{};
            rec.hdr.seq = i * 7 + 3;
            wal.append(rec);
        }
    }
    auto records = readWalRecords(tmp.path);
    ASSERT_EQ(records.size(), static_cast<std::size_t>(N));
    for (std::size_t i = 0; i < N; ++i) {
        EXPECT_EQ(records[i].commit.hdr.seq, i * 7 + 3);
    }
}

// FUTURE TEST: buffer overflow should auto-flush or return an error rather
// than silently dropping records. Expected to fail until overflow handling
// is implemented.
TEST(WalAppend, DISABLED_BufferOverflowPreservesAllRecords) {
    TempFile tmp{makeTempPath()};
    constexpr std::size_t cap = 4;
    auto cfg = makeConfig(tmp.path);
    cfg.capacity = cap;
    {
        WriteAheadLog wal{cfg};
        for (std::size_t i = 0; i <= cap + 2; ++i) {
            core::CommitRecord rec{};
            rec.hdr.seq = i;
            wal.append(rec);
        }
    }
    auto records = readWalRecords(tmp.path);
    EXPECT_EQ(records.size(), cap + 3);
}
