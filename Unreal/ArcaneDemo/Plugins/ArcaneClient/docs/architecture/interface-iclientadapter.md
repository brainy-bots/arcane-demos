# CA-01 — IClientAdapter
**Client Adapter Interface — Engine-Agnostic Contract**

---

| | |
|---|---|
| **Component ID** | CA-01 |
| **Layer** | Client Adapter |
| **Type** | Interface — no implementation, only contract |
| **Purpose** | Define the contract every engine adapter must implement. The server infrastructure has no knowledge of any engine. This interface is the only coupling point between them. |
| **Implementations** | CA-02 UnrealAdapter · CA-03 GodotAdapter · CA-04 UnityAdapter |
| **Depends On** | None — this is the root interface |
| **Required By** | All engine adapter implementations (CA-02, CA-03, CA-04) |

---

## 1. Overview

IClientAdapter is the complete boundary between the Arcane Engine server infrastructure and any game engine. The server stack — ClusterManager, ClusterServer, RPCHandler, ReplicationChannelManager — produces a structured stream of entity state updates, cluster assignment events, and connection lifecycle signals. IClientAdapter consumes that stream and translates it into whatever representation the engine uses internally.

The interface is intentionally minimal. It does not prescribe how the engine renders entities, runs animations, handles input, or manages scenes. It only specifies the data that flows across the boundary and the lifecycle events that must be handled. Everything engine-specific lives in the adapter implementation, invisible to the server.

> **Invariant:** No server component ever imports, links against, or references any engine SDK. If a server component needs to know anything about the engine, that is a design error. Fix it by moving the knowledge into the adapter.

---

## 2. Responsibilities

An IClientAdapter implementation is responsible for exactly these things and nothing else.

- Manage the WebSocket connection lifecycle to the ClusterManager — connect, authenticate, disconnect, reconnect on failure
- Manage the WebSocket connection lifecycle to the assigned ClusterServer — connect on assignment, reconnect on merge/split signal
- Receive and deserialize all inbound messages from the server — entity state updates, cluster assignment, neighbor broadcast, system events
- Deliver deserialized entity state to the engine's entity representation system each tick
- Capture and serialize player input each tick and transmit to the assigned ClusterServer
- Handle seamless cluster server handoff — disconnect from old server, connect to new server within the required latency window on merge/split
- Expose a metrics overlay data source — current cluster ID, active cluster count, per-cluster CPU, cross-cluster RPC rate
- Report connection health and error states to the engine for user-facing feedback

---

## 3. What It Does NOT Do

- **Render entities** — that is the engine's job
- **Run game logic or physics** — that is the ClusterServer's job
- **Make clustering or merge decisions** — that is the ClusterManager's job
- **Store persistent player data** — that is SpacetimeDB's job
- **Validate player input for cheating** — that is the InputValidationLayer's job on the server
- **Handle authentication or session management** — that is a separate auth service outside this document's scope

---

## 4. Connection Model

An adapter maintains two persistent connections simultaneously during normal operation.

| Connection | Endpoint | Protocol | Purpose |
|---|---|---|---|
| **Manager** | ClusterManager :8081 | WebSocket (JSON) | Player join/leave, cluster assignment, merge/split signals, system events |
| **Cluster** | ClusterServer :8080 (assigned) | WebSocket (msgpack) | Entity state stream inbound, player input outbound, 20Hz each direction |

> The Manager connection is long-lived for the session. The Cluster connection changes on every merge and split. The adapter must handle Cluster connection replacement without dropping frames or producing visible discontinuity.

### Handoff Sequence (Merge or Split)

```
1. ClusterManager sends CLUSTER_REASSIGN via Manager connection
   { type: CLUSTER_REASSIGN, new_server_host, new_server_port, handoff_token, deadline_ms }

2. Adapter opens NEW Cluster connection to new_server_host:new_server_port
   Sends HANDOFF_CLAIM { handoff_token, player_id }

3. New Cluster server acknowledges: HANDOFF_ACCEPTED

4. Adapter closes OLD Cluster connection
   Total time from CLUSTER_REASSIGN to old connection closed: < deadline_ms (configurable; default 200ms; can be relaxed for high-latency clients)

5. Adapter continues normal operation on new connection
   No frame skip. No visible discontinuity.
```

**Handoff semantics:** Handoff requires no data commits. One server stops writing for that entity; the other reads current state from SpacetimeDB and starts writing. The new server picks up the entity as soon as it reads the updated ownership. The client must be robust: if for any reason it has no server assigned (e.g. missed deadline, connection issues), it must not crash — it waits and requests assignment ("I don't have an owner"); the Manager assigns immediately. Any state missed during the transition is recovered by full sync (gap recovery) when the client connects. The handoff deadline (`deadline_ms`) is configurable and can be relaxed; tune via testing. An optional handshake (new server confirms before owning) is possible but not required; the new server can simply read state and start.

