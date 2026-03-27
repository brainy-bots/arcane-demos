# CA-02 — UnrealAdapter
**Unreal Engine 5 client adapter for Arcane Engine**

---

| | |
|---|---|
| **Component ID** | CA-02 |
| **Layer** | Client Adapter |
| **Type** | Implementation |
| **Implements** | CA-01 IClientAdapter |
| **Purpose** | Unreal Engine 5 implementation of the client adapter. Connects the Arcane server stack (ClusterManager, ClusterServer) to UE5; uses **Mass Entity** for high-density crowd rendering of replicated entities. This is the adapter used by the demo. |
| **Document version** | 1.0 |

---

## 1. Overview

UnrealAdapter is the only engine-specific code on the client side. It implements the full IClientAdapter contract (CA-01): Manager and Cluster WebSocket lifecycle, message serialization (JSON for Manager, msgpack for Cluster), entity state cache, tick-driven input submission and state delivery, handoff handling, and metrics overlay data. The engine sees no raw server protocol — only typed structs and events.

**Demo scope:** The demo targets Unreal Engine 5. This document specifies how the adapter is built (plugin or game module), how it maps server entity state into **Mass** (fragments, processors, rendering), and how threading and interpolation are handled so that hundreds of entities render smoothly at 20Hz state updates and 60+ fps.

**Key choices:**
- **Mass Entity** for entity representation and crowd rendering (not one Actor per entity).
- **C++** for the adapter core; optional Blueprint exposure for config and events.
- **Dedicated network thread** (or async) for WebSocket I/O; game thread only for `tick()`, cache updates, and event delivery.
- **Message format:** Manager = JSON; Cluster = msgpack (per CA-01). Adapter handles (de)serialization.

---

## 2. Responsibilities (mapping from CA-01)

- **Manager connection:** Open WebSocket to ClusterManager (`config.manager_url`), send PLAYER_JOIN, wait for CLUSTER_ASSIGN. Reconnect with backoff on disconnect. Send PLAYER_LEAVE on shutdown.
- **Cluster connection:** Open WebSocket to assigned ClusterServer (`server_host:server_port`). Send PLAYER_INPUT at 20Hz; receive STATE_UPDATE (and HANDOFF_ACCEPTED, RPC_RESULT, AOE_BROADCAST). On CLUSTER_REASSIGN, perform handoff: new connection, HANDOFF_CLAIM, close old within `deadline_ms`.
- **Entity state:** Maintain local entity cache (entity_id → EntitySnapshot). Merge STATE_UPDATE deltas into cache; apply `removed_entity_ids`. Expose `get_entity_snapshot()` for the engine. **Mass:** map EntitySnapshot into Mass fragments and update Mass entities each tick so the Mass renderer can draw the crowd.
- **Input:** Each tick, collect player position/velocity/action from the game, call `submit_input(PlayerInput)`. Adapter batches and sends at 20Hz on the Cluster connection.
- **Events:** Emit ON_CONNECTED, ON_DISCONNECTED, ON_CLUSTER_REASSIGNED, ON_CLUSTER_HANDOFF_COMPLETE, ON_CONNECTION_FAILED, and optional ON_ENTITY_ENTERED / ON_ENTITY_LEFT / ON_SYSTEM_MESSAGE. Deliver on game thread during `tick()`.
- **Metrics:** Implement `get_metrics()` (MetricsSnapshot). Supply data to the demo’s metrics overlay UI (current cluster, cluster count, RPC rate, handoff duration, etc.).
- **Interpolation:** STATE_UPDATE at 20Hz; render at 60+ fps. Interpolate entity positions (and optionally rotation/velocity) between state updates so movement is smooth.

---

## 3. What It Does NOT Do

- **Rendering** — Mass (or the game’s renderer) draws entities; the adapter only updates the data Mass reads.
- **Game logic or physics** — Movement, combat, and AI run on the server; the client displays state and sends input.
- **Authentication UI** — Adapter receives `auth_token` via config; it does not implement login flows.
- **SpacetimeDB or server clustering** — Client is unaware of ClusterManager/ClusterServer internals; it only follows the CA-01 protocol.

---

## 4. Implementation of CA-01 Contract

### 4.1 Lifecycle

| CA-01 method | Unreal implementation |
|--------------|------------------------|
| `initialize(config)` | Store config in UArcaneAdapterSubsystem or UArcaneAdapter (UObject). Validate URLs and IDs. Create WebSocket and message queues. No engine dependency in config struct. |
| `connect()` | Open Manager WebSocket (e.g. libwebsockets, IXWebSocket, or UE IWebSocket). Send PLAYER_JOIN (JSON). Block or async-wait for CLUSTER_ASSIGN; then open Cluster WebSocket, start 20Hz send/recv loops on network thread. |
| `disconnect(reason)` | Send PLAYER_LEAVE. Close both sockets. Clear entity cache. Broadcast ON_DISCONNECTED. |
| `tick(delta)` | Called from game thread (e.g. from a UWorld tick or subsystem). Drain inbound queue (STATE_UPDATE, RPC_RESULT, etc.), merge into entity cache, update Mass fragments. Flush outbound PLAYER_INPUT. Fire any pending events. **No blocking I/O.** |
| `submit_input(input)` | Push PlayerInput into outbound queue. Network thread serializes and sends at 20Hz. |
| `get_entity_snapshot()` | Return current entity cache as array (or iterator). Mass processor can read this or the cache can write directly into Mass. |
| `get_metrics()` | Return last MetricsSnapshot (from METRICS_UPDATE on Manager or from local counters). |

