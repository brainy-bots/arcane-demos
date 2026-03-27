# State of the Art: Real-Time Game / Multiplayer Backends

Research summary for comparing Arcane against the market. Focus: **published limits, scaling model, and what “enough” means** for real-time entity/replication workloads.

---

## 1. SpacetimeDB

**What it is:** Single logical database, in-memory, real-time subscriptions. Reducers (functions) mutate state; clients subscribe to tables and get live updates.

**Published / implied limits:**
- **Pricing (Maincloud):**
  - Free: ~3M function calls/month, 12.5 GB egress, 1 GB table storage.
  - Pro ($25/mo): ~120M function calls/month, 500 GB egress, 40 GB table storage.
  - Team ($250/mo): ~300M function calls/month, 1.25 TB egress, 100 GB table storage.
- **Throughput claim:** “Over 150,000 transactions per second” in benchmark vs Node.js + PostgreSQL (~1,500 txn/s). Source: blog/pricing narrative; no public benchmark report found.
- **Hardware (Maincloud):** “Enterprise grade machines … upwards of 80 cores and 256 GB memory.” Single machine; “enormous throughput from a single machine.”
- **Docs:** No explicit “max entities,” “max concurrent subscriptions,” or “max reducer calls per second” in public docs. Performance best practices: indexes, batch ops, small types, private tables, table decomposition.

**Scaling model:**
- **Scale up:** Single process, single machine. Bigger instance = more capacity.
- **Scale out:** Not documented for standalone. Team/Enterprise: “Custom scalability & replication,” “Ability to reserve dedicated nodes”—implies multi-node is enterprise/custom, not standard horizontal sharding.
- **Scale to zero:** Free tier: DB can pause after 1 week inactivity.

**Takeaway:** Ceiling is one machine (or one logical DB). No public horizontal sharding; “enough” is defined by that single system’s limits. To find the real limit you’d run on a large instance (or Maincloud) and push until saturation.

---

## 2. Nakama (Heroic Labs)

**What it is:** Open-source distributed game server (Go). Auth, storage, matchmaking, real-time matches, leaderboards. Authoritative multiplayer: each match can run in Lua/JS/Go with 10 ticks/s.

**Published limits (from official benchmarks):**
- **Methodology:** Tsung load testing; GCE; single-node (OSS) vs 2-node cluster (Enterprise); DB on CloudSQL (constant).
- **CCU (concurrent socket connections):**
  - 1 node, 1 CPU / 3 GB RAM: **~20,277** max connected; mean connect time ~21 ms.
  - 2 nodes, 1 CPU / 3 GB RAM each: **~29,550** CCU.
  - 2 nodes, 2 CPU / 6 GB RAM each: **~35,723** CCU.
- **Throughput (requests/sec):**
  - User registration: ~500–940 req/s (1 node ~528/s, 2 nodes up to ~939/s).
  - Auth: ~530–930 req/s.
  - Simple RPC (Lua/JS/Go): ~700–825 req/s per config.
  - Authoritative match workload (10 ticks/s, 10 players per match, broadcast/echo): ~39–103 req/s mean depending on config; network ~7–12 Mbit/s sent, ~48–62 Mbit/s received.
- **Scalability:** “Scale for millions of concurrent players”; “hundreds of thousands of simultaneous connections”; “scale both up and out.” Cluster = multiple Nakama nodes + shared DB.

**Scaling model:**
- **Horizontal:** Yes. Add nodes behind load balancer; more nodes → more CCU and RPS.
- **Per-match:** One match = one match handler (Lua/JS/Go); 10 players per match in the benchmark; tick rate 10/s.

**Takeaway:** Well-documented benchmarks. CCU and RPS scale with node count. Real-time “entity” load in the benchmark is match-centric (messages per match), not “N world entities at 20 Hz” so not directly comparable to a single-world entity replication ceiling.

---

## 3. Photon (Exit Games / Unity)

**What it is:** Managed real-time multiplayer (Photon Fusion, PUN, etc.). Often used with Unity.

**Findings:** No clear public docs found for “max CCU,” “max entities per room,” or “max updates per second” in search. Pricing and capacity are typically “CCU-based” (concurrent users); exact caps and pricing are on vendor pages (not fully captured here). Consider checking photonengine.com pricing and limits directly.

---

## 4. PlayFab (Microsoft)

**What it is:** Full game backend (auth, economy, leaderboards, multiplayer via Azure PlayFab Multiplayer Servers or other Azure services).