---

## 5. Interface Definition

Defined in language-agnostic pseudo-code. Each adapter implementation maps these to the target language's idioms. Method names must match exactly — they are referenced by ID in this document series.

### 5.1 Lifecycle Methods

---

#### `initialize(config: AdapterConfig) -> Result`
**Returns:** `Result` (ok | error with reason)

Called once at startup. Establishes configuration, validates required fields, prepares connection pool. Does NOT open network connections.

| Parameter | Type | Description |
|---|---|---|
| `config.manager_url` | string | `ws://host:8081` — ClusterManager WebSocket URL |
| `config.player_id` | UUID | Stable player identifier for this session |
| `config.auth_token` | string | Session token from auth service |
| `config.reconnect_interval_ms` | int | Milliseconds between reconnect attempts (default 1000) |
| `config.max_reconnect_attempts` | int | Max reconnects before emitting CONNECTION_FAILED (default 10) |

---

#### `connect() -> Result`
**Returns:** `Result` (ok | error with reason)

Opens Manager connection. Sends PLAYER_JOIN. Blocks until cluster assignment received or timeout.

---

#### `disconnect(reason: DisconnectReason)`
**Returns:** void

Sends PLAYER_LEAVE to ClusterManager. Closes both connections gracefully. Emits ON_DISCONNECTED event.

| Parameter | Type | Description |
|---|---|---|
| `reason` | DisconnectReason | `GRACEFUL \| TIMEOUT \| ERROR \| KICKED` |

---

### 5.2 Tick Methods

Called every engine frame. Must complete within the engine's frame budget (typically 16ms at 60Hz). No blocking I/O inside tick methods.

---

#### `tick(delta: float) -> void`
**Returns:** void

Main per-frame update. Drains the inbound message queue, processes all pending entity state updates, submits player input to the outbound queue. Must be called from the engine's main game thread.

| Parameter | Type | Description |
|---|---|---|
| `delta` | float | Elapsed seconds since last tick |

---

#### `submit_input(input: PlayerInput) -> void`
**Returns:** void

Enqueues player input for transmission to ClusterServer. Non-blocking — input is batched and sent on the next network flush (every 50ms). Safe to call multiple times per frame.

| Parameter | Type | Description |
|---|---|---|
| `input.position` | Vector3 | Current player world position |
| `input.velocity` | Vector3 | Current player velocity vector |
| `input.action` | ActionType | `IDLE \| MOVE \| CAST \| ATTACK` |
| `input.action_data` | bytes? | Optional serialized action payload (spell params, target ID, etc.) |
| `input.timestamp` | float | Client timestamp for latency compensation |

---

### 5.3 Entity State Access

#### `get_entity_snapshot() -> EntitySnapshot[]`
**Returns:** Array of EntitySnapshot — all entities currently visible to this player

Returns the latest known state of all entities in the local cluster plus all entities received via cross-cluster replication from neighboring clusters. Called by the engine's rendering system each frame.

```
EntitySnapshot {
  entity_id:       UUID
  entity_type:     PLAYER | NPC | BOSS | PROJECTILE | AOE_FIELD
  cluster_id:      UUID          // which cluster owns this entity
  position:        Vector3
  velocity:        Vector3
  facing:          Quaternion
  animation_state: AnimState     // IDLE | WALK | RUN | CAST | ATTACK | DEATH
  health:          int           // 0-100
  health_max:      int
  visual_tags:     string[]      // e.g. ['guild_A', 'enemy', 'casting']
  last_updated:    float         // server timestamp of last update
}
```

---

### 5.4 Metrics Overlay Data

#### `get_metrics() -> MetricsSnapshot`
**Returns:** MetricsSnapshot — current system metrics for demo overlay display

Returns a snapshot of infrastructure metrics for display in the real-time overlay panel. Data is refreshed from Prometheus every 2 seconds. Safe to call any time after `connect()`.

```
MetricsSnapshot {
  local_cluster_id:            UUID
  local_cluster_cpu_pct:       float
  local_cluster_player_count:  int
  total_active_clusters:       int
  total_players_online:        int
  cross_cluster_rpc_rate:      float    // RPCs per second
  cross_cluster_rpc_p99_ms:    float    // p99 latency ms
  merge_events_last_60s:       int
  split_events_last_60s:       int
  connection_latency_ms:       float    // round trip to cluster server
}
```

---

## 6. Event Callbacks

The adapter emits events that the engine subscribes to. The engine must register handlers for all REQUIRED events before calling `connect()`. Events are delivered on the engine's main thread via the adapter's internal queue, drained during `tick()`.

