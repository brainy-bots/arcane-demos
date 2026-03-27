# Why Arcane solves SpacetimeDB's scaling limit

Analysis based on benchmarking a single SpacetimeDB instance for real-time game workloads. See `SPACETIMEDB_BENCHMARK_RESULTS.md` for the raw numbers and test methodology.

---

## 1. SpacetimeDB's limit and why it exists

### The N-squared subscription problem

SpacetimeDB's architecture processes each database write through a sequential pipeline:

1. **Receive** WebSocket message from client
2. **Deserialize** BSATN payload
3. **Execute** reducer in WASM (single-threaded — transactional consistency guarantee)
4. **For each active subscription**: evaluate whether the changed row matches the subscription's SQL query
5. **For each match**: serialize the row delta (BSATN) and queue a WebSocket send to that subscriber

Steps 4–5 are the bottleneck. With N players, each subscribing to the Entity table:

- Each position update triggers N subscription evaluations
- N players send 10 updates/sec each (at 10 Hz)
- Total subscription events/sec = **N × 10 × N = 10N²**

We measured the practical ceiling at approximately **250,000 subscription events/sec** on our test hardware (Ryzen 7, client and server co-located). This gives a stable player ceiling of about **150 concurrent players** at 10 Hz.

### Why this is fundamental, not incidental

This isn't a bug or an unoptimized code path. It's inherent to SpacetimeDB's design as a **database with push subscriptions**: every write must be evaluated against every active subscription. That's correct behavior for a database. It means SpacetimeDB excels at workloads with many reads and few writes, or with highly selective subscriptions. But a real-time game where every player writes their position every tick and every other player subscribes is the worst case: high write rate, low selectivity, maximum fanout.

### NPCs make it worse

In SpacetimeDB-only, NPCs are rows in the same Entity table, updated by reducer calls from a simulation server. Each NPC position update fans out to all player subscribers, just like a player update. The load equation becomes **(P + M) × 10 × P**, where M is the number of NPCs.

200 NPCs reduce the player ceiling from 150 to approximately **50 players**. A game world with 500 NPCs + players would struggle to support more than 30–40 simultaneous players on a single SpacetimeDB instance.

---

## 2. How Arcane solves this

### Architecture difference

In SpacetimeDB-only, every client has a direct WebSocket connection to SpacetimeDB and subscribes to shared tables. The database is responsible for both storage and real-time distribution.

Arcane separates these responsibilities:

| Responsibility | SpacetimeDB-only | With Arcane |
|----------------|-----------------|-------------|
| Real-time position distribution | SpacetimeDB subscriptions (N² fanout) | Arcane ClusterServers → direct WebSocket to clients |
| Cross-server replication | N/A (single database) | Redis pub/sub between neighbor clusters |
| Persistent state (inventory, events) | SpacetimeDB reducers + subscriptions | SpacetimeDB reducers (unchanged) |
| Position persistence | Every tick, N individual reducer calls | Throttled batch writes (every 1–2 seconds) |

The key change: **real-time position data no longer flows through SpacetimeDB subscriptions.** Players connect to an Arcane ClusterServer, which pushes state updates directly. SpacetimeDB is used only for durable persistence and discrete game events.

### Why this eliminates the N² problem

An Arcane ClusterServer distributes state to its clients by building one batched delta per tick and sending it to all connected players. The cost:

1. **Collect** all entity position changes this tick — O(entities) in-memory operations
2. **Serialize** one delta — done once, regardless of how many clients
3. **Send** the serialized bytes to N clients — parallel async I/O (tokio), not sequential

Compare this to SpacetimeDB, where each of N individual writes triggers N subscription evaluations and N individual serializations (N² total per tick):

| Operation | SpacetimeDB (per tick) | Arcane ClusterServer (per tick) |
|-----------|----------------------|-------------------------------|
| Write processing | N reducer calls (sequential WASM) | N in-memory updates (native Rust) |
| Subscription evaluation | N writes × N subscribers = **N² evaluations** | None — just send to connected clients |
| Serialization | N writes × N subscribers = **N² serializations** | **1 serialization** (reused for all clients) |
| Network sends | N² individual WebSocket writes (sequential pipeline) | N parallel WebSocket writes (async I/O) |
| **Total CPU cost** | **O(N²)** | **O(N)** |

Even on a single machine, this changes the scaling curve from quadratic to linear. The bottleneck shifts from "subscription evaluation budget" to "network bandwidth and per-client send throughput," which are much higher limits.

