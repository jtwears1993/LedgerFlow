# WAL Benchmarks

## Purpose

Measure the performance characteristics of `ledgerflow::wal::WriteAheadLog` across
four dimensions: lifecycle cost, in-memory append throughput, batch append throughput,
and flush/fsync cost on destruction.

## Build Instructions

```bash
cmake -S . -B build_test \
    -DCMAKE_BUILD_TYPE=Release \
    -DLEDGERFLOW_BUILD_BENCHMARKS=ON

cmake --build build_test --parallel
```

## Run Instructions

```bash
# All benchmarks
./build_test/test/benchmark/ledgerflow_benchmarks

# Specific benchmark
./build_test/test/benchmark/ledgerflow_benchmarks --benchmark_filter=BM_DestructFlush

# JSON output
./build_test/test/benchmark/ledgerflow_benchmarks --benchmark_format=json
```

## Benchmark List

| Benchmark | What it measures |
|---|---|
| `BM_ConstructDestroy` | `open()` + `close()` + `fsync()` with empty buffer |
| `BM_AppendSingle` | Ring-buffer push + sequencer increment (pure memory, no I/O) |
| `BM_AppendMany/N` | Throughput for N appends per work unit |
| `BM_DestructFlush/N` | `write()` + `fsync()` + `close()` for N buffered records |

## Metrics Tracked

- **Time/iteration** (ns or µs): end-to-end latency for the measured operation
- **CPU time**: CPU-only time, excluding OS scheduling and I/O wait
- **items/second**: record throughput
- **bytes/second**: raw byte throughput (where applicable)

## Caveats

- **fsync cost dominates flush benchmarks.** `BM_ConstructDestroy` and `BM_DestructFlush`
  show ~1ms real time because `fsync()` blocks until the storage device confirms durability.
  CPU time is much lower (~22µs–425µs). On NVMe the gap will be smaller; on HDD much larger.
- **Filesystem matters.** Results on tmpfs or a RAM disk will show near-zero flush times.
  These results were taken on a standard ext4 filesystem.
- **CPU frequency scaling.** The benchmark runner emitted a warning that CPU scaling was
  enabled, which adds noise to real-time measurements. CPU time figures are more reliable.
- **v0 limits.** Only `FsyncMode::Always` is supported. `commit()` is a no-op; all flushing
  happens in the destructor. Benchmarks reflect this single-flush-on-close model.
- **Buffer overflow.** `BM_AppendSingle` uses a 1M-record capacity ring buffer. Records
  appended beyond that capacity are silently dropped (current v0 behaviour).

## Latest Results

**Timestamp:** 2026-04-25T10:17:29+01:00  
**OS:** Linux 6.17.0-22-generic, 16 × 5134 MHz  
**CPU caches:** L1 32 KiB × 8, L2 1 MiB × 8, L3 16 MiB  
**Compiler:** GCC 13.3.0  
**Build type:** Release  
**Command:**
```bash
./build_test/test/benchmark/ledgerflow_benchmarks
```

```
-----------------------------------------------------------------------------
Benchmark                                   Time             CPU   Iterations
-----------------------------------------------------------------------------
BM_ConstructDestroy/iterations:500    1007338 ns        22277 ns          500
BM_AppendSingle/iterations:1048576       2.91 ns         2.91 ns  1048576  items_per_second=343.55M/s
BM_AppendMany/1/iterations:200           2852 ns         2634 ns      200  bytes_per_second=17.4MiB/s   items_per_second=379.6k/s
BM_AppendMany/4/iterations:200           2906 ns         2603 ns      200  bytes_per_second=70.4MiB/s   items_per_second=1.54M/s
BM_AppendMany/16/iterations:200          2913 ns         2657 ns      200  bytes_per_second=275.7MiB/s  items_per_second=6.02M/s
BM_AppendMany/64/iterations:200          2810 ns         2505 ns      200  bytes_per_second=1.14GiB/s   items_per_second=25.6M/s
BM_AppendMany/256/iterations:200         2964 ns         2667 ns      200  bytes_per_second=4.29GiB/s   items_per_second=96.0M/s
BM_AppendMany/512/iterations:200         3671 ns         3396 ns      200  bytes_per_second=6.74GiB/s   items_per_second=150.8M/s
BM_DestructFlush/1/iterations:50     1044013 ns        58978 ns       50  bytes_per_second=795KiB/s    items_per_second=16.9k/s
BM_DestructFlush/4/iterations:50     1039254 ns        60844 ns       50  bytes_per_second=3.01MiB/s   items_per_second=65.7k/s
BM_DestructFlush/16/iterations:50    1080951 ns        87255 ns       50  bytes_per_second=8.39MiB/s   items_per_second=183.4k/s
BM_DestructFlush/64/iterations:50    1161927 ns       167140 ns       50  bytes_per_second=17.5MiB/s   items_per_second=382.9k/s
BM_DestructFlush/256/iterations:50   1426720 ns       424588 ns       50  bytes_per_second=27.6MiB/s   items_per_second=602.9k/s
```

### Reading the numbers

- **`BM_AppendSingle` at 2.91 ns** is the pure in-memory cost. No I/O is measured here.
- **`BM_DestructFlush` real time ~1ms** regardless of N reflects the fixed cost of a single
  `fsync()` call. CPU time scales with N (more `write()` calls), but wall time is fsync-bound.
- **`BM_AppendMany` setup overhead** (~2.5–3.4µs) includes WAL construction and file open,
  which explains why small N looks relatively expensive per-record.
