// WAL Microbenchmark Suite — LedgerFlow
//
// Sections:
//   1   Lifecycle          — construct / destroy
//   2   Record Encoding    — CPU only, no IO
//   3   Append to Memory   — ring-buffer push, no IO
//   4   Write Without Fsync — syscall cost, page-cache only
//   5   fsync / fdatasync  — isolated sync cost
//   6   Durability Path    — commit() and destructor flush
//   7   Transactions       — realistic fill-transaction shape
//   8   Group Commit       — batching policy sweep
//   9   Recovery           — WAL replay (warm and cold-ish)
//  10   Sequential Read    — readNextWalRecord throughput
//  11   CRC Overhead       — checksum cost (software CRC32)
//
// ============================================================
// For meaningful IO numbers:
//   sudo cpupower frequency-set -g performance
//   taskset -c 0 ./ledgerflow_benchmarks
//   Record filesystem / storage device in BENCHMARK.md.
//   IO benchmarks use UseRealTime(); CPU-only benchmarks do not.
// ============================================================

#include <benchmark/benchmark.h>
#include <fcntl.h>
#include <sys/uio.h>
#include <unistd.h>
#include <cstring>

#include "ledgerflow/wal/wal.hpp"

using namespace ledgerflow::wal;

// ============================================================
// Local record types — fixed-size ledger transaction payload
// ============================================================

#pragma pack(push, 1)
// One fill transaction = TxnHeaderRecord + 4×TxnLineRecord + CommitRecord
struct TxnHeaderRecord {
    uint64_t txn_id;     //  8
    int64_t  ts_ns;      //  8
    uint16_t line_count; //  2
    uint8_t  _pad[6];    //  6  → 24 bytes
};
struct TxnLineRecord {
    uint64_t account_id; //  8
    int64_t  amount;     //  8
    uint8_t  currency[3];//  3
    uint8_t  _pad[5];    //  5  → 24 bytes
};
struct CommitRecord {
    uint64_t txn_id;     //  8
    uint32_t line_count; //  4
    uint32_t checksum;   //  4  → 16 bytes
};
#pragma pack(pop)

static_assert(sizeof(TxnHeaderRecord) == 24);
static_assert(sizeof(TxnLineRecord)   == 24);
static_assert(sizeof(CommitRecord)    == 16);

// ============================================================
// Constants
// ============================================================

