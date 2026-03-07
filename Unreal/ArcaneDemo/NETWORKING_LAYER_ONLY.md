# Networking Layer Only

**Principle:** The only custom part is **how we get entity state** (our Rust backend over WebSocket). Everything else—character class, movement component, animation, floor—uses **the same classes and behavior** as the normal player. We do not reimplement movement, animation, or floor logic.

**Library contract:** See `Plugins/ArcaneClient/LIBRARY_PLUG_AND_PLAY.md`. The Arcane Client plugin is a **plug-and-play networking layer**: the customer uses standard engine behavior and calls **ApplyEntityStateToActor** to feed our snapshot into the engine’s replicated-movement path. No custom character/AnimInstance/floor logic required.

## Current (wrong) approach

- Custom `SetReplicatedDisplayState`, `bIsReplicatedDisplay`, `ReplicatedDisplayVelocity`, custom `UArcaneDemoCharacterMovementComponent` (skip `CalcVelocity`), custom `UArcaneEntityAnimInstance`, manual `GroundZ` and floor trace, switching entity mesh to a different AnimInstance class.
- That reimplements or bypasses standard behavior.

## Target (right) approach

1. **Data source (only custom part)**  
   - Adapter/subsystem receives WebSocket snapshot from Rust → list of entity state `(id, position, velocity, …)`.
   - This stays. No change to how we **get** data.

2. **Data sink (engine standard)**  
   - Spawn the **same** character class as the player (no “entity” mode).
   - Use the **same** `UCharacterMovementComponent` (no subclass).
   - Use the **same** AnimBlueprint as the player (ABP_Unarmed or whatever the character uses).
   - **Feed our snapshot into the same path the engine uses for replicated movement** so the character and ABP see normal replicated state:
     - Either set the actor’s `ReplicatedMovement` (location, rotation, velocity) and call the same apply logic the engine uses when replication is received (e.g. what `OnRep_ReplicatedMovement` triggers),  
     - Or call the CharacterMovementComponent’s API that applies server movement state on simulated proxies (e.g. equivalent of “apply this replicated move”).
   - No custom floor logic: once we apply movement the same way as replication, the existing character/movement and collision handle floor.

3. **What we remove or stop using**  
   - No `bIsReplicatedDisplay`, no `ReplicatedDisplayVelocity`, no `SetReplicatedDisplayState` on the character.
   - No `UArcaneDemoCharacterMovementComponent` (use base `UCharacterMovementComponent`).
   - No `UArcaneEntityAnimInstance` for entities; entity mesh uses the **same** Anim class as the player.
   - No manual `GroundZ` / floor trace for entity placement; rely on engine behavior when we apply movement like replication.
   - Optional: keep `SetDisplayColor` for cluster tint if desired; that’s cosmetic, not movement/animation.

## Implementation steps

1. **Find engine API**  
   - In Engine source: how does `ACharacter` / `UCharacterMovementComponent` apply replicated movement on simulated proxies? (e.g. `ReplicatedMovement`, `OnRep_ReplicatedMovement`, or CMC’s apply function.)
   - Determine the minimal set of state to set (location, rotation, velocity, maybe movement mode) and the function(s) to call so the rest runs as for a normal replicated character.

2. **Single “network adapter” in front of the engine**  
   - In the place that currently calls `SetReplicatedDisplayState` (e.g. `ArcaneEntityDisplay::UpdateEntityVisuals`):
     - Convert snapshot entry to the format the engine expects (e.g. `FRepMovement` or whatever the apply path takes).
     - Call that engine apply path for the corresponding character actor (same class as player, no entity-specific logic on the character).
   - Remove all entity-specific behavior from the character class and from the movement/anim components.

3. **Entity characters = same class, same components**  
   - Spawn `AArcaneDemoCharacter` (or base `ACharacter`) with no “display” mode.
   - Do **not** override AnimInstance class for the mesh; use the character’s default (same as player).
   - Do **not** use a custom movement component for entities.
   - Only difference from the player: they are not possessed; their state is driven by our “replication substitute” that feeds the same path the engine uses for replicated movement.

4. **Display actor**  
   - `ArcaneEntityDisplay` (or equivalent) stays as the place that:
     - Subscribes to the adapter/snapshot (networking layer).
     - Spawns/destroys character actors as needed.
     - Each frame: for each entity, converts snapshot → engine format and calls the engine apply path.
   - No custom movement, animation, or floor logic inside the display; it only forwards data into the engine.

## Outcome

- **Networking layer:** Custom (Rust → WebSocket → snapshot → “apply as replicated movement”).
- **Everything else:** Same classes and same behavior as the player; no reimplementation of movement, animation, or floor.
