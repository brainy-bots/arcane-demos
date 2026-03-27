# Demo to-do: from here on

What we still need to do so the demo **proves the library capabilities**. Then a separate phase for making it look better and adding gameplay.

**Current focus:** Make it work and find the limits first — get Arcane, Unreal, and SpacetimeDB modes running reliably and discover where each hits its limit (entity count, playability). Presentation (e.g. a video showing how it works plus a chart) comes later; we are not storing recorded benchmark results for now.

**Goal (DEMO_GOAL.md):** Show join, multi-cluster, replication, and client the way a production MMO would; scale toward 200–400+ visible entities.

**Next steps for progress:**
- **Phase 1b:** Run `.\scripts\benchmark\run_benchmark.ps1 -Mode Unreal` then `-Mode Arcane` (or `-Mode Both`). Note FPS/entity counts where each becomes unplayable; document in this file or DEMO_GOAL.
- **Phase 1c:** Run SpacetimeDB once: `spacetime start` → `.\scripts\run_demo_spacetime.ps1` → launch game with Use SpacetimeDB Networking on (or `run_verification.ps1 -BuildAndRunGame -CloseAfter -UseSpacetimeDB -StartBackend`). Then run benchmark with `-Mode SpacetimeDB` to find its limit.

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

## Phase 1b: Find limits (Arcane vs Unreal)

Make both modes run; push entity count until you find where each becomes unplayable or breaks. No stored results for now — we use this to know the limits. Later: optional video + chart for presentation.

| # | Step | Status |
|---|------|--------|
| 1 | **Run and find Unreal limit** | ⬜ | Run game in Unreal mode at 20, 50, 100, 150, 200+ bots (Game Mode: Use Arcane off; Listen Server). Note where FPS or stability drops. |
| 2 | **Run and find Arcane limit** | ⬜ | Run game in Arcane mode with `run_demo.ps1` at increasing `-EntityCount`. Note where it hits the limit. |
| 3 | **Compare** | ⬜ | Same machine, same scene — where does each mode max out? (Chart/video for presentation later, not stored CSV.) |

**Phase 1b complete** when we know the limits for both; presentation (video + chart) is a later step.

---

## Phase 1c: SpacetimeDB — make it work, find its limit

Same demo with SpacetimeDB as the data source; get it running and find where it hits its limit. Presentation (video + chart) later.

| # | Step | Status |
|---|------|--------|
| 1 | **SpacetimeDB module + simulator** | ✅ | Module and `run_demo_spacetime.ps1` in place. Publish to local `spacetime start`. |
| 2 | **Unreal SpacetimeDB mode** | ✅ | Full display: SpacetimeDB plugin + bindings in `ModuleBindings/`; `SpacetimeDBEntityDisplay` subscribes to `Entity`, applies to actors. Codegen from `spacetimedb_demo/spacetimedb`; run `spacetime generate -l unrealcpp` when module schema changes. |
| 3 | **Find SpacetimeDB limit** | ⬜ | Run at increasing entity count; note where it becomes unplayable or breaks. Compare to Arcane/Unreal limits (for presentation later, e.g. video + chart). |

**Verify SpacetimeDB end-to-end:** In one terminal run `spacetime start`. In another: `.\scripts\run_demo_spacetime.ps1`. Then run the game with "Use SpacetimeDB Networking" on, or: `.\scripts\verification-loop\run_verification.ps1 -BuildAndRunGame -CloseAfter -UseSpacetimeDB -StartBackend`.

**SpacetimeDB-only setup + finding limits (e.g. 64 GB / 16 cores):** See **`docs/SPACETIMEDB_ONLY_SETUP.md`**. Run `spacetime start` then: `.\scripts\benchmark\run_benchmark.ps1 -Mode SpacetimeDB -SpacetimeDBLimitFinding` to benchmark at 100, 200, 500, 1000, 2000, 5000 entities and get CSV + chart.

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
- **Ad-hoc numbers (optional):** `.\scripts\benchmark\run_benchmark.ps1` can produce CSV/chart; we are not storing results for now — focus is make it work and find limits; presentation (video + chart) later.

---

## How to present the demo (one-pager)

1. **Start backend:** `.\scripts\run_demo.ps1` (single) or Redis + `.\scripts\run_demo_multi.ps1` (multi). Leave windows open.
2. **Start the game;** client joins via `GET http://127.0.0.1:8081/join`, gets cluster, receives replicated state.
3. **Point out:** HUD shows Entities + FPS. Humanoids same mesh as player, path-like movement. Multi-cluster: color by cluster = ownership.

**Proves:** Central join, per-cluster simulation, replication (Redis), single client view of the world. Scale = entity count + FPS.

---

## Repository split (future)

To release the library with versions and keep demos as standard, customer-facing projects, see **`docs/REPO_SPLIT_PLAN.md`**: library repo (arcane), Unreal plugin repo (arcane-client-unreal), demos repo (arcane-demos) with Unreal demo + backend demo + scripts.