namespace {

constexpr int kLinesPerTxn     = 4;
constexpr int kRecordsPerTxn   = 1 + kLinesPerTxn + 1; // header + lines + commit
// Bytes on disk per fill transaction when serialised correctly:
// 1*(24+24) + 4*(24+24) + 1*(24+16) = 48 + 192 + 40 = 280
constexpr std::size_t kBytesPerTxn =
    (sizeof(WalFrameHeader) + sizeof(TxnHeaderRecord)) +
    kLinesPerTxn * (sizeof(WalFrameHeader) + sizeof(TxnLineRecord)) +
    (sizeof(WalFrameHeader) + sizeof(CommitRecord));
static_assert(kBytesPerTxn == 280);

constexpr uint16_t kEvtHeader = 1;
constexpr uint16_t kEvtLine   = 2;
constexpr uint16_t kEvtCommit = 3;

// ============================================================
// Software CRC32 (table-less, for benchmark isolation only)
// A production implementation should use hardware CRC or a
// table-based variant. This intentionally mirrors a real cost.
// ============================================================

uint32_t crc32_sw(const void* data, std::size_t len) {
    uint32_t crc = 0xFFFFFFFFu;
    const auto* p = static_cast<const uint8_t*>(data);
    for (std::size_t i = 0; i < len; ++i) {
        crc ^= static_cast<uint32_t>(p[i]);
        for (int b = 0; b < 8; ++b)
            crc = (crc >> 1) ^ (0xEDB88320u & ~((crc & 1u) - 1u));
    }
    return ~crc;
}

// ============================================================
// Helpers
// ============================================================

std::string makeTempPath() {
    char tmpl[] = "/tmp/wal_bench_XXXXXX";
    int fd = ::mkstemp(tmpl);
    if (fd != -1) ::close(fd);
    return std::string(tmpl);
}

WalConfig makeConfig(const std::string& path,
                     std::size_t capacity = DefaultWalCapacity) {
    WalConfig cfg;
    cfg.wal_file_path = path;
    cfg.capacity      = capacity;
    return cfg;
}

template <typename T>
std::vector<std::byte> toBytes(const T& v) {
    std::vector<std::byte> out(sizeof(T));
    std::memcpy(out.data(), &v, sizeof(T));
    return out;
}

// Build a WalRecord with correct header fields (crc32 stubbed to 0).
template <typename T>
WalRecord makeRecord(uint16_t event_type, const T& payload, uint64_t seq) {
    WalRecord r{};
    r.header.magic      = WalMagicBoundaryByte;
    r.header.version    = WalVersion;
    r.header.event_type = event_type;
    r.header.crc32      = 0;
    r.header.seq        = seq;
    r.data              = toBytes(payload);
    r.header.length     = static_cast<uint32_t>(sizeof(WalFrameHeader) + r.data.size());
    return r;
}

// Pre-write N simple 1-byte records (used by read benchmarks).
void prewriteRecords(const std::string& path, int n) {
    const int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    const std::byte kByte{0x01};
    for (int i = 0; i < n; ++i) {
        WalRecord r{};
        r.header.magic      = WalMagicBoundaryByte;
        r.header.version    = WalVersion;
        r.header.event_type = kEvtLine;
        r.header.crc32      = 0;
        r.header.seq        = static_cast<uint64_t>(i);
        r.data              = {kByte};
        r.header.length     = sizeof(WalFrameHeader) + 1u;
        writeNextWalRecord(fd, r);
    }
    ::fsync(fd);
    ::close(fd);
}

// Pre-write N fill transactions (TxnHeader + 4×TxnLine + Commit per txn).
// Uses writeNextWalRecord so format is correct regardless of WAL class bugs.
void prewriteTransactions(const std::string& path, int num_txns) {
    const int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    uint64_t seq = 0;
    for (int t = 0; t < num_txns; ++t) {
        TxnHeaderRecord hdr{};
        hdr.txn_id     = static_cast<uint64_t>(t);
        hdr.ts_ns      = static_cast<int64_t>(t) * 1000;
        hdr.line_count = kLinesPerTxn;
        writeNextWalRecord(fd, makeRecord(kEvtHeader, hdr, seq++));

        for (int l = 0; l < kLinesPerTxn; ++l) {
            TxnLineRecord line{};
            line.account_id = static_cast<uint64_t>(l + 1);
            line.amount     = (l % 2 == 0) ? 10000L : -10000L;
            line.currency[0] = 'U'; line.currency[1] = 'S'; line.currency[2] = 'D';
            writeNextWalRecord(fd, makeRecord(kEvtLine, line, seq++));
        }

        CommitRecord commit{};
        commit.txn_id     = static_cast<uint64_t>(t);
        commit.line_count = kLinesPerTxn;
        commit.checksum   = 0;
        writeNextWalRecord(fd, makeRecord(kEvtCommit, commit, seq++));
    }
    ::fsync(fd);
    ::close(fd);
}

// One-byte and transaction-shaped static payloads.
const std::byte         kOneByte{0x01};
const std::vector<std::byte> kPayload1{kOneByte};
const TxnHeaderRecord   kHdrPayload{1, 1000000LL, kLinesPerTxn, {}};
const TxnLineRecord     kLinePayload{42, 10000LL, {'U','S','D'}, {}};
const CommitRecord      kCommitPayload{1, kLinesPerTxn, 0};

} // namespace

// ============================================================
// SECTION 1 — Lifecycle
// ============================================================

// Measures open() + empty destructor flush (fsync + close).
// Use real time: fsync dominates.
static void BM_Lifecycle_ConstructDestroy(benchmark::State& state) {
    for (auto _ : state) {
        state.PauseTiming();
        auto path = makeTempPath();
        auto cfg  = makeConfig(path);
        state.ResumeTiming();

        { WriteAheadLog wal{cfg}; }

        state.PauseTiming();
        ::unlink(path.c_str());
        state.ResumeTiming();
    }
}
BENCHMARK(BM_Lifecycle_ConstructDestroy)->UseRealTime()->MinTime(3.0);

// ============================================================
// SECTION 2 — Record Encoding (CPU only, no IO)
// Measures: header population + payload copy into vector.
// Excludes: ring-buffer push, IO, fsync.
// ============================================================

// Measures encoding a single TxnHeaderRecord into a WalRecord in memory.
static void BM_Encode_TxnHeader(benchmark::State& state) {
    uint64_t seq = 0;
    for (auto _ : state) {
        auto r = makeRecord(kEvtHeader, kHdrPayload, seq++);
        benchmark::DoNotOptimize(r);
    }
    state.SetItemsProcessed(static_cast<int64_t>(state.iterations()));
}
BENCHMARK(BM_Encode_TxnHeader);

// Measures encoding a single TxnLineRecord into a WalRecord in memory.
static void BM_Encode_TxnLine(benchmark::State& state) {
    uint64_t seq = 0;
    for (auto _ : state) {
        auto r = makeRecord(kEvtLine, kLinePayload, seq++);
        benchmark::DoNotOptimize(r);
    }
    state.SetItemsProcessed(static_cast<int64_t>(state.iterations()));
}
BENCHMARK(BM_Encode_TxnLine);

