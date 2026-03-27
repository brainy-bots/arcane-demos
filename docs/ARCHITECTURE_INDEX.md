# Architecture Index

This index is the entrypoint for understanding `arcane-demos` structure and ownership boundaries.

## Start here

- `MODULE_INTERACTIONS.md`: repository-level module map and dependency flow.
- `ARTIFACT_BOUNDARY_POLICY.md`: source vs generated-output policy.
- `COMPATIBILITY_MATRIX.md`: supported version combinations with sibling repositories.

## Boundary model

- `crates/arcane-demo` is demo backend code that integrates with `arcane`.
- `Unreal/ArcaneDemo` is the playable integration project.
- `Unreal/ArcaneDemo/Plugins/ArcaneClient` is a vendored snapshot, while `arcane-client-unreal` remains the canonical plugin source.
- `scripts/` orchestrates local runs and benchmarks and should write outputs only to ignored artifact locations.
