# WAL and PositionEngine Benchmarks

## Purpose

Measure the performance characteristics of `ledgerflow::wal::WriteAheadLog` and
`ledgerflow::BasicPositionEngine`.

WAL benchmarks cover encoding, ring-buffer, write syscall, fsync/fdatasync,
group commit, recovery, sequential read, and CRC. PositionEngine benchmarks
cover in-memory order updates, market-data marks, event dispatch, and linear
position lookup costs.

---

## Build Instructions

```bash
cmake -S . -B build_test \
    -DCMAKE_BUILD_TYPE=Release \
    -DLEDGERFLOW_BUILD_BENCHMARKS=ON

cmake --build build_test --parallel
```

---

## Run Instructions

```bash
# All benchmarks
./build_test/test/benchmark/ledgerflow_benchmarks

# Single category (e.g. fsync)
./build_test/test/benchmark/ledgerflow_benchmarks --benchmark_filter=BM_Fsync

# PositionEngine only
./build_test/test/benchmark/ledgerflow_benchmarks --benchmark_filter=PositionEngine

# JSON output for tooling
./build_test/test/benchmark/ledgerflow_benchmarks --benchmark_format=json
```

---

## Environment Recommendations

For meaningful IO numbers:

```bash
# Set CPU governor to performance
sudo cpupower frequency-set -g performance

# Pin to one core
taskset -c 0 ./build_test/test/benchmark/ledgerflow_benchmarks

# Reduce system load before running
```

Record filesystem type, mount options, and storage device in results.
IO benchmarks use `UseRealTime()`; CPU-only benchmarks (encode, append-to-memory) do not.

---

## Hardware Counter Profiling

```bash
perf stat -e cycles,instructions,cache-references,cache-misses,branches,branch-misses \
    ./build_test/test/benchmark/ledgerflow_benchmarks
```

---

## Transaction Shape

A **fill transaction** used in transaction benchmarks:

| Record | Payload type | Payload bytes | WAL bytes (header+payload) |
|--------|-------------|---------------|---------------------------|
| TxnHeaderRecord | fixed | 24 | 48 |
| TxnLineRecord ×4 | fixed | 24 | 48 each |
| CommitRecord | fixed | 16 | 40 |
| **Total** | | **104** | **280** |

---

## Benchmark List

### Section 1 — Lifecycle
| Benchmark | Measures |
|---|---|
| `BM_Lifecycle_ConstructDestroy` | `open()` + empty destructor flush (fsync + close) |

### Section 2 — Record Encoding (CPU only, no IO)
| Benchmark | Measures |
|---|---|
| `BM_Encode_TxnHeader` | Header population + vector copy for TxnHeaderRecord |
| `BM_Encode_TxnLine` | Same for TxnLineRecord |
| `BM_Encode_CommitRecord` | Same for CommitRecord |
| `BM_Encode_FillTransaction` | All 6 records for one fill transaction |

### Section 3 — Append to Memory Buffer (CPU only)
| Benchmark | Measures |
|---|---|
| `BM_Append_Single` | Ring-buffer push + sequencer increment (hot path) |
| `BM_Append_Many/N` | N appends; WAL destroyed outside timed section |

### Section 4 — Write Without fsync (page-cache write cost)
| Benchmark | Measures |
|---|---|
| `BM_Write_SingleRecord_NoFsync` | One `writeNextWalRecord()` call (2 write syscalls) |
| `BM_Write_BatchRecords_NoFsync/N` | N writes to fresh file, no fsync |
| `BM_Write_Writev_SingleRecord_NoFsync` | One `writev()` (single syscall, header + payload) |

### Section 5 — fsync / fdatasync Isolated Cost
| Benchmark | Measures |
|---|---|
| `BM_Fsync_AfterNBytes/N` | `fsync()` after writing N dirty bytes |
| `BM_Fdatasync_AfterNBytes/N` | `fdatasync()` after writing N dirty bytes |

### Section 6 — WAL Durability Path
| Benchmark | Measures |
|---|---|
| `BM_Commit_Single` | One append + `commit()` (write + fsync) |
| `BM_Commit_Many/N` | N appends + one `commit()` |
| `BM_Destruct_Flush/N` | Destructor flush for N buffered records |

### Section 7 — Transaction-Shaped Benchmarks
| Benchmark | Measures |
|---|---|
| `BM_Txn_AppendOnly/N` | N fill transactions appended to ring buffer only (no fsync) |
| `BM_Txn_FsyncPerTxn/N` | N fill transactions with `commit()` after each |
| `BM_Txn_GroupCommit/N` | N fill transactions with one `commit()` at the end |

### Section 8 — Group Commit Policy
| Benchmark | Measures |
|---|---|
| `BM_GroupCommit/N` | N WAL records + one `commit()`; sweeps 1–1024 |