// Measures encoding a CommitRecord into a WalRecord in memory.
static void BM_Encode_CommitRecord(benchmark::State& state) {
    uint64_t seq = 0;
    for (auto _ : state) {
        auto r = makeRecord(kEvtCommit, kCommitPayload, seq++);
        benchmark::DoNotOptimize(r);
    }
    state.SetItemsProcessed(static_cast<int64_t>(state.iterations()));
}
BENCHMARK(BM_Encode_CommitRecord);

// Measures encoding one full fill transaction (6 WalRecords) in memory.
// Excludes IO, ring-buffer push, fsync.
static void BM_Encode_FillTransaction(benchmark::State& state) {
    uint64_t seq = 0;
    for (auto _ : state) {
        auto hdr = makeRecord(kEvtHeader, kHdrPayload, seq++);
        benchmark::DoNotOptimize(hdr);
        for (int l = 0; l < kLinesPerTxn; ++l) {
            auto line = makeRecord(kEvtLine, kLinePayload, seq++);
            benchmark::DoNotOptimize(line);
        }
        auto commit = makeRecord(kEvtCommit, kCommitPayload, seq++);
        benchmark::DoNotOptimize(commit);
    }
    state.SetItemsProcessed(static_cast<int64_t>(state.iterations()));
    state.counters["records_per_txn"] = kRecordsPerTxn;
}
BENCHMARK(BM_Encode_FillTransaction);

// ============================================================
// SECTION 3 — Append to Memory Buffer (CPU only, no IO)
// Measures: WAL ring-buffer push + sequencer increment.
// Excludes: IO, fsync. Buffer sized to never fill.
// ============================================================

// Measures single append into the ring buffer (hot path, no flush).
static void BM_Append_Single(benchmark::State& state) {
    auto path = makeTempPath();
    WriteAheadLog wal{makeConfig(path, 1u << 20)};

    for (auto _ : state) {
        wal.append(kPayload1, kEvtLine);
        benchmark::ClobberMemory();
    }

    state.SetItemsProcessed(static_cast<int64_t>(state.iterations()));
    ::unlink(path.c_str());
}
BENCHMARK(BM_Append_Single)->Iterations(1 << 20);

// Measures N appends per iteration; WAL is destroyed outside the timed section.
// Reports in-memory throughput only.
static void BM_Append_Many(benchmark::State& state) {
    const auto n = state.range(0);
    for (auto _ : state) {
        state.PauseTiming();
        auto  path = makeTempPath();
        auto* wal  = new WriteAheadLog{makeConfig(path, static_cast<std::size_t>(n))};
        state.ResumeTiming();

        for (int64_t i = 0; i < n; ++i)
            wal->append(kPayload1, kEvtLine);
        benchmark::ClobberMemory();

        state.PauseTiming();
        delete wal;
        ::unlink(path.c_str());
        state.ResumeTiming();
    }
    state.SetItemsProcessed(state.iterations() * n);
    state.SetBytesProcessed(state.iterations() * n *
        static_cast<int64_t>(sizeof(WalFrameHeader) + 1));
}
BENCHMARK(BM_Append_Many)->RangeMultiplier(4)->Range(1, 512)->Iterations(200);

// ============================================================
// SECTION 4 — Write Syscall Without fsync
// Measures: write() page-cache cost only.
// Excludes: fsync, ring-buffer, WAL class overhead.
// ============================================================

// Measures one writeNextWalRecord() call per iteration without fsync.
// Uses real time: syscall latency is wall-clock sensitive.
static void BM_Write_SingleRecord_NoFsync(benchmark::State& state) {
    auto     path = makeTempPath();
    const int fd  = ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    auto rec = makeRecord(kEvtLine, kLinePayload, 0);
    uint64_t seq  = 0;

    for (auto _ : state) {
        rec.header.seq = seq++;
        benchmark::DoNotOptimize(writeNextWalRecord(fd, rec));
    }

    state.SetItemsProcessed(static_cast<int64_t>(state.iterations()));
    state.SetBytesProcessed(state.iterations() *
        static_cast<int64_t>(sizeof(WalFrameHeader) + sizeof(TxnLineRecord)));
    ::close(fd);
    ::unlink(path.c_str());
}
BENCHMARK(BM_Write_SingleRecord_NoFsync)->UseRealTime()->MinTime(3.0);

// Measures writing N records per iteration to a fresh file, no fsync.
static void BM_Write_BatchRecords_NoFsync(benchmark::State& state) {
    const auto n = state.range(0);
    for (auto _ : state) {
        state.PauseTiming();
        auto     path = makeTempPath();
        const int fd  = ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        auto rec = makeRecord(kEvtLine, kLinePayload, 0);
        state.ResumeTiming();

        for (int64_t i = 0; i < n; ++i) {
            rec.header.seq = static_cast<uint64_t>(i);
            writeNextWalRecord(fd, rec);
        }
        benchmark::ClobberMemory();

        state.PauseTiming();
        ::close(fd);
        ::unlink(path.c_str());
        state.ResumeTiming();
    }
    state.SetItemsProcessed(state.iterations() * n);
    state.SetBytesProcessed(state.iterations() * n *
        static_cast<int64_t>(sizeof(WalFrameHeader) + sizeof(TxnLineRecord)));
}
BENCHMARK(BM_Write_BatchRecords_NoFsync)
    ->UseRealTime()->RangeMultiplier(4)->Range(1, 512)->MinTime(3.0);

