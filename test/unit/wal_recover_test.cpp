#include <gtest/gtest.h>

#include "wal_test_helpers.hpp"

using namespace ledgerflow::wal;
using namespace ledgerflow::test;

// Intent: after recover(), records previously written to the WAL file are
// loaded into out and the sequencer continues from the last written seq.

TEST(WalRecover, RecoverLoadsWalRecords) {
    TempFile tmp{makeTempPath()};
    constexpr int N = 5;

    {
        WriteAheadLog wal{makeConfig(tmp.path)};
        for (int i = 0; i < N; ++i) {
            wal.append({}, /*event_type=*/1);
        }
    }

    std::vector<WalRecord> recovered;
    {
        WriteAheadLog wal{makeConfig(tmp.path)};
        wal.recover(recovered);
        // wal.append({}, /*event_type=*/1); // seq should continue from N
        // wal.commit();
    }

    // Expect N records recovered + 1 new record on disk.
    ASSERT_EQ(recovered.size(), static_cast<std::size_t>(N));
    auto all = readWalRecords(tmp.path);
    ASSERT_EQ(all.size(), static_cast<std::size_t>(N));
    EXPECT_EQ(all.back().header.seq, static_cast<uint64_t>(N - 1)); // seq starts from 0 ->  5 records 0,1,2,3,4
}

// Intent: recover() must replay records in the exact order they were written.
// EXPECTED TO FAIL: Bug A + Bug B — only 1 record returned, order meaningless.
TEST(WalRecover, RecoverPreservesSequenceOrder) {
    TempFile tmp{makeTempPath()};
    constexpr int N = 8;
    {
        WriteAheadLog wal{makeConfig(tmp.path)};
        for (int i = 0; i < N; ++i) {
            wal.append({}, /*event_type=*/1);
        }
    }

    std::vector<WalRecord> recovered;
    {
        WriteAheadLog wal{makeConfig(tmp.path)};
        EXPECT_NO_THROW(wal.recover(recovered));
    }

    ASSERT_EQ(recovered.size(), static_cast<std::size_t>(N));
    for (int i = 0; i < N; ++i) {
        EXPECT_EQ(recovered[i].header.seq, static_cast<uint64_t>(i));
    }
}

// Intent: recover() on a freshly created (empty) WAL file must not throw
// and must leave the sequencer starting at 0.
// EXPECTED TO FAIL: Bug B — after recover on empty file, appending one record
// and re-opening yields a second recover that stops early; sequencer may not
// seed correctly.
TEST(WalRecover, HandlesEmptyFile) {
    TempFile tmp{makeTempPath()};
    {
        WriteAheadLog wal{makeConfig(tmp.path)};
        std::vector<WalRecord> out;
        EXPECT_NO_THROW(wal.recover(out));
        EXPECT_TRUE(out.empty());
        wal.append({}, /*event_type=*/1);
    }

    std::vector<WalRecord> recovered;
    {
        WriteAheadLog wal{makeConfig(tmp.path)};
        wal.recover(recovered);
    }

    // After recovering one record, seq in that record must be 0.
    ASSERT_EQ(recovered.size(), 1u);
    EXPECT_EQ(recovered[0].header.seq, 0u);
}

// Intent: if the WAL file does not exist yet, the constructor creates it via
// O_CREAT and recover() on the resulting empty file must not throw.
TEST(WalRecover, HandlesMissingFileSafely) {
    std::string path = makeTempPath();
    ::unlink(path.c_str()); // remove so the path truly doesn't exist

    WriteAheadLog wal{makeConfig(path)};
    std::vector<WalRecord> out;
    EXPECT_NO_THROW(wal.recover(out));
    EXPECT_TRUE(out.empty());

    ::unlink(path.c_str());
}