**Findings:** No specific “replication limit” or “entities per server” found in search. Multiplayer capacity is usually expressed in terms of sessions/servers and scaling rules, not a single “entity count” number. Worth checking current Azure/PlayFab multiplayer docs for session size and VM sizing.

---

## 5. Unreal Engine (built-in replication)

**What it is:** Default net driver replicates actors; server is authority.

**Published / implied limits:**
- No hard-coded “max replicated actors.” Limits come from:
  - **NetClientTicksPerSecond:** Throttles how many clients are updated per frame (bandwidth/CPU).
  - **Relevancy:** `IsNetRelevantFor()`; distance, dormancy, `bOnlyRelevantToOwner`.
  - **NetUpdateFrequency:** How often each actor replicates.
- Best practices: replicate only what’s needed, lower update frequency, quantized types. So “limit” is practical (bandwidth, CPU, relevancy), not a single number.

**Takeaway:** Industry baseline for “many actors” is “design for relevancy and update rate”; no official “X entities max.” Community wisdom: dozens to low hundreds of frequently replicated actors per client are typical before tuning.

---

## 6. Gaps and comparison angles

**What’s missing in public info:**
- **SpacetimeDB:** No published “max entities at 20 Hz” or “max reducer calls/sec” per instance; no standard benchmark for game-like entity replication.
- **Nakama:** Benchmarks are auth/RPC and match-based (messages per match), not “one world, N entities, 20 Hz position updates.”
- **Photon / PlayFab:** CCU and session-oriented; entity/replication limits not clearly stated in search results.

**Scaling model comparison:**

| System           | Scale out (add machines) | Single-system ceiling      | Game-like entity replication benchmarks |
|-----------------|---------------------------|----------------------------|-----------------------------------------|
| SpacetimeDB     | Not standard; enterprise  | One machine (e.g. 80c/256GB) | No public “N entities at 20 Hz”        |
| Nakama          | Yes (cluster)             | Per-node CCU ~20k–35k      | Match messages, not world entities      |
| Arcane (yours)  | Yes (more cluster nodes)  | Per-cluster limit          | Demo: entity count at 20 Hz              |

**How to prove “better than the standard”:**
1. **Define a target:** e.g. “500 entities at 20 Hz in one world” or “X concurrent users with position updates at Y Hz.”
2. **Measure SpacetimeDB on one instance:** Ramp entities (or reducer rate) until CPU/latency/errors saturate. That’s “SpacetimeDB limit on this hardware.”
3. **Measure Arcane:** Same workload on one cluster node; then add nodes and show total capacity going up while per-node load stays bounded.
4. **Compare to Nakama:** Use their published CCU/RPS as “industry benchmark” for connectivity and RPC; position Arcane as “same or better connectivity, plus explicit high-entity real-time world with horizontal scaling.”

---

## 7. What each solution builds (and how Arcane compares)

### MMO / persistent world: who actually targets it?

**None of the mainstream products (Nakama, Photon, PlayFab) are focused on MMOs or persistent multiplayer servers.** They’re built for match-based or session-based games: you get a match/room, you play, the session ends. No “one persistent world, always on, many entities, scale by adding servers.”

**In our comparison set, the one that overlaps with “persistent state / one world” is SpacetimeDB:** it’s a database (tables, persistence, real-time subscriptions), so it can back a persistent world — but as a single logical DB, scale-up only.

**Actual MMO / persistent-world backends** are a different, smaller niche: e.g. Redwood, AvA (Adaptive Virtual Architecture), Donet (open-source), or legacy stacks (BigWorld). They’re not the same market as “game backend as a service” (Nakama, Photon, PlayFab).

So for **Arcane** (persistent world, many entities, scale out with clusters): the direct comparison is **SpacetimeDB** (one DB, one world, scale up), and optionally those MMO-focused backends. Nakama/Photon/PlayFab are “different category” — useful to know, but they’re not in the “persistent multiplayer server” lane.

### Primitive: match/room/session vs world/entity