### Section 9 — Recovery
| Benchmark | Measures |
|---|---|
| `BM_Recover_Empty` | `recover()` on empty file |
| `BM_Recover_Warm/N` | Warm-cache recovery of N records |
| `BM_Recover_Coldish/N` | Cold-ish recovery with `POSIX_FADV_DONTNEED` hint |
| `BM_Recover_Transactions/N` | Recovery of N realistic fill transactions |

### Section 10 — Sequential Record Read
| Benchmark | Measures |
|---|---|
| `BM_Read_Sequential/N` | `readNextWalRecord()` throughput, warm cache |
| `BM_Read_Coldish/N` | Same with `POSIX_FADV_DONTNEED` hint before each iteration |

### Section 11 — CRC Overhead (software CRC32, proxy for production cost)
| Benchmark | Measures |
|---|---|
| `BM_CRC_Header` | CRC32 over 24-byte WalFrameHeader |
| `BM_CRC_TxnLine` | CRC32 over 24-byte TxnLineRecord |
| `BM_CRC_FillTransaction` | CRC32 over full 104-byte transaction payload |

### Section 12 — PositionEngine Hot Path
| Benchmark | Measures |
|---|---|
| `BM_PositionEngine_CreatePositionFromOrder` | Cold creation of a new position from an order event |
| `BM_PositionEngine_UpdateExistingPositionFromOrder` | Steady-state same-symbol order update via direct handler |
| `BM_PositionEngine_ApplyTopOfBookMarketData` | Steady-state top-of-book mark via direct handler |
| `BM_PositionEngine_DispatchOrderEvent` | Same-symbol order update through `onEvent` |
| `BM_PositionEngine_DispatchMarketDataEvent` | Top-of-book mark through `onEvent` |
| `BM_PositionEngine_GetPositionHit/N` | Lookup hit with N positions; symbol at end of vector |
| `BM_PositionEngine_GetPositionMiss/N` | Lookup miss with N positions |
| `BM_PositionEngine_MixedDispatch90MarketData10Order` | Deterministic 90% market data / 10% order dispatch mix |

---

## Caveats

- **Bug A** (`commitRecordToFile` writes `sizeof(WalRecord)` bytes including vector internals)
  is still present. `BM_Destruct_Flush` and `BM_Commit_*` are measuring the buggy write path.
  After Bug A is fixed, these numbers will change.
- **Bug B** (missing `break` in `recover()` switch) limits actual recovery to 1 record
  regardless of file size. `BM_Recover_*` numbers reflect recovering exactly 1 record.
  After Bug B is fixed, recover throughput numbers will change significantly.
- **CRC is stubbed** (always 0 in production). `BM_CRC_*` benchmarks use a software
  CRC32 to show what the cost *would be* once enabled.
- **CPU frequency scaling** was enabled during the run below; real-time IO numbers are
  more reliable than CPU-time numbers.
- **`fdatasync` is ~2× faster than `fsync`** on this hardware for small writes. Consider
  using `fdatasync` in production once the implementation is stable.
- **`writev` is ~44% faster** than two separate `write()` calls for single-record writes.
  This is the preferred write path for the final implementation.
- **PositionEngine benchmarks include the current implementation behavior.** The current
  implementation accumulates `total_unrealized_pnl` on repeated marks, so market-data
  update benchmarks measure that extra store/add work as implemented today.
- **PositionEngine lookup is linear over `std::vector<Position>`.** Hit benchmarks look up
  the last symbol, so they intentionally represent worst-case successful lookup.

---

## Latest Results

**Timestamp:** 2026-04-29T21:58:53+01:00  
**OS:** Linux 6.17.0-22-generic, 16 × 5135 MHz  
**CPU caches:** L1 32 KiB × 8, L2 1 MiB × 8, L3 16 MiB  
**Build type:** Release (GCC)  
**Filesystem:** ext4 (tmpfs for temp WAL files under /tmp)

### Section 1 — Lifecycle
```
BM_Lifecycle_ConstructDestroy     5851 ns real   5759 ns CPU
```

### Section 2 — Record Encoding (CPU time; no IO)
```
BM_Encode_TxnHeader               8.84 ns    113M items/s
BM_Encode_TxnLine                 9.05 ns    110M items/s
BM_Encode_CommitRecord            8.92 ns    112M items/s
BM_Encode_FillTransaction        53.3  ns    18.8M txns/s   (6 records/txn)
```

### Section 3 — Append to Memory (CPU time)
```
BM_Append_Single                 34.6  ns    28.9M items/s
BM_Append_Many/1                 3333  ns     345k items/s
BM_Append_Many/64                5480  ns    13.5M items/s
BM_Append_Many/512              15158  ns    34.8M items/s
```

