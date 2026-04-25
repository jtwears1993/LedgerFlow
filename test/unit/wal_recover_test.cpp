#include <gtest/gtest.h>

#include "wal_test_helpers.hpp"

// All tests in this file define FUTURE behavior for recover().
// recover() is currently a no-op. Enable each test as the implementation lands.
// Run with --gtest_also_run_disabled_tests to verify failure against current code.

using namespace ledgerflow::wal;
using namespace ledgerflow::test;

// Intent: after recover(), records previously written to the WAL file are
// accessible and the sequencer continues from the last written sequence number.
TEST(WalRecover, DISABLED_RecoverLoadsWalRecords) {
    TempFile tmp{makeTempPath()};
    constexpr int N = 5;

    // Write N records with the first WAL instance.
    {
        WriteAheadLog wal{makeConfig(tmp.path)};
        for (int i = 0; i < N; ++i) {
            core::CommitRecord rec{};
            rec.hdr.seq = static_cast<uint64_t>(i);
            wal.append(rec);
        }
    }

    // Re-open and recover; append one more record.
    {
        WriteAheadLog wal{makeConfig(tmp.path)};
        wal.recover();
        core::CommitRecord rec{};
        wal.append(rec);
    }

    // Expect N+1 records; the last record's sequence number must be N
    // (continuing from where the first WAL left off).
    auto records = readWalRecords(tmp.path);
    ASSERT_EQ(records.size(), static_cast<std::size_t>(N + 1));
    EXPECT_EQ(records.back().sequence_number, static_cast<uint64_t>(N));
}

// Intent: recover() must replay records in the exact order they were written.
// Out-of-order recovery breaks transaction causality.
TEST(WalRecover, DISABLED_RecoverPreservesSequenceOrder) {
    TempFile tmp{makeTempPath()};
    constexpr int N = 8;
    {
        WriteAheadLog wal{makeConfig(tmp.path)};
        for (int i = 0; i < N; ++i) {
            core::CommitRecord rec{};
            rec.hdr.seq = static_cast<uint64_t>(i * 3);
            wal.append(rec);
        }
    }

    {
        WriteAheadLog wal{makeConfig(tmp.path)};
        EXPECT_NO_THROW(wal.recover());
    }

    auto records = readWalRecords(tmp.path);
    ASSERT_EQ(records.size(), static_cast<std::size_t>(N));
    for (int i = 0; i < N; ++i) {
        EXPECT_EQ(records[i].commit.hdr.seq, static_cast<uint64_t>(i * 3));
    }
}

// Intent: recover() on a freshly created (empty) WAL file must not throw
// and must leave the WAL in a functional state with sequencer starting at 0.
TEST(WalRecover, DISABLED_HandlesEmptyFile) {
    TempFile tmp{makeTempPath()};
    WriteAheadLog wal{makeConfig(tmp.path)};
    EXPECT_NO_THROW(wal.recover());

    core::CommitRecord rec{};
    wal.append(rec);
    {
        // re-scope to flush
        WriteAheadLog wal2{makeConfig(tmp.path)};
        wal2.recover();
    }

    // After recover on empty, sequencer must start at 0.
    auto records = readWalRecords(tmp.path);
    EXPECT_EQ(records.front().sequence_number, 0u);
}

// Intent: if the WAL file does not exist, recover() creates one and
// initialises a clean sequencer state without throwing.
TEST(WalRecover, DISABLED_HandlesMissingFileSafely) {
    // Use a path that does not exist yet (mkstemp creates it; unlink so it's gone).
    std::string path = makeTempPath();
    ::unlink(path.c_str());

    // Construction creates the file via O_CREAT.
    WriteAheadLog wal{makeConfig(path)};
    EXPECT_NO_THROW(wal.recover());

    ::unlink(path.c_str());
}
