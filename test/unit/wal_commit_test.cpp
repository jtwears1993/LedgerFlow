#include <gtest/gtest.h>

#include "wal_test_helpers.hpp"

// All tests in this file define FUTURE behavior for commit().
// commit() is currently a no-op. Enable each test as the implementation lands.
// Run with --gtest_also_run_disabled_tests to verify failure against current code.

using namespace ledgerflow::wal;
using namespace ledgerflow::test;

// Intent: records appended before commit() are visible on disk immediately
// after commit() returns, without requiring WAL destruction.
TEST(WalCommit, DISABLED_CommitFlushesBufferToDisk) {
    TempFile tmp{makeTempPath()};
    WriteAheadLog wal{makeConfig(tmp.path)};

    core::CommitRecord rec{};
    rec.hdr.seq = 1;
    wal.append(rec);
    wal.commit();

    auto records = readWalRecords(tmp.path);
    EXPECT_EQ(records.size(), 1u);
}

// Intent: after commit() returns in FsyncMode::Always, data is durable on
// the storage device — not just in the OS page cache.
TEST(WalCommit, DISABLED_CommitFsyncsInAlwaysMode) {
    TempFile tmp{makeTempPath()};
    auto cfg = makeConfig(tmp.path);
    cfg.fsync_mode = FsyncMode::Always;
    WriteAheadLog wal{cfg};

    core::CommitRecord rec{};
    rec.hdr.seq = 99;
    wal.append(rec);
    EXPECT_NO_THROW(wal.commit());

    auto records = readWalRecords(tmp.path);
    ASSERT_EQ(records.size(), 1u);
    EXPECT_EQ(records[0].commit.hdr.seq, 99u);
}

// Intent: calling commit() multiple times or on an empty buffer must not
// error or duplicate records already flushed.
TEST(WalCommit, DISABLED_CommitIsIdempotent) {
    TempFile tmp{makeTempPath()};
    WriteAheadLog wal{makeConfig(tmp.path)};

    EXPECT_NO_THROW(wal.commit());
    EXPECT_NO_THROW(wal.commit());

    core::CommitRecord rec{};
    rec.hdr.seq = 1;
    wal.append(rec);
    wal.commit();
    wal.commit();

    auto records = readWalRecords(tmp.path);
    EXPECT_EQ(records.size(), 1u);
}

// Intent: records committed to disk must appear in the same order they were
// appended, not reordered by implementation internals.
TEST(WalCommit, DISABLED_CommitPreservesAppendOrder) {
    TempFile tmp{makeTempPath()};
    WriteAheadLog wal{makeConfig(tmp.path)};

    for (uint64_t i = 0; i < 5; ++i) {
        core::CommitRecord rec{};
        rec.hdr.seq = i;
        wal.append(rec);
    }
    wal.commit();

    auto records = readWalRecords(tmp.path);
    ASSERT_EQ(records.size(), 5u);
    for (std::size_t i = 0; i < 5; ++i) {
        EXPECT_EQ(records[i].commit.hdr.seq, i);
    }
}
