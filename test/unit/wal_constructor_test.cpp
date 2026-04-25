#include <gtest/gtest.h>
#include <sys/stat.h>

#include "wal_test_helpers.hpp"

using namespace ledgerflow::wal;
using namespace ledgerflow::test;

TEST(WalConstructor, ValidConfigConstructs) {
    TempFile tmp{makeTempPath()};
    EXPECT_NO_THROW(WriteAheadLog wal{makeConfig(tmp.path)});
}

TEST(WalConstructor, FsyncModeAlwaysWorks) {
    TempFile tmp{makeTempPath()};
    auto cfg = makeConfig(tmp.path);
    cfg.fsync_mode = FsyncMode::Always;
    EXPECT_NO_THROW(WriteAheadLog wal{cfg});
}

TEST(WalConstructor, FsyncModeTimeThrows) {
    TempFile tmp{makeTempPath()};
    auto cfg = makeConfig(tmp.path);
    cfg.fsync_mode = FsyncMode::Time;
    EXPECT_THROW(WriteAheadLog wal{cfg}, std::runtime_error);
}

TEST(WalConstructor, FsyncModeBatchThrows) {
    TempFile tmp{makeTempPath()};
    auto cfg = makeConfig(tmp.path);
    cfg.fsync_mode = FsyncMode::Batch;
    EXPECT_THROW(WriteAheadLog wal{cfg}, std::runtime_error);
}

TEST(WalConstructor, FsyncModeNoneThrows) {
    TempFile tmp{makeTempPath()};
    auto cfg = makeConfig(tmp.path);
    cfg.fsync_mode = FsyncMode::None;
    EXPECT_THROW(WriteAheadLog wal{cfg}, std::runtime_error);
}

TEST(WalConstructor, DestructorFlushesRecordsToFile) {
    TempFile tmp{makeTempPath()};
    {
        WriteAheadLog wal{makeConfig(tmp.path)};
        core::CommitRecord rec{};
        rec.hdr.seq = 42;
        rec.line_count = 3;
        wal.append(rec);
    }
    struct stat st{};
    ASSERT_EQ(::stat(tmp.path.c_str(), &st), 0);
    EXPECT_EQ(static_cast<std::size_t>(st.st_size), sizeof(WalRecord));
}

TEST(WalConstructor, NoCrashOnLifecycle) {
    TempFile tmp{makeTempPath()};
    EXPECT_NO_THROW({ WriteAheadLog wal{makeConfig(tmp.path)}; });
}
