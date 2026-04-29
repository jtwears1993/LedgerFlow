#include <gtest/gtest.h>

#include "wal_test_helpers.hpp"

using namespace ledgerflow::wal;
using namespace ledgerflow::test;

TEST(WalAppend, AppendAcceptsPayload) {
    TempFile tmp{makeTempPath()};
    WriteAheadLog wal{makeConfig(tmp.path)};
    const std::vector<std::byte> payload{std::byte{0xDE}, std::byte{0xAD}};
    EXPECT_NO_THROW(wal.append(payload, /*event_type=*/1));
}

// Intent: each append increments the WAL-assigned seq starting from 0.
// EXPECTED TO FAIL: commitRecordToFile() writes sizeof(WalRecord) bytes,
// so trailing vector-internal bytes corrupt the stream. readWalRecords()
// returns fewer records than appended. Exposes Bug A.
TEST(WalAppend, SequenceNumbersIncrease) {
    TempFile tmp{makeTempPath()};
    {
        WriteAheadLog wal{makeConfig(tmp.path)};
        for (int i = 0; i < 5; ++i) {
            wal.append({}, /*event_type=*/1);
        }
    }
    auto records = readWalRecords(tmp.path);
    ASSERT_EQ(records.size(), 5u);
    for (std::size_t i = 0; i < records.size(); ++i) {
        EXPECT_EQ(records[i].header.seq, i);
    }
}

// Intent: records flushed on destruction must be readable with correct seq
// and matching payload bytes.
// EXPECTED TO FAIL: payload bytes will be garbage due to Bug A.
TEST(WalAppend, RecordsPersistAfterDestruction) {
    TempFile tmp{makeTempPath()};
    constexpr int N = 10;
    {
        WriteAheadLog wal{makeConfig(tmp.path)};
        for (int i = 0; i < N; ++i) {
            std::vector<std::byte> payload{std::byte{static_cast<unsigned char>(i)}};
            wal.append(payload, /*event_type=*/2);
        }
    }
    auto records = readWalRecords(tmp.path);
    ASSERT_EQ(records.size(), static_cast<std::size_t>(N));
    for (int i = 0; i < N; ++i) {
        EXPECT_EQ(records[i].header.seq, static_cast<uint64_t>(i));
        ASSERT_EQ(records[i].data.size(), 1u);
        EXPECT_EQ(records[i].data[0], std::byte{static_cast<unsigned char>(i)});
    }
}

// Intent: records must appear on disk in the order they were appended.
// EXPECTED TO FAIL: serialization bug (Bug A) corrupts the stream so
// readWalRecords() returns fewer records than written.
TEST(WalAppend, OrderPreserved) {
    TempFile tmp{makeTempPath()};
    constexpr int N = 8;
    {
        WriteAheadLog wal{makeConfig(tmp.path)};
        for (int i = 0; i < N; ++i) {
            wal.append({}, /*event_type=*/1);
        }
    }
    auto records = readWalRecords(tmp.path);
    ASSERT_EQ(records.size(), static_cast<std::size_t>(N));
    for (std::size_t i = 0; i < records.size(); ++i) {
        EXPECT_EQ(records[i].header.seq, i);
    }
}

// FUTURE TEST: buffer overflow should auto-flush or signal an error rather
// than silently dropping records. Keep disabled until overflow handling lands.
TEST(WalAppend, DISABLED_BufferOverflowPreservesAllRecords) {
    TempFile tmp{makeTempPath()};
    constexpr std::size_t cap = 4;
    auto cfg = makeConfig(tmp.path);
    cfg.capacity = cap;
    {
        WriteAheadLog wal{cfg};
        for (std::size_t i = 0; i <= cap + 2; ++i) {
            wal.append({}, /*event_type=*/1);
        }
    }
    auto records = readWalRecords(tmp.path);
    EXPECT_EQ(records.size(), cap + 3);
}