// Measures writev() — writes header iov + payload iov in one syscall.
// Avoids the two-write cost of writeNextWalRecord for small payloads.
static void BM_Write_Writev_SingleRecord_NoFsync(benchmark::State& state) {
    auto     path = makeTempPath();
    const int fd  = ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    auto rec = makeRecord(kEvtLine, kLinePayload, 0);
    uint64_t seq  = 0;

    for (auto _ : state) {
        rec.header.seq = seq++;
        struct iovec iov[2];
        iov[0].iov_base = &rec.header;
        iov[0].iov_len  = sizeof(WalFrameHeader);
        iov[1].iov_base = rec.data.data();
        iov[1].iov_len  = rec.data.size();
        benchmark::DoNotOptimize(::writev(fd, iov, 2));
    }

    state.SetItemsProcessed(static_cast<int64_t>(state.iterations()));
    state.SetBytesProcessed(state.iterations() *
        static_cast<int64_t>(sizeof(WalFrameHeader) + sizeof(TxnLineRecord)));
    ::close(fd);
    ::unlink(path.c_str());
}
BENCHMARK(BM_Write_Writev_SingleRecord_NoFsync)->UseRealTime()->MinTime(3.0);

// ============================================================
// SECTION 5 — fsync / fdatasync Isolated Cost
// Measures: how long a sync call takes after N dirty bytes.
// Excludes: encoding, ring-buffer; write() is in PauseTiming.
// ============================================================

// Measures fsync(fd) after N dirty bytes. Ranges over realistic sizes.
static void BM_Fsync_AfterNBytes(benchmark::State& state) {
    const auto nbytes = state.range(0);
    std::vector<std::byte> buf(static_cast<std::size_t>(nbytes), std::byte{0xAB});

    auto     path = makeTempPath();
    const int fd  = ::open(path.c_str(), O_RDWR | O_CREAT | O_TRUNC, 0644);

    for (auto _ : state) {
        state.PauseTiming();
        // Seek to 0 and overwrite to re-dirty the pages without truncating.
        ::lseek(fd, 0, SEEK_SET);
        writeAll(fd, buf.data(), buf.size());
        state.ResumeTiming();

        ::fsync(fd);
        benchmark::ClobberMemory();
    }

    state.SetBytesProcessed(state.iterations() * nbytes);
    ::close(fd);
    ::unlink(path.c_str());
}
BENCHMARK(BM_Fsync_AfterNBytes)
    ->UseRealTime()
    ->Args({static_cast<int64_t>(kBytesPerTxn)})
    ->Args({4096})
    ->Args({65536})
    ->Args({1 << 20})
    ->MinTime(3.0);

// Measures fdatasync(fd) after N dirty bytes.
// fdatasync skips metadata updates (mtime) so may be faster than fsync.
static void BM_Fdatasync_AfterNBytes(benchmark::State& state) {
    const auto nbytes = state.range(0);
    std::vector<std::byte> buf(static_cast<std::size_t>(nbytes), std::byte{0xAB});

    auto     path = makeTempPath();
    const int fd  = ::open(path.c_str(), O_RDWR | O_CREAT | O_TRUNC, 0644);

    for (auto _ : state) {
        state.PauseTiming();
        // Seek to 0 and overwrite to re-dirty the pages without truncating.
        ::lseek(fd, 0, SEEK_SET);
        writeAll(fd, buf.data(), buf.size());
        state.ResumeTiming();

        ::fdatasync(fd);
        benchmark::ClobberMemory();
    }

    state.SetBytesProcessed(state.iterations() * nbytes);
    ::close(fd);
    ::unlink(path.c_str());
}
BENCHMARK(BM_Fdatasync_AfterNBytes)
    ->UseRealTime()
    ->Args({static_cast<int64_t>(kBytesPerTxn)})
    ->Args({4096})
    ->Args({65536})
    ->Args({1 << 20})
    ->MinTime(3.0);

// ============================================================
// SECTION 6 — WAL Durability Path
// Measures: commit() and destructor flush including write+fsync.
// ============================================================

// Measures a single append + explicit commit() round-trip.
static void BM_Commit_Single(benchmark::State& state) {
    for (auto _ : state) {
        state.PauseTiming();
        auto path = makeTempPath();
        WriteAheadLog wal{makeConfig(path)};
        wal.append(kPayload1, kEvtLine);
        state.ResumeTiming();

        wal.commit();

        state.PauseTiming();
        ::unlink(path.c_str());
        state.ResumeTiming();
    }
    state.SetItemsProcessed(static_cast<int64_t>(state.iterations()));
}
BENCHMARK(BM_Commit_Single)->UseRealTime()->MinTime(5.0);