### 4.2 Event callbacks

Map CA-01 events to Unreal delegates or a simple callback interface so Blueprint or C++ game code can react:

- **ON_CONNECTED** → e.g. `FOnArcaneConnected`
- **ON_DISCONNECTED** → `FOnArcaneDisconnected(reason)`
- **ON_CLUSTER_REASSIGNED** → `FOnClusterReassigned(old_id, new_id, new_host)`
- **ON_CLUSTER_HANDOFF_COMPLETE** → `FOnHandoffComplete(new_cluster_id)`
- **ON_CONNECTION_FAILED** → `FOnConnectionFailed(reason, attempt_count)`
- Optional: ON_ENTITY_ENTERED, ON_ENTITY_LEFT, ON_SYSTEM_MESSAGE.

Events must be invoked on the **game thread** (e.g. enqueued from network thread and drained in `tick()`).

### 4.3 Message format

- **Manager:** JSON. Use UE JsonUtilities or a small JSON library. Messages: PLAYER_JOIN, PLAYER_LEAVE (out); CLUSTER_ASSIGN, CLUSTER_REASSIGN, SYSTEM_MESSAGE, METRICS_UPDATE (in).
- **Cluster:** msgpack. Use a C++ msgpack library (e.g. msgpack-c). Messages: PLAYER_INPUT (out); STATE_UPDATE, STATE_UPDATE_FULL, HANDOFF_ACCEPTED, RPC_RESULT, AOE_BROADCAST (in). Length-prefix or framing per CA-01 §7.

---

## 5. Unreal-Specific Architecture

### 5.1 Module / plugin layout

- **Option A — Game module:** Code lives in the game’s source (e.g. `Source/MyGame/Arcane/`). Depends on Core, Networking (or third-party WebSocket), and Mass.
- **Option B — Plugin:** Separate plugin (e.g. `Plugins/ArcaneClient/`) so it can be reused across projects. Same dependencies.

Recommended for demo: **Plugin** so the adapter is clearly separated and can be versioned with the Arcane doc set.

```
ArcaneClient/
  Source/
    ArcaneClient/
      Private/
        ArcaneAdapter.cpp
        ArcaneWebSocketManager.cpp
        ArcaneMessageCodec.cpp
        Mass/
          ArcaneMassProcessor.cpp
          ArcaneMassFragments.cpp
      Public/
        ArcaneAdapter.h
        ArcaneTypes.h       // EntitySnapshot, PlayerInput, MetricsSnapshot
        IArcaneAdapter.h    // CA-01-aligned interface
        Mass/
          ArcaneMassFragments.h
          ArcaneMassProcessor.h
  ArcaneClient.Build.cs
```

### 5.2 Threading

- **Game thread:** `tick(delta)`, `submit_input()`, `get_entity_snapshot()`, `get_metrics()`, event delivery. All CA-01 “main thread” semantics.
- **Network thread (or async):** WebSocket connect/read/write, serialization, enqueue decoded messages to inbound queue and outbound PLAYER_INPUT to send buffer. No UE API calls from this thread except thread-safe queue push.
- **Synchronization:** Lock-free queues (e.g. TQueue) or a single mutex-protected queue for inbound messages; outbound same. Keep critical sections short.

### 5.3 WebSocket and dependencies

- **WebSocket:** Use a library that supports wss and runs off the game thread (e.g. **libwebsockets**, **IXWebSocket**, or UE’s **IWebSocket** if available and thread-safe). Manager and Cluster each have one connection.
- **msgpack:** Link msgpack-c (or equivalent) for Cluster binary protocol. Manager uses JSON only.
- **UE modules:** Core, CoreUObject, Engine; NetCore/Networking if used; **MassEntity**, **MassGameplay** (or minimal Mass) for crowd.

---

## 6. Mass Entity Mapping

### 6.1 Role of Mass

Mass is used to represent **replicated entities** (players, NPCs, projectiles, etc.) as many small units without one Actor per entity. The adapter does not render; it **feeds data** into Mass. A Mass processor reads the entity cache (or fragments written by the adapter) and updates transform/visual state each tick.

### 6.2 Fragments (data)

Define fragments that mirror the minimal state needed for rendering and demo logic:

