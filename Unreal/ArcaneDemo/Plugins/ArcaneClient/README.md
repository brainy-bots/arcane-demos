# Arcane Client — Unreal Engine plugin

Client adapter for the Arcane backend: HTTP join to manager, WebSocket to cluster, entity state cache. Add this plugin to your UE5 project to use Arcane networking.

## Add to your project

1. **Copy the plugin** into your project's `Plugins/` folder:
   - Clone or download this repo.
   - Copy the entire contents (so that `ArcaneClient.uplugin` is at `YourProject/Plugins/ArcaneClient/ArcaneClient.uplugin`).

2. **Or use a Git submodule** (from your project root):
   ```bash
   git submodule add <url-of-arcane-client-unreal> Plugins/ArcaneClient
   ```

3. Open your project in Unreal Editor; the plugin should be enabled. If not, enable it under Edit → Plugins.

## Usage

- Initialize the adapter and connect via the manager (`GET /join`). Then each frame: `Tick(DeltaTime)`, `GetEntitySnapshot()`, and for each entity apply state to your actor with `ApplyEntityStateToActor(Actor, State, WorldOrigin, Scale)`.
- See **[arcane-demos](https://github.com/brainy-bots/arcane-demos)** for a minimal UE5 game project that uses this plugin (join, replication, entity display).

## Connection lifecycle and retries

- The subsystem exposes explicit connection states: `Disconnected`, `Joining`, `ConnectingWebSocket`, `Connected`, `Reconnecting`, `Failed`.
- Auto-reconnect is enabled by default and can be configured with:
  - `bEnableAutoReconnect`
  - `MaxReconnectAttempts`
  - `ReconnectDelaySeconds`
- On connection errors, the subsystem retries join+websocket connect until attempts are exhausted, then moves to `Failed`.

## Tests

- Plugin internals include Unreal automation tests in `Source/ArcaneClient/Private/Tests/`:
  - protocol codec parsing/encoding tests
  - entity cache snapshot/interpolation tests
- Run these from Unreal's automation runner in an editor/dev build.

## Backend

You need a running Arcane backend (manager + cluster). Use the reference server from the **arcane** Rust repo, or the demo backend from **arcane-demos**.

## Versioning

Releases are tagged (e.g. `v0.1.0`). Pin to a tag when using as a submodule for reproducible builds.
