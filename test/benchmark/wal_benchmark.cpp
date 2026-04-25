#include <benchmark/benchmark.h>
#include <unistd.h>

#include "ledgerflow/wal/wal.hpp"

using namespace ledgerflow;
using namespace ledgerflow::wal;

namespace {

std::string makeTempPath() {
    char tmpl[] = "/tmp/wal_bench_XXXXXX";
    int fd = ::mkstemp(tmpl);
    if (fd != -1) ::close(fd);
    return std::string(tmpl);
}

WalConfig makeConfig(const std::string& path, std::size_t capacity = DefaultWalCapacity) {
    WalConfig cfg;
    cfg.wal_file_path = path;
    cfg.capacity = capacity;
    return cfg;
}

} // namespace

// Measures open() + close() + fsync() round-trip with an empty buffer.
static void BM_ConstructDestroy(benchmark::State& state) {
    for (auto _ : state) {
        state.PauseTiming();
        auto path = makeTempPath();
        auto cfg = makeConfig(path);
        state.ResumeTiming();

        { WriteAheadLog wal{cfg}; }

        state.PauseTiming();
        ::unlink(path.c_str());
        state.ResumeTiming();
    }
}
BENCHMARK(BM_ConstructDestroy)->Iterations(500);

// Measures the in-memory ring-buffer push + sequencer increment hot path.
// A single WAL is held for the entire benchmark; destructor flush is not
// part of the measured time.
static void BM_AppendSingle(benchmark::State& state) {
    auto path = makeTempPath();
    auto cfg = makeConfig(path, 1 << 20); // 1M record capacity
    WriteAheadLog wal{cfg};
    core::CommitRecord rec{};
    rec.hdr.seq = 1;

    for (auto _ : state) {
        wal.append(rec);
        benchmark::DoNotOptimize(rec);
    }

    state.SetItemsProcessed(static_cast<int64_t>(state.iterations()));
    ::unlink(path.c_str());
}
BENCHMARK(BM_AppendSingle)->Iterations(1 << 20);

// Measures N appends per iteration; reports throughput in records/sec and
// bytes/sec. Each iteration uses a fresh WAL so the buffer is never full.
static void BM_AppendMany(benchmark::State& state) {
    const auto n = state.range(0);
    for (auto _ : state) {
        state.PauseTiming();
        auto path = makeTempPath();
        auto cfg = makeConfig(path, static_cast<std::size_t>(n));
        auto* wal = new WriteAheadLog{cfg};
        core::CommitRecord rec{};
        state.ResumeTiming();

        for (int64_t i = 0; i < n; ++i) {
            wal->append(rec);
        }
        benchmark::ClobberMemory();

        state.PauseTiming();
        delete wal;
        ::unlink(path.c_str());
        state.ResumeTiming();
    }
    state.SetItemsProcessed(state.iterations() * n);
    state.SetBytesProcessed(state.iterations() * n *
                            static_cast<int64_t>(sizeof(WalRecord)));
}
BENCHMARK(BM_AppendMany)->RangeMultiplier(4)->Range(1, 512)->Iterations(200);

// Measures destructor flush time for N pre-loaded records.
// Captures write() + fsync() cost — the durability path.
static void BM_DestructFlush(benchmark::State& state) {
    const auto n = state.range(0);
    for (auto _ : state) {
        state.PauseTiming();
        auto path = makeTempPath();
        auto cfg = makeConfig(path, static_cast<std::size_t>(n) + 1);
        auto* wal = new WriteAheadLog{cfg};
        core::CommitRecord rec{};
        for (int64_t i = 0; i < n; ++i) {
            wal->append(rec);
        }
        state.ResumeTiming();

        delete wal; // drain + write() + fsync() + close()

        state.PauseTiming();
        ::unlink(path.c_str());
        state.ResumeTiming();
    }
    state.SetItemsProcessed(state.iterations() * n);
    state.SetBytesProcessed(state.iterations() * n *
                            static_cast<int64_t>(sizeof(WalRecord)));
}
BENCHMARK(BM_DestructFlush)->RangeMultiplier(4)->Range(1, 256)->Iterations(50);