### SpacetimeDB load with Arcane

With C Arcane ClusterServers and N total players:

| | SpacetimeDB-only | Arcane + SpacetimeDB |
|---|---|---|
| SpacetimeDB connections | N player connections | C cluster server connections |
| Position writes/sec | N × 10 | C batch persists every 1–2 sec |
| Subscription events/sec | N² × 10 | ~0 for real-time positions |
| Discrete event writes/sec | N × APS | Same (but fans out to C, not N) |

Example: 600 players across 4 cluster servers:

- **SpacetimeDB-only**: 600 × 10 × 600 = **3,600,000 events/sec** — 14× over the ceiling
- **Arcane**: ~4 batch persist writes/sec, ~0 subscription fanout for positions — **SpacetimeDB is barely loaded**

---

## 3. When is Arcane better?

### Arcane is better when

- **Many players share the same world state** (mutual visibility). This is the exact scenario where SpacetimeDB's N² fanout is worst and Arcane's O(N) distribution shines.
- **NPCs or server-simulated entities exist.** In SpacetimeDB-only, each NPC write fans out to all subscribers. In Arcane, NPCs are just more entities in the batched delta — linear cost. Additionally, Arcane allows game developers to freely allocate compute budget to NPC intelligence and behaviors without any impact on the state distribution pipeline.
- **Player counts exceed ~100–150 per region.** Below ~100, SpacetimeDB-only handles the load fine (20 ms latency, stable). The benefit of Arcane emerges as player count grows into the hundreds.
- **The game requires horizontal scaling.** Arcane's dynamic clustering (merge/split) distributes players across multiple ClusterServers. SpacetimeDB is a single-node database — it cannot shard game state across instances without application-level partitioning.

### Arcane is NOT better when

- **Player count is small** (< 50–100) and there are few NPCs. SpacetimeDB-only is simpler to deploy and performs well at this scale.
- **The workload is read-heavy with few writes** (e.g., a leaderboard, a lobby). SpacetimeDB's subscription model is efficient for low-write scenarios.
- **You need only persistence, not real-time distribution.** If game clients don't need every-tick updates from the server (e.g., turn-based games), SpacetimeDB's subscription overhead is low.

### The crossover point

Based on our benchmarks, the approximate crossover where Arcane begins to matter:

| World composition | SpacetimeDB-only ceiling | With Arcane |
|-------------------|--------------------------|-------------|
| Players only, no NPCs | ~150 players | 300–600+ (single machine), thousands (distributed) |
| 100 NPCs + players | ~80 players | Same — NPCs have linear cost in Arcane |
| 500 NPCs + players | ~30 players | Same — NPC simulation compute is separate from distribution |

---

## 4. Why the N² problem doesn't just move to Arcane

A natural objection: if 500 players are in the same area and all need to see each other, Arcane still has to send 500 position updates to 500 clients per tick. Isn't that the same N² problem?

The **bandwidth** cost is indeed the same — someone has to transmit N² worth of data. But the **CPU** cost is radically different:

