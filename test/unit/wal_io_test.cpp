#include <gtest/gtest.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "wal_test_helpers.hpp"

using namespace ledgerflow::wal;
using namespace ledgerflow::test;

// ---------------------------------------------------------------------------
// readAll / writeAll
// ---------------------------------------------------------------------------

TEST(WalIo, ReadWriteAll_RoundTrip) {
    TempFile tmp{makeTempPath()};
    const int fd = ::open(tmp.path.c_str(), O_RDWR | O_TRUNC);
    ASSERT_NE(fd, -1);

    const std::vector<std::byte> src{
        std::byte{0x01}, std::byte{0x02}, std::byte{0x03}, std::byte{0xFF}};
    EXPECT_TRUE(writeAll(fd, src.data(), src.size()));

    ASSERT_EQ(::lseek(fd, 0, SEEK_SET), 0);

    std::vector<std::byte> dst(src.size());
    EXPECT_EQ(readAll(fd, dst.data(), dst.size()), ReadStatus::Ok);
    EXPECT_EQ(src, dst);

    ::close(fd);
}

TEST(WalIo, ReadAll_ReturnsEoF_OnEmptyFile) {
    TempFile tmp{makeTempPath()};
    const int fd = ::open(tmp.path.c_str(), O_RDONLY);
    ASSERT_NE(fd, -1);

    std::byte buf{};
    EXPECT_EQ(readAll(fd, &buf, 1), ReadStatus::EoF);

    ::close(fd);
}

// ---------------------------------------------------------------------------
// writeNextWalRecord / readNextWalRecord
// ---------------------------------------------------------------------------

TEST(WalIo, WriteNextReadNext_RoundTrip) {
    TempFile tmp{makeTempPath()};
    const int fd = ::open(tmp.path.c_str(), O_RDWR | O_TRUNC);
    ASSERT_NE(fd, -1);

    WalRecord out_rec{};
    out_rec.header.magic      = WalMagicBoundaryByte;
    out_rec.header.version    = WalVersion;
    out_rec.header.event_type = 7;
    out_rec.header.crc32      = 0;
    out_rec.data = {std::byte{0xAA}, std::byte{0xBB}, std::byte{0xCC}};
    out_rec.header.length = sizeof(WalFrameHeader) + out_rec.data.size();
    out_rec.header.seq    = 42;

    ASSERT_TRUE(writeNextWalRecord(fd, out_rec));
    ASSERT_EQ(::lseek(fd, 0, SEEK_SET), 0);

    WalRecord in_rec{};
    ASSERT_EQ(readNextWalRecord(fd, &in_rec), ReadStatus::Ok);

    EXPECT_EQ(in_rec.header.magic,      WalMagicBoundaryByte);
    EXPECT_EQ(in_rec.header.version,    WalVersion);
    EXPECT_EQ(in_rec.header.event_type, 7u);
    EXPECT_EQ(in_rec.header.seq,        42u);
    EXPECT_EQ(in_rec.header.length,     sizeof(WalFrameHeader) + 3u);
    EXPECT_EQ(in_rec.data,              out_rec.data);

    ::close(fd);
}

TEST(WalIo, ReadNextWalRecord_Corrupted_OnBadMagic) {
    TempFile tmp{makeTempPath()};
    const int fd = ::open(tmp.path.c_str(), O_RDWR | O_TRUNC);
    ASSERT_NE(fd, -1);

    // Write a header-sized block with wrong magic.
    WalFrameHeader bad{};
    bad.magic   = 0xDEADBEEF;
    bad.version = WalVersion;
    bad.length  = sizeof(WalFrameHeader);
    ASSERT_TRUE(writeAll(fd, &bad, sizeof(bad)));
    ASSERT_EQ(::lseek(fd, 0, SEEK_SET), 0);

    WalRecord rec{};
    EXPECT_EQ(readNextWalRecord(fd, &rec), ReadStatus::Corrupted);

    ::close(fd);
}

