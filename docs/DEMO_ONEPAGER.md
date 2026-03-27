# Arcane Demo — One-Pager (how to run and show)

Same demo, same scene — you choose **how** the game gets its entity data. Use this to show people how it works with **Arcane**, with **default Unreal** replication, or with **SpacetimeDB**.

**Library:** Plug-and-play; same character class and Unreal standards. One plugin API applies server state; no custom entity types.

---

## 1. Show with Arcane (our library)

**Backend:** Rust manager + cluster; client joins via HTTP, then WebSocket.

1. **Start backend** (from repo root):
   ```powershell
   .\scripts\run_demo.ps1
   ```
   Two windows: manager (HTTP 8081) + cluster (WebSocket 8080). Leave them open.

2. **Start the game:** Open the project in Unreal Editor and press **Play** (Arcane mode is default). Or: `.\scripts\verification-loop\run_verification.ps1 -BuildAndRunGame -StartBackend -CloseAfter`.

3. **What you see:** Client joins via `GET http://127.0.0.1:8081/join`, connects to the cluster, receives state. HUD shows entity count. Humanoids (same mesh as player) move; one human player, rest simulated.

**Multi-cluster (optional):** `docker compose up -d` then `.\scripts\run_demo_multi.ps1`. Same game; client gets merged state from 3 clusters; entities colorized by cluster.

---

## 2. Show with default Unreal replication

**Backend:** None. Unreal listen server spawns replicated bots.

1. **Start the game** with Unreal networking: in Editor, set Game Mode so **Use Arcane Networking** is **unchecked** (and **Use SpacetimeDB** unchecked). Set **Num Unreal Replicated Bots** (e.g. 50). Press Play as **Listen Server** (not client).

   Or from command line (after building): launch the game with `-UseUnrealNetworking -ArcaneBenchmarkBots=50` (or another count).

2. **What you see:** Same map and character look; entities are Unreal replicated actors. Compare behavior and scale to Arcane mode.

---

## 3. Show with SpacetimeDB

**Backend:** Local SpacetimeDB server + our simulator; client subscribes to SpacetimeDB.

1. **Start SpacetimeDB** in another terminal (from repo root, ensure SpacetimeDB CLI is on PATH):
   ```powershell
   spacetime start
   ```
   Or `spacetime dev --yes --server-only` for non-interactive. Leave it running.

2. **Start the simulator:**
   ```powershell
   .\scripts\run_demo_spacetime.ps1
   ```
   One window: publishes the module, then runs the entity simulator. Leave it open.

3. **Start the game in SpacetimeDB mode:** In Editor, set Game Mode so **Use Arcane Networking** is **unchecked** and **Use SpacetimeDB Networking** is **checked**. Press Play.

   Or: `.\scripts\verification-loop\run_verification.ps1 -BuildAndRunGame -UseSpacetimeDB -StartBackend -CloseAfter` (starts simulator for you; SpacetimeDB server must already be running).

4. **What you see:** Same map and character look; entities come from SpacetimeDB subscription. (Full entity display requires SpacetimeDB Unreal plugin + generated bindings; otherwise stub shows connection only.)

---

## What to point out when presenting

- **Same demo, three backends:** Arcane (manager + cluster + Redis for multi), default Unreal (listen server), SpacetimeDB (local server + simulator). Switch mode and run again to compare.
- **Join / connect:** Arcane = HTTP join then WebSocket; Unreal = built-in; SpacetimeDB = SDK connect + subscribe.
- **Library:** One client code path for “apply state to actor”; only the data source (Arcane adapter, Unreal replication, or SpacetimeDB table) changes.

---

## Quick reference

| Show with     | Backend / prerequisite              | Run game |
|---------------|--------------------------------------|----------|
| **Arcane**    | `.\scripts\run_demo.ps1`             | Play in Editor (default), or `run_verification.ps1 -BuildAndRunGame -StartBackend -CloseAfter` |
| **Unreal**    | None (listen server)                 | Play as Listen Server with “Use Arcane Networking” off |
| **SpacetimeDB** | `spacetime start` then `.\scripts\run_demo_spacetime.ps1` | Play with “Use SpacetimeDB Networking” on, or `run_verification.ps1 -BuildAndRunGame -UseSpacetimeDB -StartBackend -CloseAfter` |

Log when synced (Arcane): `Arcane Demo ready: N entities visible, replication active.`