| Product       | What it builds | Spatial? | Clustering? |
|---------------|----------------|----------|-------------|
| **Nakama**    | **Matches.** Matchmaker pairs players → they join a **match**. Each match runs server logic (Lua/JS/Go), tick rate 10/s, N players per match. Many independent matches; no “one world.” | No. Matches are isolated sessions, not regions of a shared world. | Yes: more Nakama nodes = more concurrent matches + more CCU. Not “one world split across nodes.” |
| **Photon**   | **Game sessions (rooms).** Matchmaking → join a **session**. Sessions have a player cap; “Spaces” can route overflow to new rooms. Many sessions; each is its own bubble. | “Spaces” = logical areas (e.g. lobby vs arena); overflow creates new rooms. Not MMO-style spatial partitioning of one world. | Scale = more CCU, more sessions. No built-in “world sharded across servers.” |
| **PlayFab**  | **Matchmaking + server allocation.** Match found → PlayFab **allocates a multiplayer server** (your build) for that match. They orchestrate; your server runs the game. | No. They assign a server to a match; spatial is in your server code if you build it. | Scale = more allocated servers (more matches). Your server can be one instance or your own cluster. |
| **SpacetimeDB** | **One global database.** Tables + reducers; clients subscribe to tables, call reducers. One logical DB = one host (one big machine). | No. You can put position in tables and subscribe by region in your schema, but the system is “one DB,” not “world split across nodes.” | No. Scale up only (bigger machine). Replication in docs = backup/HA, not horizontal sharding. |
| **Arcane**   | **Clusters + entity replication.** Manager assigns client to a **cluster**. Clusters run simulation; **entity state** (position, velocity, cluster_id) is replicated via WebSocket; Redis syncs state between clusters. One logical world; **multiple cluster nodes** each own a subset of entities. | Yes. Multi-cluster = world distributed across servers; `cluster_id` = ownership; replication so clients see merged view. Designed for “many entities in a world” and adding clusters to scale. | Yes. More cluster nodes = more capacity for the same world; per-cluster load stays bounded. |

### Are they “spatial” or “clustering” in the MMO sense?

- **Spatial (MMO-style):** One continuous world; space is partitioned across servers (zones, shards, or dynamic); entities have position; authority can hand off by region. **None of Nakama, Photon, or PlayFab ship this.** You can build it on top (e.g. your server does zones); their primitive is match/session/room.
- **SpacetimeDB:** One central DB. You could build spatial (tables with x/y/z, subscribe by bounds); the DB itself is not “spatially partitioned” — it’s one process.
- **Arcane:** Built for distributed world state: clusters, entity replication, cluster_id, inter-cluster sync (Redis). Closer to “spatial partitioning” and “one world, many servers” than the others.

### Better, worse, or different?

- **Different primitives.** They optimize for **many small sessions** (matches, rooms) or **one global DB**. Arcane optimizes for **one world, many entities, scale out with more cluster nodes**.
- **Arcane is better for:** Large shared worlds, high entity count in one view, scaling by adding cluster servers while keeping one logical world. Same niche as “MMO / battle royale replication layer” (e.g. SpatialOS-style), not “lots of 10-player matches.”
- **They are better for:** Match-based games (MOBA, 5v5, battle royale *lobbies*), auth + economy + matchmaking, or “one source of truth in a single DB.” Photon/Nakama also have mature SDKs and ecosystems.
- **Summary:** Arcane is not “Nakama but worse” or “SpacetimeDB but worse” — it’s **aimed at a different problem**: many entities in a single world with horizontal scaling by clusters. For that problem, the others don’t offer the same primitive; for match/session or single-DB workloads, they’re the incumbent fit.

---

## 8. SpacetimeDB throughput on smaller machines, and “same load, cheaper?”

### Do we have data?

**No.** SpacetimeDB don’t publish throughput by machine size (e.g. “X txn/s on 4 vCPU”). We only have:
- Their claim: “over 150,000 transactions per second” (benchmark vs Node+Postgres).
- Their Maincloud wording: “enterprise grade machines … upwards of 80 cores and 256 GB memory.”

So we **cannot** state with confidence how many txn/s SpacetimeDB does on a small instance (e.g. 4–8 vCPU, 16 GB RAM).

### Rough extrapolation (high uncertainty)

If we assume throughput scales **roughly** with CPU (many DBs scale sub-linearly, so this is an upper-bound style guess):

- 150,000 txn/s on 80 cores → **~1,875 txn/s per core** (linear).
- On an **8-core** box: **~15,000 txn/s** (if linear); in practice often lower (e.g. 1.5–2x going 10x in cores), so **~5,000–15,000 txn/s** is a very loose ballpark for 8 cores.

**Caveat:** This is not from SpacetimeDB; it’s a generic “scale with cores” guess. Real numbers need measuring SpacetimeDB on that hardware.

### Our demo load in “txn” terms

In the SpacetimeDB demo we use **one reducer call per tick** (`set_entities` with the full entity list) at **20 Hz**:
- **20 reducer calls/s** (20 “transactions” per second), regardless of entity count in the batch.
- So 200 entities @ 20 Hz = **20 txn/s**; 1,000 entities @ 20 Hz = still **20 txn/s** (bigger payload per txn).

