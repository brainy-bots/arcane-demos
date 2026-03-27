# SpacetimeDB-only setup (no Arcane)

This describes the **SpacetimeDB-only** demo stack: one data source (SpacetimeDB), no Arcane manager or cluster. Use it to run the same Unreal demo and find **how far SpacetimeDB can scale** on your machine (entity count vs client FPS).

---

## What runs

| Component | Role |
|-----------|------|
| **SpacetimeDB server** | Local DB (`spacetime start`). Hosts the demo module (Entity table + `set_entities` reducer). |
| **arcane-spacetime-sim** | Rust binary that runs the same demo agents (gravity, jump, wander) at 20 Hz and POSTs full entity state to `set_entities` each tick. |
| **Unreal client** | Connects to SpacetimeDB via SDK, subscribes to `Entity` table, drives character actors from table rows (SpacetimeDBEntityDisplay). |

No Redis, no Arcane manager, no cluster WebSocket. The client talks only to SpacetimeDB.

---

## Prerequisites

- **SpacetimeDB CLI** — Install: `irm https://windows.spacetimedb.com -useb | iex` (Windows). Then `spacetime start` runs the server (default port 3000).
- **Rust** — To build the simulator: `cargo build -p arcane-demo --bin arcane-spacetime-sim --features spacetime-sim` (or use `run_demo_spacetime.ps1` which builds it).
- **Unreal project** — Built with SpacetimeDB plugin + generated bindings (`ModuleBindings/`, Entity table). Codegen: from `spacetimedb_demo/spacetimedb` run `spacetime generate -l unrealcpp` if you change the module schema.

---

## Run once (manual)

1. **Terminal 1 — SpacetimeDB server**
   ```powershell
   spacetime start
   ```
   Leave it running.

2. **Terminal 2 — Simulator** (from repo root)
   ```powershell
   .\scripts\run_demo_spacetime.ps1
   ```
   Publishes the module (first time), then runs the simulator (default 200 entities). Leave it running.

3. **Unreal** — Open the project, set Game Mode so **Use SpacetimeDB Networking** is **on** (Use Arcane Networking off). Press Play.

You should see the same humanoid characters; HUD shows "Entities: N | FPS: …". Data comes only from SpacetimeDB.

---

## Find SpacetimeDB limits (benchmark on your machine)

Use the benchmark script to run the game at **multiple entity counts** and record FPS. On a **high-end desktop (e.g. 64 GB RAM, 16 cores)** you can push entity counts high to find where FPS or stability breaks.

### One command (SpacetimeDB only)

1. **Start SpacetimeDB once** (separate terminal):
   ```powershell
   spacetime start
   ```

2. **Run the benchmark** (from repo root):
   ```powershell
   .\scripts\benchmark\run_benchmark.ps1 -Mode SpacetimeDB -EntityCounts 100,200,500,1000,2000,5000
   ```
   This will:
   - Publish the module once (then stop that process).
   - For each entity count (100, 200, 500, 1000, 2000, 5000): start the simulator with that many entities, launch the game in SpacetimeDB mode, capture log, parse FPS and entity count, then stop the simulator.
   - Write **scripts/benchmark/benchmark_results.csv** and generate **benchmark_results.png**.

### Suggested entity counts by machine

| Machine | Suggested `-EntityCounts` |
|---------|---------------------------|
| **High-end desktop (e.g. 64 GB RAM, 16 cores)** | `100,200,500,1000,2000,5000` or `200,500,1000,2000,5000,10000` to find the ceiling |
| **Mid-range** | `50,100,200,500,1000` |
| **Quick smoke test** | `50,200` |

Example for your 64 GB / 16-core desktop:
```powershell
.\scripts\benchmark\run_benchmark.ps1 -Mode SpacetimeDB -EntityCounts 100,200,500,1000,2000,5000
```

If the game or simulator crashes at a given N, the CSV may have a blank FPS for that row; you can note "crashed at N" and re-run with a narrower range around the limit.

### Skip Unreal build (faster re-runs)

If you already built the game and only want to re-run the benchmark:
```powershell
.\scripts\benchmark\run_benchmark.ps1 -Mode SpacetimeDB -EntityCounts 200,500,1000,2000 -SkipBuild
```

---

## Results

- **benchmark_results.csv** — Columns: Mode, Entities, FPS_median, FPS_min, FPS_max, Samples (and optional server columns for Arcane). One row per (Mode, Entities).
- **benchmark_results.png** — Chart of Entities vs FPS for each mode.

Use the CSV and chart to see where SpacetimeDB stays playable (e.g. FPS ≥ 30) and where it drops or fails. That gives you a **SpacetimeDB limit** for this setup on your hardware.