// Measures N appends + one commit(); reports fsync amortisation across records.
static void BM_Commit_Many(benchmark::State& state) {
    const auto n = state.range(0);
    for (auto _ : state) {
        state.PauseTiming();
        auto path = makeTempPath();
        auto cfg  = makeConfig(path, static_cast<std::size_t>(n) + 1);
        WriteAheadLog wal{cfg};
        for (int64_t i = 0; i < n; ++i)
            wal.append(kPayload1, kEvtLine);
        state.ResumeTiming();

        wal.commit();

        state.PauseTiming();
        ::unlink(path.c_str());
        state.ResumeTiming();
    }
    state.SetItemsProcessed(state.iterations() * n);
    state.SetBytesProcessed(state.iterations() * n *
        static_cast<int64_t>(sizeof(WalFrameHeader) + 1));
}
BENCHMARK(BM_Commit_Many)
    ->UseRealTime()->RangeMultiplier(4)->Range(1, 1024)->MinTime(3.0);

// Measures destructor flush (drain ring-buffer + write + fsync + close).
// N records buffered before timing starts; destructor is the only timed work.
static void BM_Destruct_Flush(benchmark::State& state) {
    const auto n = state.range(0);
    for (auto _ : state) {
        state.PauseTiming();
        auto  path = makeTempPath();
        auto* wal  = new WriteAheadLog{makeConfig(path, static_cast<std::size_t>(n) + 1)};
        for (int64_t i = 0; i < n; ++i)
            wal->append(kPayload1, kEvtLine);
        state.ResumeTiming();

        delete wal;

        state.PauseTiming();
        ::unlink(path.c_str());
        state.ResumeTiming();
    }
    state.SetItemsProcessed(state.iterations() * n);
    state.SetBytesProcessed(state.iterations() * n *
        static_cast<int64_t>(sizeof(WalFrameHeader) + 1));
}
BENCHMARK(BM_Destruct_Flush)
    ->UseRealTime()->RangeMultiplier(4)->Range(1, 256)->MinTime(3.0);

// ============================================================
// SECTION 7 — Transaction-Shaped Benchmarks
// A fill transaction = TxnHeader + 4×TxnLine + CommitRecord = 6 records.
// ============================================================

// Measures appending N fill transactions to the ring buffer only.
// No commit, no fsync — pure encode + ring-buffer throughput.
static void BM_Txn_AppendOnly(benchmark::State& state) {
    const auto n = state.range(0); // number of transactions
    const auto records = n * kRecordsPerTxn;

    const auto hdr_bytes    = toBytes(kHdrPayload);
    const auto line_bytes   = toBytes(kLinePayload);
    const auto commit_bytes = toBytes(kCommitPayload);

    for (auto _ : state) {
        state.PauseTiming();
        auto  path = makeTempPath();
        auto* wal  = new WriteAheadLog{makeConfig(path, static_cast<std::size_t>(records) + 1)};
        state.ResumeTiming();

        for (int64_t t = 0; t < n; ++t) {
            wal->append(hdr_bytes,    kEvtHeader);
            for (int l = 0; l < kLinesPerTxn; ++l)
                wal->append(line_bytes, kEvtLine);
            wal->append(commit_bytes, kEvtCommit);
        }
        benchmark::ClobberMemory();

        state.PauseTiming();
        delete wal;
        ::unlink(path.c_str());
        state.ResumeTiming();
    }
    state.counters["transactions_per_second"] = benchmark::Counter(
        static_cast<double>(state.iterations() * n), benchmark::Counter::kIsRate);
    state.counters["wal_records_per_second"] = benchmark::Counter(
        static_cast<double>(state.iterations() * records), benchmark::Counter::kIsRate);
    state.SetBytesProcessed(state.iterations() * n *
        static_cast<int64_t>(kBytesPerTxn));
}
BENCHMARK(BM_Txn_AppendOnly)
    ->RangeMultiplier(4)->Range(1, 256)->Iterations(200);