| Event | Required | Payload | When Emitted |
|---|---|---|---|
| `ON_CONNECTED` | ✅ | `cluster_id, server_host` | Successfully joined a cluster server after connect() |
| `ON_DISCONNECTED` | ✅ | `reason: DisconnectReason` | Connection lost or graceful disconnect |
| `ON_CLUSTER_REASSIGNED` | ✅ | `old_cluster_id, new_cluster_id, new_host` | Merge or split in progress — adapter is executing handoff |
| `ON_CLUSTER_HANDOFF_COMPLETE` | ✅ | `new_cluster_id` | Handoff complete, now connected to new cluster server |
| `ON_CONNECTION_FAILED` | ✅ | `reason, attempt_count` | All reconnect attempts exhausted — unrecoverable |
| `ON_ENTITY_ENTERED` | optional | `EntitySnapshot` | Entity became visible (entered observation radius or joined cluster) |
| `ON_ENTITY_LEFT` | optional | `entity_id` | Entity left observation radius or despawned |
| `ON_BOSS_SPAWNED` | optional | `EntitySnapshot, boss_type` | World boss or dungeon boss spawned in range |
| `ON_AOE_RECEIVED` | optional | `AoEObject` | AoE physical object broadcast from neighboring cluster |
| `ON_SYSTEM_MESSAGE` | optional | `severity, message` | Server-generated system message (event announcement, shutdown warning) |

---

## 7. Message Protocol

All messages are length-prefixed. Manager connection uses JSON for debuggability. Cluster connection uses msgpack for throughput. The adapter handles serialization and deserialization — the engine only sees typed structs.

### 7.1 Manager Connection (JSON)

```
// Client → Manager
PLAYER_JOIN      { type, player_id, auth_token, position: {x,y,z},
                   guild_id?, party_id?, enemy_guild_ids: [] }
PLAYER_LEAVE     { type, player_id, reason }

// Manager → Client
CLUSTER_ASSIGN   { type, cluster_id, server_host, server_port, handoff_token? }
CLUSTER_REASSIGN { type, new_cluster_id, new_server_host, new_server_port,
                   handoff_token, deadline_ms }
SYSTEM_MESSAGE   { type, severity: INFO|WARN|ERROR, message, timestamp }
METRICS_UPDATE   { type, MetricsSnapshot }    // pushed every 2s
```

### 7.2 Cluster Connection (msgpack)

```
// Client → ClusterServer (20Hz)
PLAYER_INPUT     { type, player_id, position, velocity, action, action_data?,
                   timestamp, sequence_num, last_state_seq }
                   // last_state_seq: last STATE_UPDATE seq number received by client

// ClusterServer → Client (20Hz) — delta by default
STATE_UPDATE     { type, tick: int, timestamp: float, seq: int,
                   updated: [EntityStateDelta],    // only fields that changed this tick
                   removed_entity_ids: UUID[] }

// EntityStateDelta carries only changed fields via dirty_fields bitmask.
// Server tracks one integer per client: last_acked_seq.
// If incoming last_state_seq != current seq → gap detected →
// server sends full snapshot on next tick then resumes deltas.
STATE_UPDATE_FULL { type, tick: int, timestamp: float, seq: int,
                    entities: [EntitySnapshot],    // complete state of all visible entities
                    removed_entity_ids: UUID[] }
// Full snapshots are demand-driven only — never sent on a fixed schedule.

// ClusterServer → Client (event-driven)
HANDOFF_ACCEPTED { type, cluster_id, player_count }
RPC_RESULT       { type, request_id, result: SUCCESS | FAILURE, state_tick: int }
// state_tick = the STATE_UPDATE tick (or seq) in which this action was applied. Client uses it to
// correlate: show immediate feedback (sound, animation) on success; when STATE_UPDATE with that
// tick arrives, treat it as verification. No duplicate payload (e.g. new_health) in RPC_RESULT —
// authoritative state comes from STATE_UPDATE. Enables responsive feedback for high-ping clients.
AOE_BROADCAST    { type, aoe_object: AoEObject }

// Client → ClusterServer (event-driven)
HANDOFF_CLAIM    { type, handoff_token, player_id }
```

### 7.3 RPC result and client-side prediction

RPC_RESULT carries **success or failure** and the **state_tick** (or seq) of the STATE_UPDATE in which the action was applied. The ClusterServer sends RPC_RESULT to the client when it receives the return from a **SpacetimeDB reducer** (e.g. after calling a game reducer for a discrete event like attack hit or use item). It does not duplicate authoritative state (e.g. new health); that arrives in STATE_UPDATE.

**Client behavior:** On RPC_RESULT success, the client can **immediately** show feedback (sound, animation, optional predicted state) without waiting for the state update — improving perceived responsiveness, especially for high-ping users. When the STATE_UPDATE with that state_tick arrives, the client uses it as **verification**: reconcile prediction or correct. So the client predicts optimistically and the state stream is the source of truth. This is the standard client-side prediction + server reconciliation pattern; the state_tick correlation id is what makes it work.

