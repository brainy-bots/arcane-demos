# SpacetimeDB benchmark results

Measured ceiling of a single SpacetimeDB instance for a real-time game workload: how many concurrent players can it handle before latency degrades?

---

## 1. Test setup

### Hardware

Single consumer desktop running **both** the SpacetimeDB server and all simulated clients:

| Component | Spec |
|-----------|------|
| CPU | AMD Ryzen 7 (8 cores / 16 threads, ~4.5 GHz boost) |
| RAM | 64 GB DDR4 |
| OS | Windows 10 |
| Network | Loopback (127.0.0.1) — no real network latency |

Running client and server on the same machine means they compete for CPU. A dedicated server machine would perform better. We note this caveat but chose this setup because it's reproducible on any developer workstation.

### SpacetimeDB module

The SpacetimeDB WASM module defines three tables:

| Table | Fields | Purpose |
|-------|--------|---------|
| `Entity` | `entity_id` (UUID, PK), `x`, `y`, `z` (f64) | Player positions — updated every tick |
| `Inventory` | `row_id` (auto-inc PK), `owner_id`, `item_type`, `quantity` | Persistent player inventory |
| `GameEvent` | `event_id` (auto-inc PK), `actor_id`, `target_id`, `event_type`, `timestamp_us` | Interaction log |

The Entity table is intentionally slim (position only, no velocity or metadata) following SpacetimeDB's own performance guidance to keep frequently-updated tables focused. Velocity, stats, and other data that changes less often would go in separate tables in a production game.

Reducers use **update-in-place** (`find` then `update`) so that subscribers see a single `update` event per position change, not a `delete` + `insert` pair.

### Simulated clients (Unreal Stub Swarm)

Each simulated player is a **Rust "Unreal stub"** — from SpacetimeDB's perspective, it is identical to a real Unreal game client:

- **One persistent WebSocket connection** per player (`DbConnection` from `spacetimedb-sdk`)
- **Binary serialization** (BSATN) — same wire protocol as the Unreal SDK
- **Subscription-based reads** — `SELECT * FROM entity` — the server pushes row deltas, no polling
- **Typed reducer calls** for writes — `update_player`, `pickup_item`, `use_item`, `player_interact`

Each stub runs on its own OS thread with a 512 KB stack. The movement model places players on a ring around the world center (spread mode) or packed into a small area (clustered mode), with deterministic walk-and-bounce physics.

**Per player, per tick:**
1. Update local position (movement simulation)
2. Call `update_player` reducer with new position
3. Receive subscription deltas for all entities (pushed by server)

**Per player, at 2 actions/sec (configurable):**
- Randomly call `pickup_item`, `use_item`, or `player_interact`

Source code: `crates/arcane-demo/src/swarm/` (4 modules: `mod.rs`, `stub_client.rs`, `metrics.rs`, `movement.rs`).

### Why this setup is representative

