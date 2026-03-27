# Arcane Demos

Minimal examples that show how to use the Arcane library and Unreal client plugin: Unreal demo game, backend demo server (Rust), and scripts for running and benchmarking.

## Three-repo layout

- **arcane** — Rust library (core, spatial, rules, pool, infra + reference server). Clone next to this repo: `E:\code\arcane` and `E:\code\arcane-demos`.
- **arcane-client-unreal** — Unreal Engine plugin. This demo project includes a **copy** of the plugin under `Unreal/ArcaneDemo/Plugins/ArcaneClient`. To update the plugin, replace that folder from a fresh clone of arcane-client-unreal.
- **arcane-demos** (this repo) — Unreal demo game + backend demo crate + scripts.

## Prerequisites

- **Rust** — to build the backend demo and the arcane library.
- **Unreal Engine 5.x** — to open and run the Unreal demo.
- **arcane** repo — clone so it sits **next to** arcane-demos (e.g. `E:\code\arcane`, `E:\code\arcane-demos`).

## Run the demo (single cluster, 200 entities)

1. From this repo root:
   ```powershell
   .\scripts\run_demo.ps1
   ```
   This builds the manager (from arcane) and cluster-demo (from this repo), starts the manager in a new window, then runs the cluster in the current window.

2. Start the Unreal client:
   - Open `Unreal/ArcaneDemo/ArcaneDemo.uproject` in Unreal Editor and press **Play**, or
   - Run verification: `.\scripts\verification-loop\run_verification.ps1 -BuildAndRunGame -CloseAfter`

3. The client joins via `GET http://127.0.0.1:8081/join`, connects to the cluster, and displays replicated entities. HUD shows entity count and FPS.

## Multi-cluster (Redis)

1. Start Redis: `docker compose up -d` (from this repo root).
2. Run: `.\scripts\run_demo_multi.ps1` (manager + 3 clusters in separate windows).
3. Start the Unreal client as above. You’ll see entities from all clusters, colorized by cluster.

## Benchmark (Arcane vs Unreal)

```powershell
.\scripts\benchmark\run_benchmark.ps1
```
Runs both Arcane and default Unreal networking at several entity counts, parses FPS from the game log, and writes `scripts/benchmark/benchmark_results.csv` and a chart. See `scripts/benchmark/BENCHMARK.md`.

## Unreal demo project

- **Path:** `Unreal/ArcaneDemo/`
- **Plugin:** `Plugins/ArcaneClient` (copy of arcane-client-unreal).
- **Docs:** `Unreal/ArcaneDemo/README.md` for in-engine details.

## Backend demo (Rust)

- **Crate:** `crates/arcane-demo` — depends on the **arcane** repo (path `../../../arcane/crates/arcane-infra`). Must have arcane cloned next to arcane-demos.
- **Binary:** `arcane-cluster-demo` — cluster server with demo agents (gravity, jump, wander). Used by `run_demo.ps1` and `run_demo_multi.ps1`.

## Versioning

Demos are “living” examples; we don’t tag them. Pin to specific versions of **arcane** and **arcane-client-unreal** (e.g. `v0.1.0`) for reproducible builds.

See `docs/COMPATIBILITY_MATRIX.md` for tested repo/version combinations and expected contract compatibility.
See `docs/ARCHITECTURE_INDEX.md` for architecture navigation and ownership boundaries.
See `docs/MODULE_INTERACTIONS.md` for repository module responsibilities and integration boundaries.
See `docs/ARTIFACT_BOUNDARY_POLICY.md` for source vs generated-output rules.
