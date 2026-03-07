# Arcane Client: Plug-and-Play Library

**We are building a library.** If the customer has to reimplement movement, animation, floor logic, or fight the engine from scratch, they will not use our library.

## Contract

- **The library is only a new networking layer.** It replaces *where* entity state comes from (our backend instead of Unreal replication). It does **not** replace how the engine applies that state.
- **All standard engine behavior stays.** The customer uses the same character class, same AnimBlueprint, same movement component, same collision/floor as they would for a normal replicated character. No custom “entity” mode, no custom AnimInstance, no manual GroundZ.
- **Plug-and-play:** Customer adds our plugin, connects to our backend, and feeds our entity state into the engine’s **standard** path (the same path used for replicated movement). Everything else works the way the engine already works.

## What the library provides (networking only)

- **Connection:** `Connect()`, `Tick()`, `GetEntitySnapshot()`, `GetInterpolatedEntitySnapshot()`, `SendPlayerState()`, etc. — how to get entity state from our backend. *(Already exists.)*
- **Apply to engine:** A single API that takes **entity state + an actor** and applies that state using the **engine’s standard replicated-movement path** (e.g. `FRepMovement` + the same apply logic the engine uses when replication is received). So the actor (any `ACharacter` or actor the customer chooses) behaves exactly like a simulated proxy — same movement, same animation, same floor — with no custom code on the character.

## What the customer does (minimal)

1. Add the Arcane Client plugin to their project.
2. Connect to the backend (`Initialize`, `Connect`), call `Tick` each frame, and get the snapshot (`GetEntitySnapshot` or `GetInterpolatedEntitySnapshot`).
3. For each entity, they have an actor (e.g. their normal character class, same as the player). They call **one library function**: e.g.  
   `Arcane->ApplyEntityStateToActor(Actor, State, WorldOrigin, PositionScale)`  
   (or equivalent: “apply this entity state to this actor using the engine’s standard path”).
4. Nothing else. No custom character subclass for “display”, no custom movement component, no custom AnimInstance, no floor traces, no slot setup. The actor is the same class they use for the player or for any other replicated character; the only difference is the state is driven by our snapshot instead of by Unreal’s replication.

## Library API addition (to implement)

- **ApplyEntityStateToActor(AActor* Actor, const FArcaneEntityState& State, FVector WorldOrigin, float PositionScale)** *(implemented)*  
  - Converts `State` (server space) to world position and velocity.  
  - Builds the engine’s replicated-movement struct (`FRepMovement`: Location, Rotation, LinearVelocity).  
  - Applies it to `Actor` via the **same code path** the engine uses when it receives replicated movement (`SetReplicatedMovement` + `OnRep_ReplicatedMovement`).  
  - So the actor’s movement component and AnimBlueprint receive state the same way as a simulated proxy; movement, animation, and floor work with no custom logic.  
  - **Requirement:** Entity actors must have `SetReplicatingMovement(true)` (e.g. in BeginPlay) so the engine’s apply path runs; the actor does not need to be on a network channel.

Optional helpers:

- **WorldPositionFromState**, **WorldVelocityFromState** (or a single struct) if the customer wants to do their own spawn/placement but still use our “apply to actor” for the actual movement update.

## What we remove from the demo (and from the library)

- No API that requires the customer to use a custom character class, custom movement component, or custom AnimInstance.
- No “entity display state”, “replicated display velocity”, or manual position/velocity setting in character code. The **library** applies state via the engine path; the character code stays standard.
- No GroundZ / floor trace in the library or in the demo’s “display” logic; the engine’s normal behavior handles floor once we apply movement the standard way.

## Outcome

- **Customer:** Uses standard engine patterns. Only adds our networking (connect, tick, snapshot) and one “apply state to actor” call. No redoing anything from scratch.
- **Library:** Only implements the new networking layer and the thin adapter that pushes our state into the engine’s existing replicated-movement path.
