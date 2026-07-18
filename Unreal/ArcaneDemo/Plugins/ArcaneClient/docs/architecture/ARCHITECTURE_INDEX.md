# Architecture Index

This page is the single navigation entrypoint for plugin internals.

## Core runtime components

- **Codec**: `Source/ArcaneClient/Public/ArcaneProtocolCodec.h` and `Source/ArcaneClient/Private/ArcaneProtocolCodec.cpp`
- **Entity cache**: `Source/ArcaneClient/Public/ArcaneEntityCache.h` and `Source/ArcaneClient/Private/ArcaneEntityCache.cpp`
- **Connection client**: `Source/ArcaneClient/Public/ArcaneConnectionClient.h` and `Source/ArcaneClient/Private/ArcaneConnectionClient.cpp`
- **Lifecycle/state machine**: `Source/ArcaneClient/Public/ArcaneAdapterSubsystem.h` and `Source/ArcaneClient/Private/ArcaneAdapterSubsystem.cpp`

## Architecture docs

- `MODULE_INTERACTIONS.md`: module graph and responsibility boundaries
- `interface-iclientadapter.md`: adapter-facing interface expectations
- `module-unreal-adapter.md`: subsystem/module behavior notes
- `README.md`: docs folder overview

## Test coverage entrypoints

- `Source/ArcaneClient/Private/Tests/ArcaneProtocolCodecTests.cpp`
- `Source/ArcaneClient/Private/Tests/ArcaneEntityCacheTests.cpp`