So we are **far below** 150k txn/s. The useful comparison is: at what **entity count and tick rate** does SpacetimeDB on a given machine start to saturate (CPU, latency, or errors)? That has to be measured.

### Can we show “same result with less powerful machines”?

**Only if we measure both.** We’d need to:
1. Pick a **concrete load** (e.g. 200 entities @ 20 Hz, or 500 @ 20 Hz).
2. Run **SpacetimeDB** on a **small** machine (e.g. 4–8 vCPU, 16 GB); ramp entities/tick rate until it saturates; record max sustainable load and CPU/RAM.
3. Run **Arcane** (one cluster node) on the **same or smaller** machine with the **same** load; record CPU/RAM.

If Arcane sustains the same load at **lower** CPU or on a **smaller** instance, then we can claim “same result with less powerful machines.” If Arcane needs **more** total resources (e.g. several small nodes) to match one SpacetimeDB box at that load, then for that load we’re not cheaper — our advantage is **only** that we can scale past their ceiling by adding nodes (and beyond that ceiling we might be cheaper because they can’t scale out).

So today:
- **We do not know** whether Arcane is more efficient per machine at a given load (no side-by-side benchmark).
- **We can demonstrate** that we scale horizontally (add cluster nodes → more capacity); SpacetimeDB doesn’t (single host).
- **To answer “same result with less powerful machines?”** we need to run that comparison (same load, same or smaller hardware, measure both).

### Estimating users SpacetimeDB could handle (spread vs all together)

Use **requests per second per user** (e.g. 20–60 txn/s) to ballpark max users from 150k txn/s on the big 80c/256GB host.

- **Spread around:** 150,000 ÷ 20 = **~7,500 users** (20 txn/s per user); 150,000 ÷ 60 = **~2,500 users** (60 txn/s). So **order of 2.5k–7.5k users** if load is spread (no single hot spot).
- **All together:** Same reducer + everyone subscribed to the same table → broadcast cost ≈ updates/s × N clients = **20N²** fan-out. That dominates: e.g. 1,000 users → 20M messages/s. So "everyone in one room" is likely **hundreds to low thousands** (500–2k?) before latency/CPU blows up; SpacetimeDB don't publish this — needs measurement.

### Summary

| Question | Answer |
|----------|--------|
| SpacetimeDB txn/s on a small machine? | Unknown. No published data. Rough guess (e.g. 8 cores → order of 5k–15k txn/s) is possible with heavy caveats. |
| Can we prove “same result with less powerful machines”? | Not without benchmarking both on the same load and machine size. |
| What we can prove today? | We scale horizontally (add nodes); SpacetimeDB has a single-host ceiling. For the same load below their ceiling, cost comparison is open until we measure. |
| Users SpacetimeDB could handle (spread)? | ~2.5k–7.5k (150k txn/s ÷ 20–60 txn/s per user), on the big box. |
| Users SpacetimeDB could handle (all together)? | Likely lower (hundreds–low thousands) due to broadcast/contention; no published number. |

---

## 9. Sources (summary)

- SpacetimeDB: spacetimedb.com (pricing, docs), performance best practices, blog/gist on 150k txn/s.
- Nakama: heroiclabs.com/docs/nakama/getting-started/benchmarks (Tsung benchmarks, CCU, RPS, match workload).
- Unreal: Epic docs (replication, performance/bandwidth).
- Photon / PlayFab / AccelByte: limited concrete numbers in search; vendor docs and pricing pages are the next step.

---

*Generated for Arcane demo comparison. Update as new benchmarks or vendor docs appear.*

---

## Quick reference: Arcane vs others

| | **Arcane** | **Nakama** | **Photon** | **PlayFab** | **SpacetimeDB** |
|--|------------|------------|------------|-------------|------------------|
| **Primitive** | Clusters + entity replication | Matches (server-side match handler) | Sessions/rooms | Matchmaking + allocate your server | One DB, tables + reducers |
| **Spatial / one world** | Yes (multi-cluster, merged view) | No (many isolated matches) | No (many sessions; Spaces = routing) | No (you build it) | No (one DB; you model space) |
| **Scale out** | Add cluster nodes | Add nodes (more matches) | More CCU/sessions | More allocated servers | Scale up only |
| **Good for** | MMO-style, many entities, one world | Match-based games, auth, leaderboards | Real-time sessions (FPS, etc.) | Full backend + matchmaking + your servers | Global state, real-time subscriptions, single logical DB |
