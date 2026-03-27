# Artifact Boundary Policy

This repository is an integration/demo source tree. Generated runtime outputs must not mix with source by default.

## Allowed source roots

- `crates/`
- `Unreal/ArcaneDemo/Source/`
- `Unreal/ArcaneDemo/Config/`
- `scripts/` (script source only)
- `docs/`

## Generated output roots

Generated files should be written under dedicated output folders and ignored by git:

- `scripts/benchmark/` run outputs (`benchmark_results.csv`, charts, stderr captures)
- `scripts/swarm/` run outputs (`*_sweep.csv`, `*_logs/`)
- `scripts/verification-loop/capture/` screenshots/log captures
- Unreal build/runtime outputs under `Unreal/ArcaneDemo/Binaries|Build|Intermediate|Saved`

## Rules

1. If a script writes outputs, default to an ignored path.
2. Keep reproducibility inputs in source (`.md`, `.ps1`, config), not generated result dumps.
3. If benchmark result files must be shared, place them in a docs/report file rather than committing raw run artifacts.
