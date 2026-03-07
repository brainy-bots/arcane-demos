# Verification run summary (entity movement)

Generated from the timeline sequence: `wait:1,capture,wait:3,capture,wait:3,capture,wait:3,capture,wait:3,capture` (5 screenshots over ~13s).

## Log evidence

### 1. State updates are received
- **State update: total updates applied** increases over time: 1 → 16 → 54 → 91 → 127 → 164 → 200 → 237 → 274 → 310.
- So the client is receiving continuous full snapshots from the server (~20 Hz).

### 2. Entity positions change over time (MovementCheck)
First entity **server position** (server space) over game time:

| gameTime | first serverPos (x, y, z) |
|----------|---------------------------|
| 0.1      | (0.09, 0.00, 200.00)      |
| 2.1      | (0.43, 0.00, 200.00)      |
| 4.1      | (3.36, 0.00, 199.79)      |
| 6.1      | (6.22, 0.00, 197.48)      |
| 8.1      | (8.78, 0.00, 194.89)      |
| 10.2     | (11.88, 0.00, 192.90)     |
| 12.2     | (15.50, 0.00, 192.76)     |
| 14.2     | (18.37, 0.00, 194.99)     |
| 16.2     | (19.95, 0.00, 198.32)     |
| 18.2     | (20.42, 0.00, 200.00)     |

So the first entity’s position **moves** (e.g. x from ~0 to ~20 over 18s). The mid-entity position also changes between log lines.

### 3. Conclusion
- **Replication path:** Server → WebSocket → adapter cache → display snapshot is working; positions in the snapshot **change over time**.
- **Display:** The same snapshot is passed to `UpdateEntityVisuals` every tick and `SetEntityTransform(WorldPos, WorldVel)` is called per entity, so visuals should reflect the new positions.

If entities still appear stuck in a manual run, check:
- **bCenterEntitiesOnPlayer** is **false** (default) so the entity cloud is world-fixed.
- **GroundZ** matches your floor Z if entities look like they’re floating or underground.

## Axis fix (server → client) — "entities in a line" resolved

Earlier runs showed **first serverPos = (x, 0.00, z)** with the middle component always 0. The server was sending position as `(a.x, a.y, a.z)` (y = height 0..7), so the client mapped:
- Position.X → Unreal X (0..200 ✓)
- Position.Y → Unreal Y (**0..7** → narrow strip = "line")
- Position.Z → Unreal Z (we used as height; was actually horizontal 0..200)

**Fix (deployed):** Server now sends `(a.x, a.z, a.y)` so JSON `x,y,z` = horizontal1, horizontal2, vertical. Client uses Position.X/Y for horizontal (both 0..200) and Position.Z for height (0..~7). After restarting the cluster (so the new `arcane-cluster-demo` runs), the next verification run should show:
- Log line: `server X range … Y range … Z (height) …` with **X and Y both spanning ~0..200**.
- In-game: entities spread in 2D, not a line.

## Screenshots
- `screenshot_1.png` … `screenshot_5.png`: captures at ~1s, ~4s, ~7s, ~10s, ~13s. Camera is static; entity positions in world space change between frames, so comparing these can show movement (e.g. different character positions between 1 and 5).
