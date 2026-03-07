# Demo to-do: from here on

What we still need to do so the demo **proves the library capabilities**. Then a separate phase for making it look better and adding gameplay.

**Goal (DEMO_GOAL.md):** Show join, multi-cluster, replication, and client the way a production MMO would; scale toward 200–400+ visible entities.

**Constraint:** The demo is a **library showcase**. Keep it plug-and-play: use only the plugin's public API (Connect, Tick, GetEntitySnapshot, ApplyEntityStateToActor); same character class and Unreal standards as a normal replicated game. No new library APIs for demo-only features.

---

## Phase 1: Prove the library (do these now)

| # | Step | Status |
|---|------|--------|
| 1 | **Verify multi-cluster flow** | ✅ | Run Redis (`docker compose up -d`) + `run_demo_multi.ps1` (4 windows), then `run_verification_multi.ps1` or manual verification. **Pass:** Verification runs; client connects, receives merged entity state (~207 entities), FPS ~20–26. HUD shows entity count + FPS; "Clusters: 3" when snapshot has distinct cluster IDs. |
| 2 | **Document scale: 100 entities (single-cluster)** | ✅ | Set `DEMO_ENTITIES=100` in run_demo.ps1, run verification, record FPS (log/HUD). Add one line to DEMO_GOAL or README: e.g. "Tested: 100 entities at ~X FPS." |
| 3 | **Document scale: 150 entities (multi-cluster 50×3)** | ✅ | Run multi-cluster at 50 per cluster. Documented in DEMO_GOAL: run_verification_multi.ps1 passes; 207 entities visible at ~20–26 FPS. |
| 4 | **Push to 200 entities and document limit** | ✅ | Single-cluster 200 in run_demo.ps1. Verified: ~200 entities at ~40 FPS (HUD may show 201 with player). Documented in DEMO_GOAL. |
| 5 | **Optional: show authority/handoff** | 📌 Later | Current demo: entities stay on spawn cluster (no dynamic handoff). Library has cluster_id per entity; handoff could be added later. Skip for Phase 1. |

**Phase 1 complete** for “demo runs and scales.” To **prove the library is better than baseline**, do Phase 1b below.

---

## Phase 1b: Prove library beats baseline (benchmark + chart)

| # | Step | Status |
|---|------|--------|
| 1 | **Run benchmark (Unreal mode)** | ⬜ | `.\scripts\benchmark\run_benchmark.ps1 -Mode Unreal` — no backend; runs game at 20, 50, 100, 150, 200 bots, parses FPS from log, writes CSV. |
| 2 | **Run benchmark (Arcane mode)** | ⬜ | `.\scripts\benchmark\run_benchmark.ps1 -Mode Arcane` — starts backend per entity count, runs game, parses FPS, appends to CSV. (Or run `-Mode Both` once.) |
| 3 | **Generate and publish chart** | ⬜ | Script produces `scripts/benchmark/benchmark_results.png` (Entities vs FPS, Arcane vs Unreal). Add to repo or docs; use it to show where each hits its limit. |
| 4 | **Document “library beats baseline”** | ⬜ | One line in DEMO_GOAL or README: e.g. “At 200 entities, Arcane ~X FPS vs Unreal ~Y FPS” and “Max playable entities: Arcane N vs Unreal M.” |

**Phase 1b complete** when the chart exists and the comparison is documented. That is the proof.

---

## Phase 1c: SpacetimeDB-only networking (measure SpacetimeDB limit)

To compare against SpacetimeDB (a target we aim to improve on), add a **SpacetimeDB-only** path: same scenario (N entities, same place, 20 Hz), but state flows through SpacetimeDB instead of Arcane. Then run the same benchmark and record FPS vs entity count for SpacetimeDB.

| # | Step | Status |
|---|------|--------|
| 1 | **SpacetimeDB module** | ⬜ | Spade: `Entity` table + reducer(s) for insert/update. Publish to local `spacetime start`. |
| 2 | **External simulator** | ⬜ | Rust (or other) process: run demo agents at 20 Hz, invoke SpacetimeDB reducer each tick with entity state. Env: `DEMO_ENTITIES`, optional `STRESS_RADIUS`. |
| 3 | **Unreal SpacetimeDB mode** | ⬜ | Use SpacetimeDB Unreal SDK; subscribe to `Entity` table; spawn/update actors and apply position/velocity (same display as Arcane). FPS logging in same format for benchmark. |
| 4 | **Benchmark SpacetimeDB** | ⬜ | Script or `run_benchmark.ps1 -Mode SpacetimeDB`: start SpacetimeDB, publish, run simulator, run game, parse log, add CSV row. Chart: Arcane vs Unreal vs SpacetimeDB. |