// Measures N fill transactions with fsync after every transaction.
// Worst-case durability: one fsync per transaction = N fsync calls.
static void BM_Txn_FsyncPerTxn(benchmark::State& state) {
    const auto n = state.range(0);
    const auto cap = static_cast<std::size_t>(kRecordsPerTxn) + 1;

    const auto hdr_bytes    = toBytes(kHdrPayload);
    const auto line_bytes   = toBytes(kLinePayload);
    const auto commit_bytes = toBytes(kCommitPayload);

    for (auto _ : state) {
        state.PauseTiming();
        auto  path = makeTempPath();
        auto* wal  = new WriteAheadLog{makeConfig(path, cap)};
        state.ResumeTiming();

        for (int64_t t = 0; t < n; ++t) {
            wal->append(hdr_bytes, kEvtHeader);
            for (int l = 0; l < kLinesPerTxn; ++l)
                wal->append(line_bytes, kEvtLine);
            wal->append(commit_bytes, kEvtCommit);
            wal->commit(); // fsync per transaction
        }

        state.PauseTiming();
        delete wal;
        ::unlink(path.c_str());
        state.ResumeTiming();
    }
    state.counters["transactions_per_second"] = benchmark::Counter(
        static_cast<double>(state.iterations() * n), benchmark::Counter::kIsRate);
    state.counters["wal_records_per_second"] = benchmark::Counter(
        static_cast<double>(state.iterations() * n * kRecordsPerTxn),
        benchmark::Counter::kIsRate);
    state.SetBytesProcessed(state.iterations() * n *
        static_cast<int64_t>(kBytesPerTxn));
}
BENCHMARK(BM_Txn_FsyncPerTxn)
    ->UseRealTime()->RangeMultiplier(4)->Range(1, 64)->MinTime(3.0);

// Measures N fill transactions with one commit() flush per batch.
// Models group commit: N transactions share one fsync.
static void BM_Txn_GroupCommit(benchmark::State& state) {
    const auto n = state.range(0); // transactions per commit group
    const auto records = n * kRecordsPerTxn;

    const auto hdr_bytes    = toBytes(kHdrPayload);
    const auto line_bytes   = toBytes(kLinePayload);
    const auto commit_bytes = toBytes(kCommitPayload);

    for (auto _ : state) {
        state.PauseTiming();
        auto  path = makeTempPath();
        auto* wal  = new WriteAheadLog{makeConfig(path, static_cast<std::size_t>(records) + 1)};
        state.ResumeTiming();

        for (int64_t t = 0; t < n; ++t) {
            wal->append(hdr_bytes, kEvtHeader);
            for (int l = 0; l < kLinesPerTxn; ++l)
                wal->append(line_bytes, kEvtLine);
            wal->append(commit_bytes, kEvtCommit);
        }
        wal->commit(); // single fsync for entire batch

        state.PauseTiming();
        delete wal;
        ::unlink(path.c_str());
        state.ResumeTiming();
    }
    state.counters["transactions_per_second"] = benchmark::Counter(
        static_cast<double>(state.iterations() * n), benchmark::Counter::kIsRate);
    state.counters["wal_records_per_second"] = benchmark::Counter(
        static_cast<double>(state.iterations() * records), benchmark::Counter::kIsRate);
    state.SetBytesProcessed(state.iterations() * n *
        static_cast<int64_t>(kBytesPerTxn));
}
BENCHMARK(BM_Txn_GroupCommit)
    ->UseRealTime()->RangeMultiplier(4)->Range(1, 1024)->MinTime(3.0);

// ============================================================
// SECTION 8 — Group Commit Policy Sweep
// Varies the number of WAL records per commit, not transactions.
// Useful for finding the optimal commit batch size.
// ============================================================

// Measures N raw record appends + one commit per iteration.
// Complements BM_Txn_GroupCommit at the record (not transaction) level.
static void BM_GroupCommit(benchmark::State& state) {
    const auto n = state.range(0); // records per commit group

    for (auto _ : state) {
        state.PauseTiming();
        auto  path = makeTempPath();
        auto* wal  = new WriteAheadLog{makeConfig(path, static_cast<std::size_t>(n) + 1)};
        state.ResumeTiming();

        for (int64_t i = 0; i < n; ++i)
            wal->append(kPayload1, kEvtLine);
        wal->commit();

        state.PauseTiming();
        delete wal;
        ::unlink(path.c_str());
        state.ResumeTiming();
    }
    state.SetItemsProcessed(state.iterations() * n);
    state.SetBytesProcessed(state.iterations() * n *
        static_cast<int64_t>(sizeof(WalFrameHeader) + 1));
}
BENCHMARK(BM_GroupCommit)
    ->UseRealTime()
    ->Args({1})->Args({4})->Args({16})->Args({64})
    ->Args({256})->Args({1024})
    ->MinTime(3.0);

// ============================================================
// SECTION 9 — Recovery
// ============================================================

// Measures recover() on an empty file (just the lseek + EoF detection).
static void BM_Recover_Empty(benchmark::State& state) {
    for (auto _ : state) {
        state.PauseTiming();
        auto  path = makeTempPath();
        auto* wal  = new WriteAheadLog{makeConfig(path)};
        std::vector<WalRecord> out;
        state.ResumeTiming();

        wal->recover(out);
        benchmark::DoNotOptimize(out.data());

        state.PauseTiming();
        delete wal;
        ::unlink(path.c_str());
        state.ResumeTiming();
    }
}
BENCHMARK(BM_Recover_Empty)->UseRealTime()->MinTime(3.0);

