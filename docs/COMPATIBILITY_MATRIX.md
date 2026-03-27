# Compatibility Matrix

This matrix defines the combinations that are expected to work for demo and verification flows.

## Supported combinations

| arcane-demos | arcane | arcane-client-unreal | Notes |
|---|---|---|---|
| `main` | `main` | `main` | Default active development path. |
| `v0.1.x` | `v0.1.x` | `v0.1.x` | Recommended pinned setup for reproducible demos. |

## Contract assumptions

- Manager `GET /join` response shape used by the Unreal plugin remains compatible.
- Cluster WebSocket payload for entity delta remains compatible with plugin parser.
- Demo scripts expect `arcane-manager` and `arcane-cluster` binaries with the current CLI/env var set.

## Upgrade policy

- When `arcane` or `arcane-client-unreal` introduces breaking contract changes, update:
  - this matrix,
  - demo scripts/docs that rely on the contract,
  - and add migration notes in `README.md`.