**Plan and checklist:** `docs/SPACETIMEDB_NETWORKING_PLAN.md`.

---

**How to verify multi-cluster (Step 1):**

**Option A — one command:** From repo root, ensure Redis is running (`docker compose up -d`), close any single-cluster windows, then run:
`.\scripts\verification-loop\run_verification_multi.ps1`
This starts the 4 multi-cluster windows, waits for them, then runs build + game + sequence + capture. Add `-Force` if manager is already running (e.g. you started multi-cluster manually).

**Option B — manual:** 1) Start Redis: `docker compose up -d`. 2) Close single-cluster if running. 3) Run `.\scripts\run_demo_multi.ps1` (4 windows). 4) Run `.\scripts\verification-loop\run_verification.ps1 -BuildAndRunGame -CloseAfter` (no `-StartBackend`). 5) In-game: HUD shows **"Entities: 150 | Clusters: 3 | FPS: …"**; entities in **three colors** (red/green/blue by cluster); log "Arcane Demo ready: 150 entities from 3 clusters visible." 6) If it fails, check manager URL, Redis, and cluster logs.

---

## Phase 2: Polish and gameplay (after Phase 1)

| # | Step | Status |
|---|------|--------|
| 1 | **Better look** | ⬜ | Improved textures, materials, or environment so the demo looks shinier. |
| 2 | **Enemies** | ⬜ | Add enemy entities (e.g. different mesh/behavior) to show the same replication path for multiple entity types. |
| 3 | **Attacks and interactions** | ⬜ | Basic attacks or interactions (player vs entities, or entity vs entity) to show game logic on top of the library. |

Phase 2 is about **looking good and feeling like a game**; Phase 1 is about **proving the stack works and scales**.

---

## Quick reference

- **Single-cluster:** `.\scripts\run_demo.ps1` (`DEMO_ENTITIES` in script; currently 200).
- **Multi-cluster:** Redis (`docker compose up -d`) then `.\scripts\run_demo_multi.ps1` (3 clusters, 50 per cluster = 150 total).
- **Verification (single-cluster):** `.\scripts\verification-loop\run_verification.ps1 -BuildAndRunGame -StartBackend -CloseAfter`
- **Verification (multi-cluster):** `.\scripts\verification-loop\run_verification_multi.ps1` (starts multi-cluster + runs verification). Prereq: Redis running, single-cluster closed.
- **200-entity test:** Restart single-cluster (`run_demo.ps1`), then run verification; note FPS in log/HUD and add to DEMO_GOAL.
- **Demo ready log:** `Arcane Demo ready: N entities visible, replication active.` in game log when synced.
- **Benchmark:** `.\scripts\benchmark\run_benchmark.ps1` (optional `-Mode Unreal` or `-Mode Arcane` or `-Mode Both`). Output: `scripts/benchmark/benchmark_results.csv` and `benchmark_results.png`.

---

## How to present the demo (one-pager)

1. **Start backend:** `.\scripts\run_demo.ps1` (single) or Redis + `.\scripts\run_demo_multi.ps1` (multi). Leave windows open.
2. **Start the game;** client joins via `GET http://127.0.0.1:8081/join`, gets cluster, receives replicated state.
3. **Point out:** HUD shows Entities + FPS. Humanoids same mesh as player, path-like movement. Multi-cluster: color by cluster = ownership.

**Proves:** Central join, per-cluster simulation, replication (Redis), single client view of the world. Scale = entity count + FPS.

---

## Repository split (future)

To release the library with versions and keep demos as standard, customer-facing projects, see **`docs/REPO_SPLIT_PLAN.md`**: library repo (arcane), Unreal plugin repo (arcane-client-unreal), demos repo (arcane-demos) with Unreal demo + backend demo + scripts.
