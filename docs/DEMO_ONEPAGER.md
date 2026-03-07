# Arcane Demo — One-Pager (how to run and present)

**What it proves:** Central join, per-cluster simulation, Redis replication, single client view of the world. Scale = entity count + FPS. Library is plug-and-play: same character class and Unreal standards as a normal replicated game.

---

## Run the demo (single-cluster, 200 entities)

1. **Start backend** (from repo root):
   ```powershell
   .\scripts\run_demo.ps1
   ```
   Two windows: manager (HTTP 8081) + cluster (WebSocket 8080). Leave them open.

2. **Start the game:**
   - Open `Unreal/ArcaneDemo/ArcaneDemo.uproject` in Unreal Editor and press **Play**, or
   - From repo root: `.\scripts\verification-loop\run_verification.ps1 -BuildAndRunGame -CloseAfter` (builds, runs, captures, then closes).

3. **What you see:** Client joins via `GET http://127.0.0.1:8081/join`, connects to the cluster, receives replicated state. HUD shows **Entities: 200+ | FPS: …**. Humanoids (same mesh as player) move in path-like ways. One human player can move; others are simulated.

---

## Run the demo (multi-cluster, 150 entities)

1. **Start Redis** (from repo root):
   ```powershell
   docker compose up -d
   ```

2. **Start backend:**
   ```powershell
   .\scripts\run_demo_multi.ps1
   ```
   Four windows: manager + 3 clusters (50 entities each). Leave them open.

3. **Start the game:** Same as above (Editor Play or `run_verification.ps1 -BuildAndRunGame -CloseAfter`). Or one command: `.\scripts\verification-loop\run_verification_multi.ps1` (starts multi-cluster, then runs verification).

4. **What you see:** Client is assigned to one cluster; it receives merged state from all 3. HUD shows entity count + FPS. When the snapshot has distinct cluster IDs, entities are colorized by cluster (red/green/blue = ownership).

---

## What to point out when presenting

- **Join flow:** Client hits manager `GET /join`, gets cluster address, connects via WebSocket.
- **Replication:** All entity state (position, velocity) streams from cluster(s); Redis syncs state across clusters in multi-cluster mode.
- **Scale:** Single-cluster ~200 entities at ~40 FPS; multi-cluster ~150+ entities at ~20–26 FPS (depends on hardware).
- **Library:** Unreal client uses the same character class as the player; one plugin API (`ApplyEntityStateToActor`) applies server state. No custom entity types required.

---

## Quick reference

| Mode           | Backend                          | Verification (one command) |
|----------------|----------------------------------|-----------------------------|
| Single-cluster | `.\scripts\run_demo.ps1`         | `.\scripts\verification-loop\run_verification.ps1 -BuildAndRunGame -StartBackend -CloseAfter` |
| Multi-cluster  | `docker compose up -d` then `.\scripts\run_demo_multi.ps1` | `.\scripts\verification-loop\run_verification_multi.ps1` |

Log line when synced: `Arcane Demo ready: N entities visible, replication active.`
