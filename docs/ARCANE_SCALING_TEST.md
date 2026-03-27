# Arcane + SpacetimeDB scaling test

Test where **physics runs on Arcane cluster servers** and **SpacetimeDB is used only for batch persistence**. This matches the intended split: real-time simulation on clusters, durable state in SpacetimeDB.

## Setup

- **Arcane cluster (arcane-cluster-demo)**: Runs the tick loop with client-driven state. Each client sends `PLAYER_STATE` over WebSocket; the cluster applies updates, runs physics (movement), and broadcasts entity deltas to connected clients. Optionally injects demo agents (gravity, jump) when `DEMO_ENTITIES` > 0; for scaling we use `DEMO_ENTITIES=0` so only player load is measured.
- **SpacetimeDB persist**: Each cluster periodically POSTs entity positions to SpacetimeDB’s `set_entities` reducer (position-only: `entity_id`, `x`, `y`, `z`). Low frequency (e.g. 1 Hz) so SpacetimeDB is not in the hot path.
- **arcane-manager**: Clients call `GET /join`; manager returns a cluster URL (round-robin). So with N clusters and M players, each cluster gets ~M/N connections.
- **arcane-swarm**: Simulates M players; each connects to the manager, gets assigned to a cluster, then sends `PLAYER_STATE` at tick rate and receives state deltas.

## Prerequisites

1. **Redis** running (e.g. `redis-server`), default `redis://127.0.0.1:6379`.
2. **SpacetimeDB** running: in a separate terminal run `spacetime start`. The script can build and publish the module from `spacetimedb_demo/spacetimedb` if you don’t pass `-NoPublish`.

## How to run

From `arcane-demos/scripts/swarm`:

```powershell
# One cluster, 250 players (physics on cluster, persist to SpacetimeDB)
.\Run-ArcaneScalingSweep.ps1 -NumServers 1 -PlayersTotal 250

# Two clusters, 500 players (250 per cluster)
.\Run-ArcaneScalingSweep.ps1 -NumServers 2 -PlayersTotal 500

# Four clusters, 1000 players
.\Run-ArcaneScalingSweep.ps1 -NumServers 4 -PlayersTotal 1000
```

Results are appended to `arcane_scaling_sweep.csv` (columns: `num_servers`, `players`, `total_calls`, `total_errs`, `err_rate_pct`, `lat_avg_ms`, `pass`). Pass = err_rate &lt; 1% and lat_avg_ms &lt; 200.

## What to expect

- **Per-cluster load**: Physics and broadcast run on the cluster; expect a lower player ceiling per cluster than the SpacetimeDB-only ceiling (where SpacetimeDB did physics and subscription fanout). So you may see e.g. ~200–400 players per cluster stay under 200 ms, depending on hardware.
- **Scaling**: As you add clusters (`-NumServers`), total players can grow (e.g. 2 servers → 500, 4 servers → 1000) while keeping per-cluster load and latency in check. That’s the intended advantage over a single SpacetimeDB instance.

## Logging and bottleneck visibility

The script writes **cluster and manager logs** under `arcane_scaling_logs/` (or `-LogDir`) so you can see which component is limiting:

| Log / metric | Source | What it tells you |
|--------------|--------|-------------------|
| **ArcaneServerStats: entities=… tick_ms=…** | Each cluster (stderr) | **Cluster CPU**: time per tick. If `tick_ms` grows (e.g. >50 ms at 20 Hz), the cluster is the bottleneck. |
| **SpacetimeDB persist: N entities in Xms** | Cluster (stderr) | **SpacetimeDB**: batch persist latency. Occasional line every ~10 persists. High Xms or "SpacetimeDB persist error" = DB or network. |
| **FINAL: total_calls, total_errs, lat_avg_ms** | arcane-swarm (script captures) | **Client-side**: aggregate write latency and errors. High errs or lat = client/network or cluster not keeping up. |

After each run the script prints a short **Bottleneck visibility** summary: last/max `tick_ms` per cluster and any SpacetimeDB persist time/errors. Use it to attribute:

- **High tick_ms** → cluster (physics + broadcast) overloaded.
- **High persist ms or persist errors** → SpacetimeDB or network.
- **High client err/lat, low tick_ms** → client simulator or connection limit.

For **CPU and memory** per process, use Task Manager or Resource Monitor during the run (arcane-cluster-demo, arcane-swarm, spacetime, redis). That shows which process is saturating the machine.

## Env vars (for clusters)

Set by the script; you can override for manual runs:

- `CLUSTER_ID`, `CLUSTER_WS_PORT`: per process.
- `SPACETIMEDB_PERSIST=1`, `SPACETIMEDB_URI`, `SPACETIMEDB_DATABASE`: enable and target SpacetimeDB.
- `DEMO_ENTITIES=0`: no NPCs; player-only load.