### Section 4 — Write Without fsync (real time)
```
BM_Write_SingleRecord_NoFsync    2316  ns    432k items/s    19.8 MiB/s
BM_Write_Writev_SingleRecord     1293  ns    774k items/s    35.4 MiB/s   ← 44% faster via single syscall
BM_Write_BatchRecords/1          7893  ns    127k items/s
BM_Write_BatchRecords/256      637633  ns    401k items/s    18.4 MiB/s
```

### Section 5 — fsync / fdatasync (real time)
```
BM_Fsync_AfterNBytes/280      1317055 ns  (1.3ms — fsync on one transaction)
BM_Fsync_AfterNBytes/4096     1093214 ns  (1.1ms)
BM_Fsync_AfterNBytes/65536    1123572 ns  (1.1ms)
BM_Fsync_AfterNBytes/1048576  1397371 ns  (1.4ms)

BM_Fdatasync_AfterNBytes/280   606339 ns  (0.6ms — ~2× faster than fsync)
BM_Fdatasync_AfterNBytes/4096  501577 ns  (0.5ms)
BM_Fdatasync_AfterNBytes/65536 524004 ns  (0.5ms)
BM_Fdatasync_AfterNBytes/1M    857575 ns  (0.9ms)
```
Key insight: `fdatasync` is consistently ~2× faster than `fsync` for small dirty sizes.

### Section 6 — WAL Durability Path (real time)
```
BM_Commit_Single              1231299 ns    812 items/s
BM_Commit_Many/1              1185975 ns    843 items/s
BM_Commit_Many/64             1576992 ns     40k items/s   ← 50× more records, same fsync
BM_Commit_Many/1024           4568556 ns    224k items/s
BM_Destruct_Flush/256         2345460 ns    109k items/s
```

### Section 7 — Transaction-Shaped Benchmarks (real time)
```
BM_Txn_AppendOnly/1              3336 ns    357k txns/s   (no fsync, memory only)
BM_Txn_AppendOnly/256           48574 ns   5.41M txns/s   (ring-buffer scales well)
BM_Txn_FsyncPerTxn/1          1310183 ns    763 txns/s    (1 fsync per txn)
BM_Txn_FsyncPerTxn/4          4616753 ns    866 txns/s
BM_Txn_GroupCommit/1          1164684 ns    859 txns/s
BM_Txn_GroupCommit/4          1284684 ns   3.1k txns/s    ← 3.6× vs per-txn fsync
BM_Txn_GroupCommit/64         2545151 ns  25.1k txns/s    ← 33× vs per-txn fsync
BM_Txn_GroupCommit/256        5362602 ns  47.7k txns/s
BM_Txn_GroupCommit/1024      17445526 ns  58.7k txns/s
```
Key insight: group commit at 64 transactions gives **33× better throughput** than per-transaction fsync.

### Section 8 — Group Commit Policy (real time)
```
BM_GroupCommit/1              1160925 ns    861 items/s
BM_GroupCommit/64             1554448 ns   41.2k items/s
BM_GroupCommit/1024           4011301 ns   255k items/s
```

### Section 9 — Recovery (real time)
```
BM_Recover_Empty                 2416 ns
BM_Recover_Warm/1                3496 ns    286k items/s
BM_Recover_Warm/64              78232 ns    818k items/s
BM_Recover_Warm/1024          1215426 ns    843k items/s     20 MiB/s
BM_Recover_Coldish/1            18706 ns     53k items/s   (posix_fadvise overhead ~15µs)
BM_Recover_Coldish/64          102755 ns    623k items/s
BM_Recover_Coldish/1024       1315316 ns    779k items/s
BM_Recover_Transactions/1        9431 ns    106k txns/s
BM_Recover_Transactions/1000  7276242 ns    137k txns/s    37 MiB/s
BM_Recover_Transactions/100000 724519222 ns  138k txns/s   37 MiB/s  (Bug B: only 1 record recovered)
```
Note: `BM_Recover_Transactions` throughput is dominated by `prewriteRecords` cost and WAL
open/lseek overhead. Bug B means `recover()` returns after reading 1 record; numbers will
change significantly once Bug B is fixed.

### Section 10 — Sequential Read (real time)
```
BM_Read_Sequential/1            2787 ns    359k items/s
BM_Read_Sequential/64          73389 ns    872k items/s    20.8 MiB/s
BM_Read_Sequential/4096      4694116 ns    873k items/s    20.8 MiB/s   ← bandwidth-limited
BM_Read_Coldish/1              18499 ns     54k items/s   (cache-miss penalty ~16µs)
BM_Read_Coldish/4096         4772230 ns    858k items/s    20.5 MiB/s   ← converges with warm at scale
```
Key insight: sequential read plateaus at ~21 MiB/s. Each `readNextWalRecord` call makes
2 `read()` syscalls; moving to `read()` with a kernel buffer would raise this ceiling.
Old benchmark showed ~15µs for N=1 due to `SetItemsProcessed` called inside the loop;
that was a counter bug, not a performance problem.

