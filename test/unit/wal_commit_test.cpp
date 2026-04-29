#include <gtest/gtest.h>

#include "wal_test_helpers.hpp"

using namespace ledgerflow::wal;
using namespace ledgerflow::test;

// Intent: records appended before commit() are visible on disk immediately
// after commit() returns, without requiring WAL destruction.
// EXPECTED TO FAIL: Bug A corrupts payload bytes; readWalRecords() may return
// wrong record count or garbage data.
TEST(WalCommit, CommitFlushesBufferToDisk) {
    TempFile tmp{makeTempPath()};
    WriteAheadLog wal{makeConfig(tmp.path)};

    const std::vector<std::byte> payload{std::byte{0x01}};
    wal.append(payload, /*event_type=*/1);
    wal.commit();

    auto records = readWalRecords(tmp.path);
    ASSERT_EQ(records.size(), 1u);
    EXPECT_EQ(records[0].header.seq, 0u);
}

// Intent: after commit() returns in FsyncMode::Always, data is durable on
// the storage device — not just in the OS page cache.
// EXPECTED TO FAIL: Bug A corrupts bytes after header; seq field will be garbage.
TEST(WalCommit, CommitFsyncsInAlwaysMode) {
    TempFile tmp{makeTempPath()};
    auto cfg = makeConfig(tmp.path);
    cfg.fsync_mode = FsyncMode::Always;
    WriteAheadLog wal{cfg};

    const std::vector<std::byte> payload{std::byte{0x63}};
    wal.append(payload, /*event_type=*/1);
    EXPECT_NO_THROW(wal.commit());

    auto records = readWalRecords(tmp.path);
    ASSERT_EQ(records.size(), 1u);
    EXPECT_EQ(records[0].header.seq, 0u);
    ASSERT_EQ(records[0].data.size(), 1u);
    EXPECT_EQ(records[0].data[0], std::byte{0x63});
}

// Intent: calling commit() multiple times or on an empty buffer must not
// error or duplicate already-flushed records.
TEST(WalCommit, CommitIsIdempotent) {
    TempFile tmp{makeTempPath()};
    WriteAheadLog wal{makeConfig(tmp.path)};

    EXPECT_NO_THROW(wal.commit());
    EXPECT_NO_THROW(wal.commit());

    wal.append({}, /*event_type=*/1);
    wal.commit();
    wal.commit();

    auto records = readWalRecords(tmp.path);
    EXPECT_EQ(records.size(), 1u);
}

// Intent: records committed to disk must appear in the same order they were
// appended, not reordered by implementation internals.
// EXPECTED TO FAIL: Bug A corrupts stream; readWalRecords() returns wrong count.
TEST(WalCommit, CommitPreservesAppendOrder) {
    TempFile tmp{makeTempPath()};
    WriteAheadLog wal{makeConfig(tmp.path)};

    for (int i = 0; i < 5; ++i) {
        wal.append({}, /*event_type=*/1);
    }
    wal.commit();

    auto records = readWalRecords(tmp.path);
    ASSERT_EQ(records.size(), 5u);
    for (std::size_t i = 0; i < 5; ++i) {
        EXPECT_EQ(records[i].header.seq, i);
    }
}
