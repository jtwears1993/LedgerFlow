# Ledger Flow MVP — POC (First Draft)

## 🎯 Objective

Build a **minimal, high-performance, append-only ledger system** that proves:

* Deterministic replay from a journal
* Double-entry correctness (ledger always balances)
* Idempotent command handling
* Low-latency durable commits
* Recoverability after crash
* Auditability via Postgres projection

This is **not** a full trading system. It is a **core ledger engine POC**.

---

## ✅ In Scope

### Core Functionality

* **Append-only journal (source of truth)**

    * Monotonic `sequence_id`
    * Binary or compact event format
    * Group `fsync` durability

* **Single-writer sequencer**

    * Owns ordering, state mutation, and validation
    * Eliminates locks/coordination complexity

* **In-memory account balances**

    * Per trader:

        * `TradableBalance`
        * `ReservedBalance`
    * Used for all validation (no DB reads)

* **Trading Events (minimal set)**

    1. `DepositCollateral`
    2. `NewOrderAccepted` (reserve collateral)
    3. `NewOrderRejected`
    4. `OrderCancelled` (release collateral)

* **Double-entry posting rules**

    * Every state change results in balanced debit/credit entries
    * Enforced at event creation time

* **Idempotency**

    * All commands include `idempotency_key`
    * Duplicate requests return original result

* **Crash recovery**

    * Replay journal to rebuild state
    * Truncate partial/corrupt tail
    * Resume from last valid sequence

* **Postgres (audit only)**

    * Async projection from journal
    * Stores immutable event history
    * No role in hot path

---

## ❌ Out of Scope

* Positions (`InPosition`)
* Fills / execution lifecycle
* PnL (realised or unrealised)
* Margin engines / risk models
* Multi-product support (futures/options)
* Exchange connectivity
* Settlement / payments
* Multi-node / clustering / replication
* Advanced permissions / RBAC
* Complex posting rule engine
* UI / dashboards / reporting

---

## 🧱 High-Level Design

```text
Client Request
    ↓
API Layer (thin)
    ↓
Single Writer (Sequencer + Validation + Posting Rules)
    ↓
Append-only Journal (group fsync commit)
    ↓
In-memory State Update
    ↓
Response (Accepted / Rejected)
    ↓
Async Postgres Projection (audit only)
```

---

## 🔁 Core Flow — NewOrder

```text
1. Receive NewOrder (with idempotency_key)
2. Lookup trader balance (in memory)
3. Compute required collateral
4. If insufficient:
      → create OrderRejected event
   Else:
      → create OrderAccepted event
      → reserve collateral
5. Append event to journal buffer
6. Group fsync (durability boundary)
7. Apply state change in memory
8. Respond to client
9. Project event to Postgres (async)
```

---

## 💰 Posting Rules

**DepositCollateral**

```
Debit:  TradableBalance
Credit: SystemControl
```

**NewOrderAccepted (reserve)**

```
Debit:  ReservedBalance
Credit: TradableBalance
```

**OrderCancelled (release)**

```
Debit:  TradableBalance
Credit: ReservedBalance
```

**Invariant**

```
Sum(debits) = Sum(credits) for every event
```

---

## ⚡ Performance Target

| Stage               |                 Latency |
| ------------------- | ----------------------: |
| Validation + lookup |                 ~1–5 µs |
| Append to buffer    |                 ~1–5 µs |
| Group wait          |                0–100 µs |
| fsync               |               50–500 µs |
| **Total**           | **~100–600 µs typical** |

---

## 🔒 Guarantees

* **Atomicity**: Event is either fully committed to journal or not at all
* **Durability**: Acknowledged events survive crash (post-fsync)
* **Consistency**: State derived solely from committed events
* **Replayability**: Full system state rebuild from journal
* **Idempotency**: Safe retries with no duplication
* **Auditability**: All actions traceable via event log

---

## 🧪 Success Criteria

* Can process and persist events under load with stable latency
* Journal replay produces identical state deterministically
* Double-entry invariant never violated
* System recovers correctly from simulated crash
* Postgres projection can rebuild from journal without divergence

---

## 🧭 Next Steps (Post-MVP)

* Introduce `OrderConfirmed`, `OrderFilled`
* Add `InPosition` accounts
* Expand projection models (balances, orders)
* Add snapshotting for faster recovery
* Explore replication / HA strategies

---

## 🧠 Summary

This MVP proves the **core architectural bet**:

> A **single-writer, append-only, double-entry journal** can deliver
> **low-latency, durable, auditable, and replayable state management**.

Everything else builds on top of this.
