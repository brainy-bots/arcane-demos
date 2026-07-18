# Arcane Client module interactions

This page maps plugin internals to responsibilities and data flow.

```mermaid
flowchart LR
  subgraph PublicAPI["Public UE API"]
    Subsystem["UArcaneAdapterSubsystem"]
  end

  subgraph Internal["Internal components"]
    Conn["ArcaneConnectionClient"]
    Codec["ArcaneProtocolCodec"]
    Cache["ArcaneEntityCache"]
  end

  subgraph External["External services"]
    Manager["Arcane manager /join"]
    Cluster["Arcane cluster WebSocket"]
  end

  Subsystem -->|"HTTP GET /join"| Manager
  Manager --> Subsystem
  Subsystem -->|"connect/send/close"| Conn
  Conn -->|"messages/events"| Subsystem
  Subsystem -->|"parse/encode"| Codec
  Subsystem -->|"apply/update/snapshot"| Cache
  Conn --> Cluster
  Cluster --> Conn
```

## Responsibility summary

- `UArcaneAdapterSubsystem`: lifecycle orchestration, retry policy/state machine, Blueprint-facing API.
- `ArcaneConnectionClient`: WebSocket transport wrapper and callback wiring.
- `ArcaneProtocolCodec`: join/state JSON parsing and player-state JSON encoding.
- `ArcaneEntityCache`: snapshot storage and interpolation logic for render-time reads.

## Stability note

External compatibility target is the subsystem API and backend protocol expectations (manager join shape, cluster state payloads). Internal class boundaries are implementation details and may evolve.
