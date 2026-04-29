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

// Intent: destructor must flush buffered records so the file holds exactly
// sizeof(WalFrameHeader) + payload bytes per record.
// EXPECTED TO FAIL: commitRecordToFile() writes sizeof(WalRecord)=48 bytes
// (includes vector internals) instead of sizeof(WalFrameHeader)+payload.
// Actual file size will be 48, not 26. Exposes Bug A.
TEST(WalConstructor, DestructorFlushesRecordsToFile) {
    TempFile tmp{makeTempPath()};
    const std::vector<std::byte> payload{std::byte{0x01}, std::byte{0x02}};
    {
        WriteAheadLog wal{makeConfig(tmp.path)};
        wal.append(payload, /*event_type=*/1);
    }
    struct stat st{};
    ASSERT_EQ(::stat(tmp.path.c_str(), &st), 0);
    constexpr std::size_t expected = sizeof(WalFrameHeader) + 2;
    EXPECT_EQ(static_cast<std::size_t>(st.st_size), expected);
}

TEST(WalConstructor, NoCrashOnLifecycle) {
    TempFile tmp{makeTempPath()};
    EXPECT_NO_THROW({ WriteAheadLog wal{makeConfig(tmp.path)}; });
}