// Measures warm-cache recovery of N simple 1-byte records.
// File is pre-written once; each iteration re-opens and recovers.
// NOTE: Bug B (missing break in recover() switch) means only the first
// record is currently recovered regardless of N. Numbers will change
// once Bug B is fixed.
static void BM_Recover_Warm(benchmark::State& state) {
    const auto n = static_cast<int>(state.range(0));
    auto path = makeTempPath();
    prewriteRecords(path, n); // written once, re-read each iteration

    for (auto _ : state) {
        state.PauseTiming();
        auto* wal = new WriteAheadLog{makeConfig(path, static_cast<std::size_t>(n) + 1)};
        std::vector<WalRecord> out;
        out.reserve(static_cast<std::size_t>(n));
        state.ResumeTiming();

        wal->recover(out);
        benchmark::DoNotOptimize(out.data());

        state.PauseTiming();
        delete wal;
        state.ResumeTiming();
    }
    ::unlink(path.c_str());
    state.SetItemsProcessed(state.iterations() * static_cast<int64_t>(n));
    state.SetBytesProcessed(state.iterations() * static_cast<int64_t>(n) *
        static_cast<int64_t>(sizeof(WalFrameHeader) + 1));
}
BENCHMARK(BM_Recover_Warm)
    ->UseRealTime()->RangeMultiplier(4)->Range(1, 1024)->MinTime(3.0);

// Measures cold-ish recovery: advises the kernel to evict file pages before
// each recover() call. Not guaranteed cold — depends on memory pressure.
static void BM_Recover_Coldish(benchmark::State& state) {
    const auto n = static_cast<int>(state.range(0));
    auto path = makeTempPath();
    prewriteRecords(path, n);

    for (auto _ : state) {
        state.PauseTiming();
        {
            // Hint kernel to drop this file's pages from cache.
            const int fd = ::open(path.c_str(), O_RDONLY);
            ::posix_fadvise(fd, 0, 0, POSIX_FADV_DONTNEED);
            ::close(fd);
        }
        auto* wal = new WriteAheadLog{makeConfig(path, static_cast<std::size_t>(n) + 1)};
        std::vector<WalRecord> out;
        out.reserve(static_cast<std::size_t>(n));
        state.ResumeTiming();

        wal->recover(out);
        benchmark::DoNotOptimize(out.data());

        state.PauseTiming();
        delete wal;
        state.ResumeTiming();
    }
    ::unlink(path.c_str());
    state.SetItemsProcessed(state.iterations() * static_cast<int64_t>(n));
    state.SetBytesProcessed(state.iterations() * static_cast<int64_t>(n) *
        static_cast<int64_t>(sizeof(WalFrameHeader) + 1));
}
BENCHMARK(BM_Recover_Coldish)
    ->UseRealTime()->RangeMultiplier(4)->Range(1, 1024)->MinTime(3.0);

// Measures warm recovery of N realistic fill transactions.
// Uses prewriteTransactions (correct format) so recovery succeeds.
// NOTE: Bug B limits actual recovery to 1 record until fixed.
static void BM_Recover_Transactions(benchmark::State& state) {
    const auto n = static_cast<int>(state.range(0)); // number of transactions
    const int  total_records = n * kRecordsPerTxn;
    auto path = makeTempPath();
    prewriteTransactions(path, n);

    for (auto _ : state) {
        state.PauseTiming();
        auto* wal = new WriteAheadLog{makeConfig(path, static_cast<std::size_t>(total_records) + 1)};
        std::vector<WalRecord> out;
        out.reserve(static_cast<std::size_t>(total_records));
        state.ResumeTiming();

        wal->recover(out);
        benchmark::DoNotOptimize(out.data());

        state.PauseTiming();
        delete wal;
        state.ResumeTiming();
    }
    ::unlink(path.c_str());
    state.counters["transactions_per_second"] = benchmark::Counter(
        static_cast<double>(state.iterations() * n), benchmark::Counter::kIsRate);
    state.counters["wal_records_per_second"] = benchmark::Counter(
        static_cast<double>(state.iterations() * total_records), benchmark::Counter::kIsRate);
    state.SetBytesProcessed(state.iterations() * static_cast<int64_t>(n) *
        static_cast<int64_t>(kBytesPerTxn));
}
BENCHMARK(BM_Recover_Transactions)
    ->UseRealTime()
    ->Args({1})->Args({1000})->Args({10000})->Args({100000})
    ->MinTime(3.0);

// ============================================================
// SECTION 10 — Sequential Record Read
// Measures readNextWalRecord() throughput from a pre-written file.
// ============================================================