| Factor | SpacetimeDB | Arcane |
|--------|-------------|--------|
| **Per-write overhead** | WASM reducer execution + SQL subscription evaluation per subscriber — happens for every individual write | None — all positions updated in-memory, no per-write pipeline |
| **Serialization** | Each write serialized independently for each subscriber (N² serializations/tick) | One batch delta serialized once per tick, same bytes sent to all clients |
| **Parallelism** | Sequential single-threaded WASM reducer pipeline | Parallel async sends via tokio |
| **Filtering** | SQL WHERE clause evaluated against every subscription on every write | Application-level spatial data structures (spatial hash, k-d tree) — filter before serialization |
| **Priority** | Not supported — subscriptions are all-or-nothing | Nearby entities at full tick rate, distant entities at reduced rate (like Unreal's NetPriority) |

Beyond CPU efficiency, Arcane has structural tools to reduce the effective N:

1. **Dynamic clustering** — when an area gets too crowded, the ClusterManager splits it into sub-clusters. Each handles a subset of players with boundary replication via Redis.
2. **Interest management** — a ClusterServer can apply visibility radius, only including nearby entities in each player's STATE_UPDATE. This turns N² bandwidth into N×K, where K is the visibility cap.
3. **Priority-based update rates** — distant entities sent at 3 Hz, nearby at 10 Hz. Reduces average bandwidth without affecting perceived quality for nearby interactions.

These are standard techniques used by every large-scale multiplayer game. Arcane's architecture supports them natively. SpacetimeDB's subscription model does not — you can add WHERE clauses, but each clause must be evaluated per-write, and changing the clause requires an expensive resubscription.

---

## 5. Summary

| | SpacetimeDB-only | Arcane + SpacetimeDB |
|---|---|---|
| **Player ceiling (single machine)** | ~150 at 10 Hz | Estimated 300–600+ |
| **Player ceiling (distributed)** | ~150 (single-node DB) | Scales linearly with cluster servers |
| **With 200 NPCs** | ~50 players | ~300–600+ (NPCs are linear cost) |
| **Subscription cost per tick** | O(N²) — evaluated per write, per subscriber | O(N) — one batch, parallel sends |
| **Horizontal scaling** | Not supported without app-level sharding | Built-in: dynamic merge/split clustering |
| **NPC compute budget** | Competes with subscription pipeline (same WASM thread) | Independent — cluster servers allocate freely |
| **SpacetimeDB role** | Everything (storage + real-time distribution + game logic) | Persistence + discrete events only |

SpacetimeDB is an excellent database. Its limit for real-time games is not a flaw — it's an inherent consequence of the subscription model (every write evaluated against every subscription). Arcane complements it by handling the part that databases aren't designed for (high-frequency, low-latency state distribution to many clients) while letting SpacetimeDB do what it's good at (durable persistence, transactional game events, and authoritative state).

---

## 6. Known limitations and next steps

### What this analysis does NOT prove yet

- **Arcane's measured performance.** All Arcane numbers in this document are theoretical projections based on architecture analysis. The next step is to run the same player count sweep with Arcane + SpacetimeDB using the stub swarm, measuring actual latency and throughput. Even on the same single machine (where Arcane competes with SpacetimeDB for CPU), if Arcane handles more players, that strengthens the argument.

- **Dedicated hardware numbers.** All benchmarks ran on a single consumer desktop with client stubs and SpacetimeDB sharing CPU. A proper deployment with separate client and server machines would give SpacetimeDB more headroom — the 150-player ceiling is a conservative baseline, not an absolute limit.

### Clarifications

- **Arcane adds infrastructure.** Compared to SpacetimeDB-only (one process), Arcane requires a ClusterManager, one or more ClusterServers, and Redis. This is a standard trade-off for horizontal scaling and can be automated with standard deployment tooling (Docker, Kubernetes, cloud auto-scaling). For the class of game that needs Arcane (hundreds+ players, MMO-scale), this operational complexity is incremental — those teams are already managing distributed infrastructure.

- **Per-client visibility filtering.** The current Arcane implementation serializes one delta per tick and broadcasts identical bytes to all clients (no per-client filtering). For production use with anti-cheat requirements (preventing wallhacks), per-client visibility filtering is needed. The architecture supports this efficiently: serialize each entity's delta once (O(M)), then assemble per-client messages by selecting which pre-serialized entities each client should see (O(N×K) byte copying, not re-serialization). See [GitHub issue #4](https://github.com/brainy-bots/arcane/issues/4).

- **NPCs and game logic.** NPCs that interact with persistent game state (inventory, quests, loot) still generate SpacetimeDB reducer calls. The NPC advantage applies to the high-frequency part of NPC state (position/movement — ~90%+ of traffic), which is eliminated from SpacetimeDB. Low-frequency discrete events (sell item, drop loot, trigger quest) still go through SpacetimeDB reducers, but at trivial rates compared to position updates.

- **SpacetimeDB is evolving.** These benchmarks were run against SpacetimeDB's current architecture (single-threaded reducer pipeline, per-write subscription evaluation). The SpacetimeDB team has stated MVCC and parallel execution are planned. If implemented, the single-instance ceiling would increase. However, the N² scaling relationship for all-to-all visibility is fundamental to the subscription model and would remain — the crossover point would shift to a higher player count, but the architectural advantage of linear distribution would still hold.

### Benchmark version

| | |
|---|---|
| **Date** | March 2026 |
| **SpacetimeDB version** | Latest stable (local `spacetime start`) |
| **Hardware** | AMD Ryzen 7 (8c/16t, ~4.5 GHz), 64 GB DDR4, Windows 10, loopback network |
| **Client stub** | `arcane-swarm-sdk` using `spacetimedb-sdk` (WebSocket, BSATN, typed reducers) |
| **Module schema** | Entity (entity_id, x, y, z), Inventory, GameEvent — update-in-place, no velocity fields |
