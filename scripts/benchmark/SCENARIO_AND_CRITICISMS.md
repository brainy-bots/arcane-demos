# Scenario parameters (SpatialOS-style) and industry criticisms we address

We use the **setup and parameters** of the SpatialOS demos to define a comparable scenario for our own demo—without using their code or architecture. We also list **industry criticisms** of SpatialOS and how our approach addresses them.

---

## Scenario parameters to match (our own demo)

Derived from what SpatialOS demos and stress tests used publicly:

| Parameter | SpatialOS / cited use | We use (our demo) |
|-----------|------------------------|--------------------|
| **Entity count** | 200 (FPS starter), 1000 (Mavericks), 4000+ (Scavengers), 14k (stress test) | Configurable (e.g. 100, 200, 500, 1000); benchmark at same milestones (200, 1000) |
| **Visibility** | “Same place” = all entities visible to all (stress case) | All entities in one area, all replicated to client (no culling for the benchmark) |
| **Map** | Flat / open (FPS arena); Scavengers had real map + “slide, jump, dance” | Flat plane, third-person, simple movement (walk, jump, wander) — same class of stress |
| **Tick rate** | Often 20–60 Hz server tick | We use 20 Hz server tick; document so we’re comparable |
| **Movement** | Simple (walk, shoot in FPS; jump, dance, slide in Scavengers) | Walk, jump, path-like wander (no shooting); same “replication load” idea |
| **Metrics** | FPS, server load, “N players in one place” | Client FPS, server tick_ms, entity count; add bandwidth when we have it |

So our **“SpatialOS-style scenario”** is: **N entities (e.g. 200 or 1000), all in the same area and all visible, simple movement, 20 Hz server tick, flat map.** We run this in **our** demo and report the same metrics. No need to touch their code.

---

## Industry criticisms of SpatialOS (and how we address them)

Sources: Game Developer (“6 Reasons SpatialOS isn’t ready for VR”), GamesIndustry.biz, MassivelyOP, Gamedev.net, NWN, etc.

### 1. No local / fast iteration — “upload to cloud, 10–15 min per test”

**Criticism:** No way to run a shared SpatialOS instance locally across machines; every test required cloud upload and deployment (10–15 minutes). Killed iteration speed.

**How we address it:** Our stack runs **locally** out of the box: manager + cluster(s) on localhost, Redis optional for multi-cluster. No cloud required to develop or benchmark. Run `run_demo.ps1` and the game; iterate in seconds.

---

### 2. Lock-in and compatibility — “entity streaming doesn’t play well with existing engines”

**Criticism:** Their entity/streaming model didn’t fit existing VR and engine assumptions (static scenes, player loaded with scene). Required big rewrites to fit their model.

**How we address it:** We’re **plug-and-play**: we only replace the **networking layer**. Same character class, same AnimBlueprint, same movement and scene model as default Unreal. No “entity streaming” model to adopt; we feed state into the engine’s normal replicated-movement path (`ApplyEntityStateToActor`). Engine and content stay standard.

---

### 3. Churn and forced upgrades — “new version every ~2 months, only 2 supported, full rewrites”

**Criticism:** Very fast version cadence; only two versions supported; some upgrades required complete rewrites (e.g. Scala → C#). Forced upgrade every ~6 months.

**How we address it:** We’re a **library** with a small, stable API (connect, tick, snapshot, apply to actor). No “platform” version treadmill. Backend and client versions can be evolved without breaking the contract. No forced rewrites for version compliance.

---

### 4. Engine / vendor lag — “always behind on Unity (or engine) version”

**Criticism:** SpatialOS lagged Unity (and engine) releases; couldn’t use latest engine or store assets tied to newer versions.

**How we address it:** We’re engine-agnostic on the **protocol** side (WebSocket, JSON state). Unreal plugin is a thin adapter. We can track Unreal (or other engine) versions without being “the platform” that blocks upgrades. No single vendor controlling engine compatibility.

---

### 5. Bugs and long fix cycles — “critical bugs could take months to fix”

**Criticism:** Beta-level quality; critical or showstopper bugs could take months to get fixes from the platform vendor.

**How we address it:** **We own the stack.** Bugs are in our code and dependencies; we can fix and ship without waiting on a third-party platform. Open, auditable code path.

---

### 6. Cloud latency and cost — “infrastructure on Google servers, latency; $81M losses”

**Criticism:** Dependency on cloud infra added latency and cost; Improbable had large operating losses; single-shard “massive” MMOs were hard to make viable.

**How we address it:** **Run anywhere:** local, your own servers, or your cloud. No obligation to use a specific cloud or pay a platform fee. You control deployment and cost. Lower latency when you colocate or run on your own metal.

---

### 7. Licensing and ToS risk — “Unity revoked Improbable’s license; games in jeopardy”

**Criticism:** Unity changed ToS and revoked Improbable’s license; SpatialOS-based Unity games were suddenly at legal risk.

**How we address it:** We’re not a hosted platform that depends on a single engine vendor’s ToS. We’re a **library** you integrate; your game stays under your and the engine’s licenses. No “platform license” that a vendor can revoke.

---

### 8. Steep learning curve and complexity — “worker model, distributed simulation, hard to onboard”

**Criticism:** Worker model, spatial partitioning, and distributed simulation concepts made onboarding and iteration hard for many teams.

**How we address it:** **Simple mental model:** client connects, gets a snapshot of entity state each tick, applies it to actors. No workers, no spatial schema to learn for basic use. Optional multi-cluster and Redis for scale, but single-cluster “one server, many entities” is the default and is easy to run and reason about.

---

## Push the limit: how to run

We built the demo to match this scenario and **push the character limit** (200 → 500 → 1000+).

### Backend (Rust)

- **Entity cap:** Up to **2000** entities per cluster (`DEMO_ENTITIES`, e.g. `run_demo.ps1 -EntityCount 1000`).
- **Same-place stress (optional):** Set **`STRESS_RADIUS`** so entities stay in a small area (all visible).  
  Example: `.\scripts\run_demo.ps1 -EntityCount 1000 -StressRadius 500` — 1000 entities confined to radius 500 around center.

### Run the demo (manual)

```powershell
.\scripts\run_demo.ps1                          # 200 entities
.\scripts\run_demo.ps1 -EntityCount 500         # Push: 500
.\scripts\run_demo.ps1 -EntityCount 1000        # Push: 1000
.\scripts\run_demo.ps1 -EntityCount 1000 -StressRadius 500   # Same-place stress
```

Then start the Unreal client. Check FPS and server stats in the log.

### Benchmark (automated)

```powershell
.\scripts\benchmark\run_benchmark.ps1 -Mode Arcane
.\scripts\benchmark\run_benchmark.ps1 -Mode Arcane -EntityCounts 200,500,1000 -StressRadius 500
.\scripts\benchmark\run_benchmark.ps1 -EntityCounts 100,200,500,1000
```

Results: `scripts/benchmark/benchmark_results.csv` and `benchmark_results.png`. Unreal mode supports up to 2000 bots for comparison.

---

## Summary

- **Scenario:** We match the **setup and parameters** of SpatialOS-style demos (N entities, same place, simple movement, 20 Hz, flat map) in **our own demo** and report the same metrics. No need to use or study their architecture.
- **Criticisms:** We document the main **industry criticisms** of SpatialOS (local iteration, compatibility, churn, engine lag, bugs, latency/cost, licensing, complexity) and how our **design and delivery** address them. That gives a clear “we learned from what went wrong” story and differentiates us on workflow, simplicity, and control.
