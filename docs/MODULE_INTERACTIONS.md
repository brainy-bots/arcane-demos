# arcane-demos module interactions

This page focuses on repository boundaries and demo composition.

```mermaid
flowchart LR
  subgraph DemoRepo["arcane-demos"]
    Scripts["scripts/*.ps1"]
    DemoCrate["crates/arcane-demo"]
    UnrealProj["Unreal/ArcaneDemo"]
    PluginCopy["Unreal/ArcaneDemo/Plugins/ArcaneClient (copy)"]
  end

  subgraph ExternalRepos["Sibling repos"]
    Arcane["arcane (manager/cluster binaries)"]
    PluginCanonical["arcane-client-unreal (canonical plugin source)"]
  end

  Scripts --> DemoCrate
  Scripts --> Arcane
  UnrealProj --> PluginCopy
  PluginCanonical -->|"manual sync/copy"| PluginCopy
  DemoCrate -->|"runtime integration"| Arcane
  UnrealProj -->|"join + ws gameplay"| Arcane
```

## Responsibility summary

- `scripts/`: developer workflows (run demo, benchmark, verification).
- `crates/arcane-demo`: backend demo logic and demo binaries.
- `Unreal/ArcaneDemo`: playable UE project for manual/verification runs.
- `Unreal/ArcaneDemo/Plugins/ArcaneClient`: vendored plugin copy used by this project.

## Boundary rules

- Canonical plugin development happens in `arcane-client-unreal`; this repo uses a copied snapshot.
- `arcane-demos` is integration-oriented: it should demonstrate usage of `arcane`/plugin contracts, not redefine them.
- Generated outputs should stay under dedicated output roots and `.gitignore` policies.
