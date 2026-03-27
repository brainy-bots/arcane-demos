# Demo goal: Library capability showcase (production-style MMO usage)

**Primary goal:** Show that the **Arcane networking library is better than the baseline** (default Unreal replication). We prove that by **pushing the limit of characters** and **comparing server/client performance** with data and a **chart**: same demo, same character count steps (e.g. 20, 50, 100, 150, 200 entities), Arcane vs Unreal mode — measure FPS and document where each hits its limit. Without that comparison, we have not proved anything.

**Library first, plug-and-play:** The demo must follow **Unreal standards** and use the library as a customer would: same character class as the player, same engine movement/animation path, no custom "entity" types required by the library. The plugin exposes a thin API (Connect, Tick, GetEntitySnapshot, ApplyEntityStateToActor); the demo is minimal game code on top of that.

**Simulate production usage:** The demo is structured so that every piece (manager, clusters, Redis, client plugin) uses the **library’s real APIs and contracts**—no shortcuts. Game logic (demo agents, Unreal visuals) stays in demo/game code; infrastructure (assignment, replication, WebSocket protocol) is the library. That’s how an MMO would integrate: adopt the library, plug in game logic and assets.

---

## Maverick-style scale target

**Concept (from SpatialOS Maverick):** Many simulated characters in a shared world, replicated across servers, visible to all clients—battle royale / MMO style.

**Our target:** Same idea on the Arcane stack, with a **higher character limit** than Maverick (they aimed 200–400, up to 1000). We build toward that progressively.

**Current stack:** Manager (round-robin join) → clusters (WebSocket + Redis replication) → demo agents (gravity, jump, wander) → Unreal client sees all entities, colorized by cluster. One human player can join and move; others are simulated.

---

## What the demo showcases (library capabilities)

| Piece | Library capability demonstrated | Production MMO usage |
|-------|---------------------------------|------------------------|
| **Manager** `GET /join` | Central assignment; client gets a cluster to connect to. | Same: players hit a lobby/join endpoint and receive server address. |
| **Cluster WebSocket** | Per-cluster simulation; broadcast of `EntityStateDelta`; accept `PLAYER_STATE` from clients. | Same: each shard/server runs simulation and streams state to connected clients; client sends input/position. |
| **Redis replication** | Cluster-to-cluster state sync; neighbors see each other’s entities. | Same: replication layer so nearby shards have consistent view for handoff and visibility. |
| **Entity state** (`entity_id`, `cluster_id`, position, velocity) | Single schema for simulation state; `cluster_id` = ownership for display and authority. | Same: one entity format; game uses it for rendering and (later) authority handoff. |
| **Unreal ArcaneClient** | Adapter: HTTP join, WebSocket to cluster, entity cache, send player state. | Same: any engine implements the same contract (join → connect → receive state, send input). |
| **Multi-cluster + color** | Multiple clusters; client sees all entities; color by `cluster_id` shows which server owns which entity. | Same: proves multi-server and ownership are visible; later = handoff and load balance. |

The demo does **not** implement a full game—it implements the **network and replication path** a full game would use, with minimal game logic so we can push entity count and prove the library.

---

## Progressive steps (not all at once)

1. **Align visuals** – Replicated entities use the same character mesh/anim as the player so “many players” look right. **Done:** same mesh, default materials, path-like movement.
2. **Scale and measure** – Raise `DEMO_ENTITIES` per cluster (and clusters if needed), run, and measure FPS/limits in Unreal and on the server. **Done:** log reports entity count + FPS every 2s; one-time "Arcane Demo ready: N entities visible" when synced; single-cluster 200 and multi-cluster 150 documented (see Scale tested below).
3. **Authority and handoff** – When an entity moves across cluster boundaries, ownership can switch (already reflected in `cluster_id` and color).
4. **Persistence and world** – Optional: SpacetimeDB or similar for authoritative world state; demo can stay position-only at first.
5. **Push the limit** – Tune (culling, LOD, tick rate, batching) until we exceed the Maverick character count in a single view.

Start with (1), then (2), and iterate.

---

## Demo finished (step 1) — what you have

- **Manager + cluster:** `.\scripts\run_demo.ps1` (200 entities single-cluster; multi-cluster via `run_demo_multi.ps1`).
- **Unreal client:** Join, WebSocket, entity snapshot; humanoids same mesh/anim as player; color by cluster; path-like movement.
- **Verification:** `.\scripts\verification-loop\run_verification.ps1 -BuildAndRunGame -StartBackend -CloseAfter` builds, runs game, captures screenshots and log. Log contains "Arcane Demo ready: N entities visible" when replication is active.
- **Scale tested:**
  - **Single-cluster 200:** Tested: 200 entities at ~40 FPS (HUD may show 201 including player). Run: `.\scripts\run_demo.ps1` then `.\scripts\verification-loop\run_verification.ps1 -BuildAndRunGame -CloseAfter`.
  - **Multi-cluster 150:** Redis + run_demo_multi.ps1 (50×3). **Tested:** `run_verification_multi.ps1` passes; client sees 207 entities (150 demo + player/replication) at ~20–26 FPS. HUD shows "Entities: N | FPS"; "Clusters: 3" appears when snapshot contains distinct cluster IDs from replication.

**Presenting the demo:** See **`DEMO_ONEPAGER.md`** for how to run and show the demo with Arcane, default Unreal, or SpacetimeDB — same scene, switch the data source so people can see how each works. Optional: `.\scripts\benchmark\run_benchmark.ps1` for numeric comparison if needed. **Concrete to-do:** See **`DEMO_TODO.md`**. **Splitting the repo** (library vs demos, versioning): **`docs/REPO_SPLIT_PLAN.md`**.
