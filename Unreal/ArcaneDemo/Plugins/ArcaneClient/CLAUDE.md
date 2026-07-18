# CLAUDE.md — arcane-unreal (UE5 plugin)

## What this repo is

Unreal Engine 5 plugin for the Arcane multiplayer backend. Currently contains the client adapter (`ArcaneClient` module); being restructured into a multi-module plugin:

| Module | UE Type | Status |
|---|---|---|
| `ArcaneCore` | Runtime | Planned — shared types, wire format, coordinate conversion |
| `ArcaneClient` | Runtime | Exists — WS connection, entity cache, join flow |
| `ArcaneServer` | Server | Planned — NetDriver, cluster lifecycle, FFI bridge |

Tracked by epic: [arcane#124](https://github.com/brainy-bots/arcane/issues/124), [#7](https://github.com/brainy-bots/arcane-unreal/issues/7)

## Core architectural premise

Arcane partitions cluster-server authority by **predicted interaction probability** (affinity clustering), not by space. The UE plugin integrates with this via a custom `UArcaneNetDriver` — UE's replication APIs work unchanged, bytes flow through Arcane transport. See `arcane/docs/architecture/interface-iclusteringmodel.md`.

## Build & test

No full UE compilation in CI (requires engine source). CI validates:
- Plugin descriptor (`ArcaneClient.uplugin`) structure
- Source layout (Public/Private directories, Build.cs)
- Test file existence

To run tests locally: open in UE Editor, use the Automation Runner for tests in `Source/ArcaneClient/Private/Tests/`.

## Key constraints

- **UE 5.4 LTS** target for Chaos API stability
- **Coordinate boundary**: UE is Z-up/left-handed/cm; Arcane wire is Y-up/right-handed/m. Conversion at plugin boundary in ArcaneCore.
- **AGPL-3.0** license. `Type: "Server"` module loading ensures client builds never include server code.
- **No standalone documentation files** — architecture lives in the vault (`arcane-vault/entities/Unreal Cluster Node.md`) and GitHub issues.

## Repo references

- Architecture docs within repo: `docs/architecture/`
- Demo integration: [arcane-demos](https://github.com/brainy-bots/arcane-demos)
- Vault entity: `arcane-vault/entities/Unreal Cluster Node.md`
- Parent ecosystem: [arcane](https://github.com/brainy-bots/arcane)
