# Arcane + SpacetimeDB benchmark setup

How to run the Arcane benchmark with **controlled cluster count** and find the **ceiling (max players) for each setup** so we get a curve and the optimal configuration.

---

## 0. What “cluster” means here

**One cluster = one Arcane server process.** It does **not** mean “one physical location” or “players in the same place.” A single cluster can hold up to ~N_max(1) players (e.g. ~800 on our hardware) **wherever** they are in the world — spread, in a city, in a dungeon, or all in one siege. Clustering in Arcane is not purely spatial: you can have many clusters handling the same area (e.g. 500v500 siege) so total capacity there exceeds one server’s ceiling, and a small group (e.g. 5 in a dungeon, no guild/party with the rest) can be in a separate cluster. So:

- **N_max(C)** = maximum **total** players the system can handle when we run **C** cluster servers (round-robin assignment). We find it by **increasing N until we hit the ceiling** for that C, not by fixing N.
- **Goal:** For C = 1, 2, 4, … find N_max(C), plot the curve, and choose the **(C, N)** that gives the **most players online**.

---

## 1. Single cluster vs multi-cluster

| Mode | What runs | Players per cluster | Use case |
|------|-----------|---------------------|----------|
| **Single cluster** | One `arcane-cluster` process. Swarm uses `--arcane-ws ws://127.0.0.1:8080` (no manager). | All N players on that one server. | "How many players can one cluster handle?" |
| **Multi-cluster** | C `arcane-cluster` processes (different ports). One `arcane-manager` with round-robin. Swarm uses `--arcane-manager http://127.0.0.1:8081`. | ~N/C per cluster (round-robin). | "How does replication load scale with C and with entities per cluster?" |

**Current default:** If you only pass `--backend arcane --arcane-ws ws://127.0.0.1:8080`, **all players connect to that single cluster**. So today’s Arcane numbers are "one server, N players."

---

## 2. Controlling players per server

- **Single cluster:** Don’t set `--arcane-manager`. Every player uses `--arcane-ws`. **Players per server = N** (total players).
- **Multi-cluster:** Set `--arcane-manager http://127.0.0.1:8081` and run the manager with `MANAGER_CLUSTERS` listing C cluster endpoints. Each join gets the next cluster in round-robin order. **Players per server ≈ N/C** (evenly if N divisible by C).

So you can benchmark e.g.:

- 400 players, 1 cluster → 400 players/server
- 400 players, 4 clusters → ~100 players/server
- 400 players, 8 clusters → ~50 players/server

Entity sync between clusters (Redis) is **per cluster**: each cluster sends its owned entity delta to its neighbors each tick. So replication volume scales with (entities per cluster) × (number of neighbors). With more, smaller clusters you get more replication links but fewer entities per link.

---

## 2b. Position vs persistent actions — who handles what

| Traffic | Rate per player | Handled by | SpacetimeDB? |
|--------|------------------|------------|--------------|
| **Position** (world state movement) | 10/s (tick-rate) | **Arcane cluster** (WebSocket). Cluster keeps state in memory and replicates to clients. | Only at **lower frequency**: cluster batch-persists (e.g. 1×/s `set_entities`) when `SPACETIMEDB_PERSIST=1`. So SpacetimeDB sees ~1 write/s for positions, not 10×N. |
| **Persistent actions** (inventory, use item, interact/trade) | e.g. 2/s (`--actions-per-sec 2`) | **SpacetimeDB reducers** only. | **Yes.** These are durable game state; they are sent as reducer calls (e.g. `pickup_item`, `use_item`, `player_interact`) and must hit SpacetimeDB so the database stays authoritative. |

So: the **10 position updates/s** are handled by Arcane and only written to SpacetimeDB at a **lower frequency** (e.g. 1 Hz batch). The **high-priority** traffic (inventory, trades, interactions) is **always** sent to SpacetimeDB because it is authoritative persistent state; the swarm (or in production the cluster on behalf of the client) calls the reducers so that state is durable and consistent.