// Measures warm sequential read of N records.
// Reads all N records in the inner loop; file opened once per outer iteration.
// Fixed vs previous version: SetItemsProcessed uses iterations*N, not inner count.
static void BM_Read_Sequential(benchmark::State& state) {
    const auto n = static_cast<int>(state.range(0));
    auto path = makeTempPath();
    prewriteRecords(path, n);

    for (auto _ : state) {
        state.PauseTiming();
        const int fd = ::open(path.c_str(), O_RDONLY);
        state.ResumeTiming();

        WalRecord rec{};
        while (readNextWalRecord(fd, &rec) == ReadStatus::Ok) {
            benchmark::DoNotOptimize(rec.data.data());
            // Don't reset rec: let data.resize() reuse the buffer
        }

        state.PauseTiming();
        ::close(fd);
        state.ResumeTiming();
    }
    ::unlink(path.c_str());
    state.SetItemsProcessed(state.iterations() * static_cast<int64_t>(n));
    state.SetBytesProcessed(state.iterations() * static_cast<int64_t>(n) *
        static_cast<int64_t>(sizeof(WalFrameHeader) + 1));
}
BENCHMARK(BM_Read_Sequential)
    ->UseRealTime()->RangeMultiplier(4)->Range(1, 4096)->MinTime(3.0);

// Measures cold-ish sequential read with page-cache eviction hint before each iteration.
static void BM_Read_Coldish(benchmark::State& state) {
    const auto n = static_cast<int>(state.range(0));
    auto path = makeTempPath();
    prewriteRecords(path, n);

    for (auto _ : state) {
        state.PauseTiming();
        {
            const int fd = ::open(path.c_str(), O_RDONLY);
            ::posix_fadvise(fd, 0, 0, POSIX_FADV_DONTNEED);
            ::close(fd);
        }
        const int fd = ::open(path.c_str(), O_RDONLY);
        state.ResumeTiming();

        WalRecord rec{};
        while (readNextWalRecord(fd, &rec) == ReadStatus::Ok) {
            benchmark::DoNotOptimize(rec.data.data());
        }

        state.PauseTiming();
        ::close(fd);
        state.ResumeTiming();
    }
    ::unlink(path.c_str());
    state.SetItemsProcessed(state.iterations() * static_cast<int64_t>(n));
    state.SetBytesProcessed(state.iterations() * static_cast<int64_t>(n) *
        static_cast<int64_t>(sizeof(WalFrameHeader) + 1));
}
BENCHMARK(BM_Read_Coldish)
    ->UseRealTime()->RangeMultiplier(4)->Range(1, 4096)->MinTime(3.0);

// ============================================================
// SECTION 11 — CRC Overhead
// Benchmarks use a software CRC32 (table-less) as a proxy for what
// CRC computation would cost once the WAL stubs are replaced.
// These benchmarks are CPU-only; no IO.
// ============================================================

// Measures CRC32 over a WalFrameHeader (24 bytes).
static void BM_CRC_Header(benchmark::State& state) {
    WalFrameHeader hdr{WalMagicBoundaryByte, WalVersion, 1, sizeof(WalFrameHeader), 0, 42};
    for (auto _ : state) {
        auto c = crc32_sw(&hdr, sizeof(hdr));
        benchmark::DoNotOptimize(c);
    }
    state.SetBytesProcessed(state.iterations() *
        static_cast<int64_t>(sizeof(WalFrameHeader)));
}
BENCHMARK(BM_CRC_Header);

// Measures CRC32 over a TxnLineRecord payload (24 bytes).
static void BM_CRC_TxnLine(benchmark::State& state) {
    for (auto _ : state) {
        auto c = crc32_sw(&kLinePayload, sizeof(kLinePayload));
        benchmark::DoNotOptimize(c);
    }
    state.SetBytesProcessed(state.iterations() *
        static_cast<int64_t>(sizeof(TxnLineRecord)));
}
BENCHMARK(BM_CRC_TxnLine);

// Measures CRC32 over one full fill transaction payload (header+4lines+commit = 104 bytes).
static void BM_CRC_FillTransaction(benchmark::State& state) {
    // Build a flat buffer of all payload bytes for one fill transaction.
    std::vector<std::byte> txn_payload;
    txn_payload.resize(sizeof(TxnHeaderRecord) +
                       kLinesPerTxn * sizeof(TxnLineRecord) +
                       sizeof(CommitRecord));
    std::size_t off = 0;
    std::memcpy(txn_payload.data() + off, &kHdrPayload,    sizeof(kHdrPayload));    off += sizeof(kHdrPayload);
    for (int i = 0; i < kLinesPerTxn; ++i) {
        std::memcpy(txn_payload.data() + off, &kLinePayload, sizeof(kLinePayload)); off += sizeof(kLinePayload);
    }
    std::memcpy(txn_payload.data() + off, &kCommitPayload, sizeof(kCommitPayload));

    for (auto _ : state) {
        auto c = crc32_sw(txn_payload.data(), txn_payload.size());
        benchmark::DoNotOptimize(c);
    }
    state.SetBytesProcessed(state.iterations() *
        static_cast<int64_t>(txn_payload.size()));
}
BENCHMARK(BM_CRC_FillTransaction);