### Section 11 — CRC Overhead (software CRC32, CPU time)
```
BM_CRC_Header            154 ns   149 MiB/s
BM_CRC_TxnLine           158 ns   145 MiB/s
BM_CRC_FillTransaction   926 ns   140 MiB/s
```
CRC per transaction (926 ns) is **17× the encoding cost** (53 ns) and will dominate the
CPU hot path once enabled. A table-based or hardware CRC32 (`_mm_crc32_u64`) would reduce
this significantly (expected 5–20× improvement).

### Section 12 — PositionEngine Hot Path (CPU time)

**Timestamp:** 2026-05-02T08:53:50+01:00  
**Build type:** Release (GCC)  
**Run command:** `./build_test/test/benchmark/ledgerflow_benchmarks --benchmark_filter=PositionEngine --benchmark_min_time=0.01s`

```
BM_PositionEngine_CreatePositionFromOrder              1320 ns      757k items/s
BM_PositionEngine_UpdateExistingPositionFromOrder      44.5 ns     22.5M items/s
BM_PositionEngine_ApplyTopOfBookMarketData             18.6 ns     53.7M items/s
BM_PositionEngine_DispatchOrderEvent                   44.5 ns     22.5M items/s
BM_PositionEngine_DispatchMarketDataEvent              18.2 ns     55.0M items/s

BM_PositionEngine_GetPositionHit/1                     5.26 ns      190M items/s
BM_PositionEngine_GetPositionHit/10                    56.8 ns     17.6M items/s
BM_PositionEngine_GetPositionHit/100                    545 ns     1.84M items/s
BM_PositionEngine_GetPositionHit/1000                  5025 ns      199k items/s
BM_PositionEngine_GetPositionHit/10000                21743 ns     46.0k items/s

BM_PositionEngine_GetPositionMiss/1                    1.03 ns      971M items/s
BM_PositionEngine_GetPositionMiss/10                   2.95 ns      339M items/s
BM_PositionEngine_GetPositionMiss/100                  21.7 ns     46.2M items/s
BM_PositionEngine_GetPositionMiss/1000                  416 ns     2.41M items/s
BM_PositionEngine_GetPositionMiss/10000               19796 ns     50.5k items/s

BM_PositionEngine_MixedDispatch90MarketData10Order     10.8 ns     92.3M items/s
```

Interpretation:

- New-position creation is much slower than steady-state updates because it constructs and
  pushes a `Position` into the portfolio vector. This is the cold path and should be kept
  separate from hot update latency.
- Steady-state same-symbol order updates are ~45 ns both direct and through `onEvent`, so
  dispatch overhead is lost in the current update cost for this path.
- Top-of-book marking is ~18 ns both direct and through `onEvent`, indicating variant
  dispatch is not the dominant cost for market-data updates in this benchmark shape.
- Lookup cost scales linearly with portfolio size. Worst-case hit rises from ~5 ns at one
  symbol to ~21.7 us at 10,000 symbols; misses show the same O(N) behavior.
- The 90/10 mixed dispatch benchmark is faster than the simple weighted average because
  it repeatedly touches one symbol and is dominated by the very short market-data route.
  Use it as a dispatch sanity check, not as a substitute for portfolio-size sweeps.

---

## Reading the Numbers

| Question | Answer from benchmarks |
|---|---|
| How fast is record encoding? | ~9 ns/record, ~18M fill txns/sec (CPU only) |
| How fast is ring-buffer append? | ~35 ns/record after paylaod alloc |
| How fast is write without fsync? | ~2.3µs/record (2 syscalls); ~1.3µs with writev |
| What does fsync cost? | ~1.1–1.4ms regardless of dirty data size |
| What does fdatasync cost? | ~0.5–0.9ms — ~2× faster than fsync |
| How much does batching help? | Group-commit 64 txns → 33× better than per-txn fsync |
| How fast is warm recovery? | ~843k records/sec, ~20 MiB/s |
| Is sequential read actually slow? | No — old benchmark had a counter bug. Real: ~873k records/sec |
| What does CRC cost? | 926 ns/txn (software) — 17× encoding; use hardware CRC in production |
| How fast is steady-state position update? | ~45 ns for same-symbol order updates; ~18 ns for top-of-book marks |
| What is `onEvent` dispatch overhead? | Not material in the current order/market-data microbenchmarks |
| What is the PositionEngine lookup shape? | Linear scan; worst-case hit at 10k symbols is ~21.7µs |