---

## 2c. What we measure — cluster vs SpacetimeDB (apples-to-apples)

- **Cluster performance:** `total_calls` / `total_oks` / `lat_avg_ms` in the FINAL line = position updates **to the Arcane cluster** (writes/s and latency at the cluster). This answers “how many players can one cluster handle?”
- **SpacetimeDB performance (Arcane scenario):** To compare with the **SpacetimeDB-only** benchmark we must also measure **SpacetimeDB load** when fronted by Arcane. That load is:
  - **Batch persist:** 1 call/s (or `SPACETIMEDB_PERSIST_HZ`) per cluster — `set_entities` with all entities.
  - **Persistent actions:** when `--actions-per-sec 2`, each player sends 2 reducer calls/s to SpacetimeDB → **2×N** ops/s. The swarm prints `FINAL_SPACETIMEDB: action_calls=... spacetimedb_ops_per_sec=...` when actions are enabled.

So for **apples-to-apples** with the SpacetimeDB-only ceiling: run Arcane with `--actions-per-sec 2` (or your target rate), then compare **SpacetimeDB ops/s** in the Arcane run (batch persist + action_calls/s) to the **SpacetimeDB-only** reducer ceiling. That tells you whether **SpacetimeDB** is the limiter for total players when using Arcane.

---

## 3. How to run

### Prerequisites

- Redis: `redis-server` (default `127.0.0.1:6379`).
- SpacetimeDB (optional for persistence): `spacetime start` and module published; set `SPACETIMEDB_PERSIST=1` and `SPACETIMEDB_URI` / `SPACETIMEDB_DATABASE` on cluster processes if you want batch persist.

### Single cluster (all players on one server)

```powershell
# Terminal 1: one cluster server
$env:CLUSTER_ID = "550e8400-e29b-41d4-a716-446655440001"
$env:CLUSTER_WS_PORT = "8080"
# optional: $env:SPACETIMEDB_PERSIST = "1"
cargo run -p arcane-infra --bin arcane-cluster --features cluster-ws

# Terminal 2: swarm — all players to that one cluster
.\target\release\arcane-swarm.exe --backend arcane --arcane-ws ws://127.0.0.1:8080 --players 200 --tick-rate 10 --duration 60 --mode spread
```

No manager needed. **Players per server = 200.**

### Multi-cluster (round-robin, control players per server)

```powershell
# Terminal 1: cluster A
$env:CLUSTER_ID = "550e8400-e29b-41d4-a716-446655440001"
$env:CLUSTER_WS_PORT = "8080"
cargo run -p arcane-infra --bin arcane-cluster --features cluster-ws

# Terminal 2: cluster B
$env:CLUSTER_ID = "550e8400-e29b-41d4-a716-446655440002"
$env:CLUSTER_WS_PORT = "8082"
$env:NEIGHBOR_IDS = "550e8400-e29b-41d4-a716-446655440001"
cargo run -p arcane-infra --bin arcane-cluster --features cluster-ws

# Terminal 3: manager (round-robin over both clusters)
$env:MANAGER_CLUSTERS = "550e8400-e29b-41d4-a716-446655440001:127.0.0.1:8080,550e8400-e29b-41d4-a716-446655440002:127.0.0.1:8082"
$env:MANAGER_HTTP_PORT = "8081"
cargo run -p arcane-infra --bin arcane-manager --features manager

# Terminal 4: swarm — players distributed across the two clusters
.\target\release\arcane-swarm.exe --backend arcane --arcane-manager http://127.0.0.1:8081 --players 200 --tick-rate 10 --duration 60 --mode spread
```

**Players per server ≈ 100.** Each cluster replicates its entities to the other via Redis (NEIGHBOR_IDS).

Scale to C clusters by adding more `arcane-cluster` processes (each with its own `CLUSTER_ID`, `CLUSTER_WS_PORT`, and `NEIGHBOR_IDS`) and listing all of them in `MANAGER_CLUSTERS`.