| Concern | How we address it |
|---------|-------------------|
| **Wire protocol fidelity** | Stubs use the official `spacetimedb-sdk` Rust crate — same WebSocket framing, BSATN serialization, and subscription protocol as the Unreal C# SDK. SpacetimeDB cannot distinguish a stub from a real Unreal client. |
| **Subscription model** | Each client subscribes to `SELECT * FROM entity` (all entities). In a real game with these player counts (100–200), every player would see every other player in most scenarios. |
| **Write pattern** | Position updates at 10 Hz per player (standard for MMOs), plus 2 game actions/sec (inventory, interactions). This produces a mixed read-heavy/write-moderate workload. |
| **Update-in-place** | The `update_player` reducer uses `find` + `update` (not `delete` + `insert`), halving subscription events per write. This is the correct pattern per SpacetimeDB docs. |
| **Tick rate** | 10 Hz is standard for MMOs and large-scale multiplayer (Unreal's `NetUpdateFrequency` defaults can be tuned to this). Competitive FPS games use 30–128 Hz but are not the target use case. |
| **Single machine** | A real deployment would separate client and server on different machines. Our single-machine setup means client threads compete with SpacetimeDB for CPU, which is a disadvantage for SpacetimeDB. We accept this as a worst-case baseline — real performance would be somewhat better. |
| **Server-side physics (intended pattern)** | With `--server-physics`, we use the **intended** pattern: clients send input to a **private** PlayerInput table (no subscription fanout); a **scheduled** reducer `physics_tick` runs every 50 ms, reads input, and writes **only** to Entity. So one wave of subscription fanout per tick. Without `--server-physics`, the stub sends position via `update_player` every tick (old mode); use that only for comparison. |

**Intended pattern (now implemented):** Per SpacetimeDB docs, the host runs server-side physics. We use it correctly:

1. **Input in a private table** — Clients call `update_player_input(entity_id, dir_x, dir_z)`. The reducer writes to the **PlayerInput** table, which is **private** (no `public`). So input writes cause **no subscription fanout** (clients don't subscribe to PlayerInput).
2. **Only physics writes to Entity** — A scheduled reducer `physics_tick` runs every 50 ms: it reads PlayerInput + Entity, applies movement, and writes **only to Entity**. So there is **one wave** of writes to the subscribed table per tick → **N²** subscription events per tick, not 2N².
3. **Entity** is position-only (x, y, z). Clients subscribe to Entity and receive position updates from the physics step.

This matches the docs (e.g. move_all_players pattern) and should give better, comparable numbers.

**How to run (intended pattern):** (1) Build and publish the module with the `server_physics` feature: from `spacetimedb_demo/spacetimedb`, run `cargo build --release --features server_physics` then `spacetime publish --server local arcane` (or your server). (2) Run the swarm with `--server-physics`: `arcane-swarm-sdk --players 150 --tick-rate 10 --duration 60 --server-physics`. (3) Re-run your ceiling sweep with this setup and compare to the old numbers (position-only, no server physics). Use the per-second **hint** in the log to see which component fails first.

---

## 2. Results

All tests ran for 30 seconds with a 5-second warmup period. Metrics collected after warmup.

### Variability and run order

Latency can vary a lot run-to-run. Runs later in a session sometimes look worse (or occasionally better) than earlier ones at the same N, likely due to server/OS state (connection cleanup, memory, scheduler). For more stable ceiling estimates:

- Use **-RepeatCount 3** (or more) so each N is run multiple times; the script uses **median** latency for pass/fail.
- Use **-CooldownSeconds 10–30** between runs to give the server time to release connections and reduce cross-run effects.
- For critical numbers, restart `spacetime start` before each batch or between runs.

### Player count sweep (spread mode, 10 Hz, 2 APS)

| Players | Calls/sec (sent) | OK/sec (processed) | Avg latency | p50 | p95 | p99 | Sub events/sec | Status |
|---------|------------------|---------------------|-------------|-----|-----|-----|----------------|--------|
| 100 | 1,200 | 1,000 | 20 ms | 20 ms | 50 ms | 50 ms | 100,000 | **Stable** — flat latency for 30s |
| 150 | 1,800 | 1,500 | 22 ms | 20 ms | 50 ms | 100 ms | 225,000 | **Stable** — flat latency for 30s |
| 160 | 1,900 | ~1,500 | 30–700 ms | 50–500 ms | 100–500 ms | 100–1000 ms | 256,000 | **Borderline** — oscillates, intermittent spikes |
| 175 | 2,050 | ~1,400 | 200 ms → 4 s | 500 ms → 5 s | 1–5 s | 2–5 s | 250,000 | **Overloaded** — latency spirals continuously |
| 200 | 2,350 | ~1,500 | 150 ms → 8 s | 100 ms → 10 s | 1–10 s | 2–10 s | 300,000 | **Overloaded** — severe, unusable |

- Zero reducer errors across all tests
- Zero resubscription churn (ins=0, del=0 after warmup in all tests)
- Spread vs clustered mode produces identical results because all clients subscribe to `SELECT * FROM entity` (no spatial WHERE clause)

### Key observations

**The ceiling is ~150 concurrent players at 10 Hz on this hardware.**

At 150 players, average latency is 22 ms with p99 at 100 ms — excellent for an MMO. At 160 players the system oscillates. At 175+ it degrades continuously.

**The bottleneck is subscription fanout, not reducer throughput.**

SpacetimeDB can execute ~1,500 reducer calls/sec easily. The problem is that each write to the `Entity` table must be evaluated against all active subscriptions and serialized as a delta to each subscriber. With N players:

- Writes/sec = N × 10 (tick rate)
- Subscription events/sec = N × 10 × N = **10 × N²**

The server's subscription processing ceiling is approximately **250,000 events/sec**. Setting 10N² = 250,000 and solving gives N ≈ 158 — matching our measurements precisely.

This is an N-squared scaling relationship. Adding 10% more players increases load by ~21%. This is why there's a sharp cliff between 150 (stable) and 175 (spiraling): the load jumps from 225K to 306K events/sec, exceeding the ceiling.

---

## 3. What these numbers mean

### For a SpacetimeDB-only game

150 concurrent players at 10 Hz with full mutual visibility is a hard ceiling on this hardware. With NPCs, it gets worse: each NPC is another write source that fans out to all player subscribers. Adding M NPCs changes the equation to (P + M) × 10 × P:

| Players | NPCs | Total writes/sec | Subscription events/sec | Within ceiling? |
|---------|------|-------------------|-------------------------|-----------------|
| 150 | 0 | 1,500 | 225,000 | Yes |
| 150 | 100 | 2,500 | 375,000 | No |
| 100 | 200 | 3,000 | 300,000 | No |
| 50 | 200 | 2,500 | 125,000 | Yes |
| 50 | 500 | 5,500 | 275,000 | No |

A game world with 200 NPCs can only support ~50 players on a single SpacetimeDB instance before latency degrades.

### Compared to SpacetimeDB's published benchmarks

SpacetimeDB claims 150,000+ transactions/sec in their benchmarks. This is not contradicted by our results — we measured ~1,500 reducer completions/sec, but the bottleneck isn't the reducers. It's the subscription evaluation and delivery that follows each reducer. Their benchmarks measure raw reducer throughput without subscriptions, which is a valid database benchmark but doesn't represent a game workload where every client subscribes to shared state.

BitCraft (the MMO built on SpacetimeDB) peaked at ~4,400 concurrent Steam players after launch, using multiple SpacetimeDB instances (sharding by region). Our finding of 150 stable players per instance is consistent with that architecture.

### Caveats and what would improve these numbers

| Factor | Impact |
|--------|--------|
| **Separate machines** for clients and server | Eliminates CPU contention. Could improve ceiling by 30–50%. |
| **Higher-clocked CPU** (e.g. Intel i9-14900KS at 6 GHz) | SpacetimeDB reducers run single-threaded in WASM. Higher clock speed directly helps. |
| **Spatial subscriptions** (`WHERE x BETWEEN ... AND z BETWEEN ...`) | Reduces fanout from N to K (visible players). But adds resubscription cost. Net benefit depends on world size and player density. We tested spatial subscriptions and found the resubscription overhead was worse than the fanout savings at these player counts. |
| **Lower tick rate** (5 Hz instead of 10 Hz) | Halves write rate, approximately doubles player ceiling. But increases visual latency. |
| **Fewer game actions** | Reducing from 2 APS to 0 would free ~20% headroom. |

---

## 4. Benchmark version

| | |
|---|---|
| **Date** | March 2026 |
| **SpacetimeDB version** | Latest stable (local `spacetime start`) |
| **Hardware** | AMD Ryzen 7 (8c/16t, ~4.5 GHz), 64 GB DDR4, Windows 10, loopback network |
| **Client stub** | `arcane-swarm-sdk` v0.1.0 using `spacetimedb-sdk` (WebSocket, BSATN, typed reducers) |
| **Source code** | `crates/arcane-demo/src/swarm/` (metrics, movement, stub_client, mod) |
| **Module source** | `spacetimedb_demo/spacetimedb/src/lib.rs` |