---

## 8. Metrics

Client-side metrics are **push-based** — the adapter includes a compact metrics payload in every PLAYER_INPUT message sent to the ClusterServer. The server aggregates these across all connected clients and exposes them via its own Prometheus endpoint. There is no per-client HTTP server. Game clients are behind NAT and firewalls and cannot be scraped by Prometheus directly.

Metrics included in PLAYER_INPUT payload:

| Field | Measures |
|---|---|
| `connection_latency_ms` | Round trip to cluster server |
| `entity_cache_size` | Entities currently tracked by adapter |
| `input_queue_depth` | Pending outbound messages |
| `state_recv_rate_hz` | STATE_UPDATE messages received per second |
| `last_handoff_duration_ms` | Duration of most recent cluster handoff (0 if none) |
| `frame_budget_ms` | Time spent in last tick() call |

---

## 9. Failure Modes

| Failure | Detection | Adapter Response | Player Experience |
|---|---|---|---|
| Manager connection lost | WebSocket close or ping timeout (5s) | Reconnect with exponential backoff. Pause input sends. Cache last known state. | Brief freeze. Seamless resume if reconnected within 10s. Otherwise ON_CONNECTION_FAILED. |
| Cluster connection lost | WebSocket close or ping timeout (3s) | Reconnect to same cluster server. If unavailable, request new assignment from Manager. | Brief input lag. Entity state pauses then resumes. No data loss under 30s. |
| Handoff deadline missed | `deadline_ms` exceeded without HANDOFF_ACCEPTED | Close both connections. Request fresh CLUSTER_ASSIGN from Manager. | ~500ms gap. Player state preserved on server — no game state loss. |
| Malformed server message | Deserialization error | Log error with raw bytes. Skip message. Continue. Emit warning in metrics if rate > 1/min. | No visible impact unless persistent. |
| STATE_UPDATE rate drops | < 10Hz for 3 consecutive seconds | Interpolate using last known velocity. Emit metric. Do not disconnect. | Entities move less smoothly. Auto-recovers when rate resumes. |
| Metrics endpoint unreachable | HTTP timeout on Prometheus scrape | Serve last-known values with stale flag. Emit ON_SYSTEM_MESSAGE WARN. | Metrics overlay shows stale indicator. No gameplay impact. |

---

## 10. Implementation Guide for Engine Adapters

Engine-specific implementations are documented in their own specs. **Unreal Engine 5** (demo): see **CA-02 UnrealAdapter** (`ca_02_unreal_adapter.md`) for Mass Entity mapping, WebSocket choice, threading, and validation.

### Threading Model

Network I/O must run on a dedicated thread or async runtime — never on the engine's main game thread. The adapter maintains two queues: an inbound queue (server messages waiting to be processed) and an outbound queue (player input waiting to be sent). The `tick()` method drains the inbound queue and flushes the outbound queue from the main thread. All queue operations must be thread-safe.

> **Warning:** Never perform blocking network operations inside `tick()`. This includes DNS resolution, connection establishment, and synchronous message reads. All of these must happen on the network thread.

### Entity State Delivery

The adapter maintains a local entity cache — a dictionary from `entity_id` to `EntitySnapshot`. Each `STATE_UPDATE` merges into this cache: present entities update, `removed_entity_ids` remove. `get_entity_snapshot()` returns the current cache contents. The engine reads this cache every frame. Cache size is proportional to entities in observation range.

### Interpolation

`STATE_UPDATE` arrives at 20Hz. The engine renders at 60Hz or higher. The adapter must interpolate entity positions between state updates to avoid visible stuttering. Linear interpolation between last known position and current position over the 50ms update window is the minimum required. Adapters may implement higher-quality interpolation using velocity data.

### Cluster Color Coding for Demo

The demo overlay requires visible cluster boundaries. The adapter assigns a deterministic color to each `cluster_id` and includes it in entity `visual_tags` as `cluster_color_{hex}`. The rendering layer reads this tag and applies it to the entity's material. The color must be consistent for the lifetime of a `cluster_id` — if two clusters merge, all entities adopt the surviving cluster's color.

### Validating a New Adapter

An adapter implementation is complete when it passes all of the following checks without modification to any server component.

- Connects to a running ClusterManager and receives CLUSTER_ASSIGN within 5 seconds
- Receives STATE_UPDATE at 20Hz for 60 consecutive seconds without message loss
- Completes a merge handoff (triggered manually via ClusterManager API) within 200ms
- Displays correct cluster color coding for a 3-cluster scenario
- `get_metrics()` returns accurate active cluster count matching Prometheus
- `tick()` completes in under 2ms on target hardware with 500 entities in cache
- Survives Manager connection loss and reconnects without manual intervention

---

*Arcane Engine — CA-01 IClientAdapter — Confidential*
