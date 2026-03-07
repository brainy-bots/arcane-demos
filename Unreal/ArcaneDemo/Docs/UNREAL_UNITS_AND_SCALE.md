# Unreal Engine units and Arcane position scale

Reference so we don’t mix up scales and end up with entities 20 km away.

## Unreal Engine (official)

- **1 Unreal Unit (UU) = 1 centimeter (cm)** by default (UE4/UE5).
- **100 UU = 1 meter.** 10,000 UU = 100 m. 100,000 UU = 1 km.
- **Coordinate system:** Left-handed, **Z-up** (X = forward, Y = right, Z = up).
- **World to Meters** (World Settings): default `100` → 100 UU = 1 m (i.e. 1 UU = 1 cm). You can change this per project; the above is the usual default.

Sources: [Units of Measurement in Unreal Engine](https://dev.epicgames.com/documentation/en-us/unreal-engine/units-of-measurement-in-unreal-engine), [Coordinate System and Spaces](https://dev.epicgames.com/documentation/en-us/unreal-engine/coordinate-system-and-spaces-in-unreal-engine).

## Our server ↔ Unreal mapping

- **Server** sends position in “server units” (demo world **0–200** in X and Z, Y = height).
- **Arcane Entity Display** converts with **Position Scale**:  
  `UnrealPosition = (PositionScale * server_x, PositionScale * server_z, GroundZ + PositionScale * server_y)`  
  (We use Unreal X = server X, Unreal Y = server Z, Unreal Z = server Y so that server “up” is Unreal Z.)

So **Position Scale** is “server units → Unreal units”:

| Position Scale | 200 server units in Unreal | In real-world terms (1 UU = 1 cm) |
|----------------|---------------------------|-----------------------------------|
| **1**          | 200 UU                    | 2 m                               |
| **10**         | 2,000 UU                  | 20 m                              |
| **100**        | 20,000 UU                 | 200 m                             |

- For a **small play area** (a few meters): use **Scale = 1** (server 0–200 → 0–200 UU = 0–2 m).
- For a **medium area** (tens of meters): use **Scale = 10** (0–20 m).
- **Scale = 100** makes the demo world 200 m; with “Center entities on player” we cap to 10 so the cloud stays within ~20 m and visible.

## Sending player position to the server

The adapter sends Unreal position/velocity to the server after dividing by **SendPositionScale** (default 1; keep it equal to Position Scale so 1 server unit = 1 UU = 1 cm). If you use a different Position Scale (e.g. 10 or 100), set SendPositionScale to the same.

## Checklist to avoid scale mistakes

1. Decide how big the “server world” should be in Unreal (e.g. 2 m vs 20 m vs 200 m).
2. Set **Position Scale** accordingly: 1 → ~2 m, 10 → ~20 m, 100 → ~200 m.
3. If **Center entities on player** is on, we cap effective scale to 10 so the cloud never goes beyond ~20 m; no need to change that unless you intentionally want a larger centered area.
4. **GroundZ**: set to your floor height in Unreal (Z of the ground plane) so entities sit on the floor.