---

## 4. What to measure

| Dimension | Meaning |
|-----------|--------|
| **Total players N** | Same as SpacetimeDB-only benchmark for comparison. |
| **Clusters C** | 1 = single server; C > 1 = multi-cluster with manager. |
| **Players per cluster** | N/C (round-robin). Drives per-cluster broadcast and replication size. |
| **Swarm metrics** | writes/s, ok, err, latency (same as today). |
| **Per-cluster stats** | If the cluster binary logs entity count and tick time, you can correlate with N/C and replication. |

For each **cluster count C**, we **find the ceiling N_max(C)** by increasing N until the run fails (err rate or latency over threshold). We do **not** fix N across setups: more clusters should allow a higher total N, so for C=2 we try N=800, 900, …, 1600, etc., until we hit the ceiling.

---

## 5. Ceiling sweep: find N_max(C) and the curve

For each C (number of cluster servers), find the **maximum total players N_max(C)** by running the swarm with increasing N until failure. That gives a curve **N_max vs C** and the **optimal (C, N)** that maximizes total players.

### Script

From repo root (arcane-demos):

```powershell
.\scripts\swarm\Run-ArcaneCeilingSweep.ps1 -Clusters 1 -FindCeiling -Step 100 -MaxPlayers 2000 -Duration 45
```

- **Prerequisites:** Start Redis and **exactly C** cluster processes (and manager if C > 1) before running. The script only runs the swarm; it does not start cluster/manager.
- **Pass criteria:** `err_rate < 1%` and `lat_avg_ms < 200` (override with `-MaxErrRate` and `-MaxLatencyMs`).
- **FindCeiling:** Tries N = Step, 2×Step, 3×Step, … until a run fails or N > MaxPlayers. The last passing N is **N_max(C)**.
- **Output:** One CSV row per (C, N) attempt. Summary prints the ceiling for that C.

### Sweep: one run per cluster count

1. **C = 1**  
   Start one `arcane-cluster` on 8080. Run:
   ```powershell
   .\scripts\swarm\Run-ArcaneCeilingSweep.ps1 -Clusters 1 -FindCeiling -Step 100 -MaxPlayers 2000 -OutCsv arcane_ceiling_sweep.csv
   ```
   Record **N_max(1)** (e.g. ~800). Players per cluster = N_max(1).

2. **C = 2**  
   Start two clusters (ports 8080, 8082) and the manager with `MANAGER_CLUSTERS`. Run the same script with `-Clusters 2`. **Start from 800 and step up** (e.g. Step 100 → tries 100,200,…, then 800,900,…,1600 if you set MaxPlayers high enough). Record **N_max(2)** (e.g. 1600 if linear). Players per cluster ≈ N_max(2)/2.

3. **C = 4, 8, …**  
   Start C clusters + manager, run with `-Clusters C`, append to the same CSV. Each time we find **N_max(C)** by increasing N until failure.

### Curve and optimal configuration

- **If the limit is per-cluster** (each server’s CPU/broadcast), **N_max(C) ≈ constant × C**, so players-per-cluster is roughly constant. Example: N_max(1)=800, N_max(2)=1600, N_max(4)=3200 ⇒ ~800 per cluster.
- **If a shared bottleneck appears** (manager, Redis, or machine), **N_max(C)** flattens as C grows; players-per-cluster then drops.

Plot **N_max vs C**. The **configuration that gives the most players online** is the C with the largest **N_max(C)** (and total players = N_max(C)). Example: if N_max(1)=800, N_max(2)=1600, N_max(4)=2400, then best = 4 clusters with 2400 players; if N_max(4)=1500 (flattening), best = 2 clusters with 1600 players.

---

## 6. Recorded benchmark numbers (baseline)

**Purpose:** Keep a single place with the numbers we have so far. Update this section when you re-run with different hardware or pass criteria.