- **FArcaneEntityFragment:** entity_id, cluster_id, position, velocity, facing (rotation), animation_state, health, health_max, visual_tags (or cluster_color for demo). Maps from CA-01 `EntitySnapshot`.
- Optional: **FArcaneReplicatedTag** (tag for “this is Arcane-replicated”) so processors can filter.

Archetype: one that has transform + these fragments. Use **ISMC** (Instanced Static Mesh Component) or **Mass Representative** for rendering many instances.

### 6.3 Processor (logic)

- **UArcaneMassProcessor** (or similar): Runs each tick. For each entity in the adapter’s cache (or for each Mass entity with FArcaneEntityFragment), update fragment from latest EntitySnapshot. Apply **interpolation**: between last and current state over the 50ms window so 20Hz updates look smooth at 60 fps.
- **Interpolation:** Store previous position/rotation and timestamp; lerp based on `delta` and `last_updated` so that by the time the next STATE_UPDATE arrives, the entity has moved to the new position. Optionally use velocity for extrapolation when a tick is late.

### 6.4 Spawn / despawn

- **Entity entered:** When an entity appears in the cache (new entity_id in STATE_UPDATE), add a Mass entity with FArcaneEntityFragment. Optionally emit ON_ENTITY_ENTERED.
- **Entity left:** When entity_id is in `removed_entity_ids`, destroy the Mass entity. Optionally emit ON_ENTITY_LEFT.

Adapter can own the Mass subsystem or the game can; the adapter must provide the list of current entities and their state so that either the adapter or a processor can create/destroy Mass entities.

---

## 7. Configuration

| Config key | Type | Default | Description |
|------------|------|---------|-------------|
| Manager URL | FString | `ws://127.0.0.1:8081` | ClusterManager WebSocket URL |
| Player ID | FString (UUID) | — | Set at login or from game instance |
| Auth token | FString | — | Session token |
| Reconnect interval ms | int | 1000 | Delay between reconnect attempts |
| Max reconnect attempts | int | 10 | Before ON_CONNECTION_FAILED |
| Handoff deadline ms | int | 200 | Max time to complete cluster handoff |
| State update interpolation | bool | true | Smooth position between 20Hz updates |

Expose via **Project Settings** (custom category “Arcane”) or a config file (e.g. DefaultArcane.ini). Blueprint-readable for demo UI.

---

## 8. Metrics Overlay (demo)

- **Data source:** `get_metrics()` returns MetricsSnapshot (cluster_id, cluster count, RPC rate, merge/split counts, latency, etc.). Refresh from METRICS_UPDATE or local aggregation.
- **UI:** Demo overlay widget (UMG) reads MetricsSnapshot each frame or on timer and displays: current cluster, total clusters, players online, connection latency, last handoff duration. No adapter logic for layout — only data.

---

## 9. Validation Checklist (CA-01 + Unreal)

An implementation is complete when:

- Connects to ClusterManager and receives CLUSTER_ASSIGN within 5 seconds.
- Receives STATE_UPDATE at ~20Hz for 60+ seconds without message loss.
- Completes a merge handoff within `deadline_ms` (e.g. 200 ms).
- Cluster color coding correct for multi-cluster scenario (Mass entities show correct cluster color).
- `get_metrics()` matches server-side Prometheus (cluster count, etc.).
- `tick()` completes in under 2 ms with 500 entities in cache (game thread).
- Survives Manager disconnect and reconnects without crash.
- **Unreal-specific:** No crash or hitch when opening/closing Cluster connection during handoff. Mass entities spawn/despawn correctly when entities enter/leave observation set. Interpolation is smooth at 60 fps.

---

## 10. Dependencies Summary

| Dependency | Purpose |
|------------|---------|
| CA-01 IClientAdapter | Full contract (lifecycle, messages, events, metrics). |
| Unreal Engine 5 | Core, Engine, Mass (MassEntity, MassGameplay or minimal Mass). |
| WebSocket library | Manager + Cluster connections (libwebsockets, IXWebSocket, or UE IWebSocket). |
| msgpack-c (or similar) | Cluster connection binary protocol. |
| JSON library | Manager connection (UE JsonUtilities or third-party). |

---

## 11. Open Questions / Demo Scope

- **Exact WebSocket choice:** To be decided in implementation (platform support, packaging, threading).
- **Mass LOD:** For very large crowds, LOD or culling by distance can be added above the adapter; adapter still provides full snapshot.
- **Blueprint exposure:** Which methods/events are exposed to Blueprint for the demo (e.g. connect, disconnect, get_metrics, event bindings).
- **Platform:** Demo initially Windows; adapter should avoid platform-specific code so console/mobile can be added later.

---

## 12. References

- **CA-01 IClientAdapter** — `ca_01_iclientadapter.md` (contract, message protocol, events, validation).
- **00_component_index.md** — Simulation vs authoritative world state; client adapter layer.
- **Unreal Mass:** UE5 Mass Entity documentation (fragments, processors, ISM, representatives).

---

*Arcane Engine — CA-02 UnrealAdapter — Confidential*