TEST(WalIo, ReadNextWalRecord_EoF_OnEmptyFile) {
    TempFile tmp{makeTempPath()};
    const int fd = ::open(tmp.path.c_str(), O_RDONLY);
    ASSERT_NE(fd, -1);

    WalRecord rec{};
    EXPECT_EQ(readNextWalRecord(fd, &rec), ReadStatus::EoF);

    ::close(fd);
}

// ---------------------------------------------------------------------------
// Critical format test
// ---------------------------------------------------------------------------

// Verifies that writeNextWalRecord (the correct helper) writes [header][payload]
// with no extra bytes. This passes because the helper is correct.
TEST(WalFormat, WriteNextWalRecord_CorrectLayout) {
    TempFile tmp{makeTempPath()};
    const int fd = ::open(tmp.path.c_str(), O_RDWR | O_TRUNC);
    ASSERT_NE(fd, -1);

    WalRecord rec{};
    rec.header.magic      = WalMagicBoundaryByte;
    rec.header.version    = WalVersion;
    rec.header.event_type = 1;
    rec.header.crc32      = 0;
    rec.data              = {std::byte{0xAA}, std::byte{0xBB}, std::byte{0xCC}};
    rec.header.length     = sizeof(WalFrameHeader) + rec.data.size();
    rec.header.seq        = 0;

    ASSERT_TRUE(writeNextWalRecord(fd, rec));

    // File must be exactly sizeof(WalFrameHeader) + 3 bytes.
    struct stat st{};
    ASSERT_EQ(::fstat(fd, &st), 0);
    EXPECT_EQ(static_cast<std::size_t>(st.st_size), sizeof(WalFrameHeader) + 3u);

    // Verify raw payload bytes sit immediately after the header.
    ASSERT_EQ(::lseek(fd, static_cast<off_t>(sizeof(WalFrameHeader)), SEEK_SET),
              static_cast<off_t>(sizeof(WalFrameHeader)));
    std::vector<std::byte> payload_on_disk(3);
    ASSERT_EQ(readAll(fd, payload_on_disk.data(), 3), ReadStatus::Ok);
    EXPECT_EQ(payload_on_disk, rec.data);

    ::close(fd);
}

// Intent: the WAL on-disk format must be [WalFrameHeader][payload bytes].
// EXPECTED TO FAIL: commitRecordToFile() writes sizeof(WalRecord) bytes,
// which includes the std::vector object's internal pointer/size/capacity
// rather than the actual payload. File will be 48 bytes (sizeof WalRecord),
// not sizeof(WalFrameHeader)+3 = 27. This exposes Bug A.
TEST(WalFormat, CRITICAL_OnDiskLayoutIsHeaderThenPayload) {
    TempFile tmp{makeTempPath()};
    const std::vector<std::byte> payload{std::byte{0xAA}, std::byte{0xBB}, std::byte{0xCC}};
    {
        WriteAheadLog wal{makeConfig(tmp.path)};
        wal.append(payload, /*event_type=*/1);
    } // destructor flushes via commitRecordToFile (the buggy path)

    // File size must equal exactly one framed record: header + 3 payload bytes.
    constexpr std::size_t expected_size = sizeof(WalFrameHeader) + 3u;
    struct stat st{};
    ASSERT_EQ(::stat(tmp.path.c_str(), &st), 0);
    ASSERT_EQ(static_cast<std::size_t>(st.st_size), expected_size);

    // Payload bytes must appear at offset sizeof(WalFrameHeader).
    const int fd = ::open(tmp.path.c_str(), O_RDONLY);
    ASSERT_NE(fd, -1);
    ASSERT_EQ(::lseek(fd, static_cast<off_t>(sizeof(WalFrameHeader)), SEEK_SET),
              static_cast<off_t>(sizeof(WalFrameHeader)));
    std::vector<std::byte> payload_on_disk(3);
    ASSERT_EQ(readAll(fd, payload_on_disk.data(), 3), ReadStatus::Ok);
    EXPECT_EQ(payload_on_disk, payload);
    ::close(fd);
}