| Item | Value |
|------|--------|
| **Date recorded** | 2025-03 (single-machine runs) |
| **Hardware** | One machine: swarm + C cluster processes + manager + Redis (all on same host). |
| **Pass criteria** | err_rate &lt; 1%, lat_avg_ms &lt; 200 (sweep script). |
| **Run duration** | 30 s per N. |
| **Swarm** | `arcane_swarm --backend arcane`; position updates 10 Hz to Arcane WS. |

### Ceiling results (N_max vs C)

| Clusters C | N_max (ceiling) | Players per cluster | First N that failed |
|------------|------------------|----------------------|----------------------|
| 1 | **500** | 500 | 600 (1.26% err) |
| 2 | **1100** | 550 | 1200 (1.53% err) |
| 3 | **1100** | 367 | 1200 (1.03% err) |
| 4 | **1000** | 250 | 1100 (1.17% err) |

**Notes:** C=4 has shown 1100 in some runs and 1000 in others (variance). Best observed total on this setup: **2 or 3 clusters, 1100 players**. Curve is flat (more clusters do not increase total N_max); see §7 for bottleneck discussion.

Re-run with clusters (and Redis) on a dedicated server and the swarm on another machine to see if N_max(4) and beyond increase.

---

## 7. Why the ceiling is flat (and sometimes drops): bottlenecks

The curve N_max(1)=500, N_max(2)=1100, N_max(3)=1100, N_max(4)=1000 is **inconsistent** (C=4 sometimes 1100, sometimes 1000) and **flat** — more clusters don’t give proportionally more total players. Possible causes:

### 1. Redis

Each cluster **publishes** its entity delta to a topic every tick (20 Hz) and **subscribes** to (C−1) neighbor topics. So Redis handles:

- **Publishes/sec:** C × (C−1) × 20 (each cluster sends to C−1 neighbors).
- **Payload size:** One serialized delta per cluster per tick (all entities on that cluster).

So Redis traffic grows with **C²** and with **entities per cluster**. Redis is single-threaded for command execution. If Redis hits a throughput or latency limit, adding clusters doesn’t help and can **reduce** total N (more replication overhead, same or less headroom). That would explain: similar ceiling for C=2,3,4 and a drop for C=4 when Redis or the machine is under more load.

**How to check:** Run `redis-cli INFO stats` and `redis-cli --latency` during a run near the ceiling; or run Redis in a separate machine and see if N_max(C) increases.

### 2. Swarm (client) on the same machine

The **swarm** runs on the same box as the clusters, manager, and Redis. It has **N** threads, each doing a WebSocket send at 10 Hz. So the client process is doing 10×N sends/sec. If the swarm process (or the machine’s CPU) is the limiter, then **N_max** is “how many connections this one client process can drive,” not “how many players the servers can handle.” In that case N_max would be almost **independent of C** — we’d see a flat curve.

**How to check:** Run the **swarm on a different machine** (only clusters + manager + Redis on the server). If N_max(2) and N_max(4) go up a lot, the bottleneck was the client side.

### 3. Single-machine contention

All processes share one CPU: C cluster processes, 1 manager, Redis, 1 swarm. So we’re not measuring “capacity of C servers” in isolation. More clusters ⇒ more processes ⇒ more context switching and more Redis traffic ⇒ total capacity can **drop** (e.g. C=4 < C=3) when the machine is saturated.

**Summary:** The flat curve and run-to-run variance are consistent with **Redis** and/or the **swarm** (and overall single-machine contention) being the bottleneck, not the per-cluster server capacity. To get a clean “how does N_max scale with C?” answer, run clusters + Redis (and optionally manager) on a **dedicated server** and the **swarm on another machine**, and optionally monitor Redis (latency, commands/sec).

**Next steps:** For a comparison of observability vs Docker vs Kubernetes and a recommended order, see [ARCANE_BENCHMARK_NEXT_STEPS.md](ARCANE_BENCHMARK_NEXT_STEPS.md).
