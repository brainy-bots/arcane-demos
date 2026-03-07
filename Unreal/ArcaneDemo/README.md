# Arcane Demo — In-Engine (Unreal 5)

Minimal Unreal project that connects to the Arcane manager + cluster and displays replicated entities in the viewport. You can **switch between Arcane networking and default Unreal networking** to compare performance and limits (see **Comparing networking modes** below).

**Library, plug-and-play:** The **Arcane Client** plugin is a **library**—it only replaces the networking layer. The demo uses the same Unreal standards you would use for any replicated game: same character class as the player, same movement and animation pipeline, standard `ApplyEntityStateToActor` from the plugin. No custom “entity” classes required by the library; see `Plugins/ArcaneClient/LIBRARY_PLUG_AND_PLAY.md`.

**How the demo uses the library (only these API calls):** On start, the adapter is initialized and the client connects via the manager (`GET /join`). Each frame, the demo calls: `Adapter->Tick(DeltaTime)`, `Adapter->GetEntitySnapshot()`, then for each entity in the snapshot it gets or spawns an actor (same class as the player) and calls `Adapter->ApplyEntityStateToActor(Actor, State, WorldOrigin, Scale)`. No other plugin APIs are required; the rest is standard Unreal (character, movement, animation).

## Prerequisites

- **Unreal Engine 5.7** (or 5.5+; adjust `EngineAssociation` in `ArcaneDemo.uproject` if needed).
- **WebSockets in the engine:** The project uses the engine’s WebSockets module. If the editor says a WebSocket plugin is required or the project won’t open:
  1. Open **any** Unreal project (or create a blank one) with the same engine version.
  2. **Edit → Plugins**, search for **WebSocket**.
  3. Enable **WebSocket Networking** (under **Experimental**). Restart the editor when prompted.
  4. Open the Arcane Demo project again.
- **Manager + cluster running** (from the repo root):
  - **Single cluster** (one window for manager, one for cluster with 200 demo entities; humanoid path-like movement):
    ```powershell
    cd E:\code\pgp-demo
    .\scripts\run_demo.ps1
    ```
  - **Multi-cluster** (manager + 3 clusters, Redis required; 50 entities per cluster, colorized by cluster):
    ```powershell
    cd E:\code\pgp-demo
    .\scripts\run_demo_multi.ps1
    ```
    Ensure Redis is running (e.g. `docker compose up -d` from repo root). Leave all four windows running.
  The cluster serves WebSocket at `ws://127.0.0.1:8080` (single) or one of 8080/8082/8084 (multi); join via `GET http://127.0.0.1:8081/join`.

**Multi-cluster demo (3 clusters, Redis required):**

1. **Start Redis** (from repo root): `docker compose up -d` (or run Redis on 127.0.0.1:6379 another way).
2. **Start manager + 3 clusters:** `.\scripts\run_demo_multi.ps1` from repo root. Four windows open: manager + cluster A (8080), B (8082), C (8084). Each cluster runs 50 demo entities and replicates state via Redis.
3. **Launch the game** and connect as usual (GET /join assigns you to one cluster). You see **all** entities from all 3 clusters; **color by cluster** shows which server owns which entity (red/green/blue by cluster ID). Total entities ≈ 150 (50×3).

## Demo ready (quick start)

To get the demo running in one go:

1. **Backend:** From repo root run `.\scripts\run_demo.ps1` (starts manager + cluster with 200 demo entities). Leave both windows open.
2. **Game:** Build if needed: from `Unreal/ArcaneDemo` run `.\BuildArcaneDemo.ps1` (close the editor first). Then open the project and press **Play**, or run the **standalone game**: from repo root, `.\scripts\verification-loop\run_verification.ps1 -BuildAndRunGame -CloseAfter` (builds, launches game, captures, then closes).
3. You should see humanoid entities (same mesh as the player) moving in path-like ways, colorized by cluster. If the cluster build fails with "Access is denied" on `arcane-cluster-demo.exe`, close the cluster window and run `run_demo.ps1` again.

**Two clients (see each other's movement):** From repo root run `.\scripts\run_two_clients.ps1` (optionally `-StartBackend` if manager/cluster aren’t running). This builds once and launches **two** game windows. Both connect to the same cluster; each sends its player position/velocity and receives the full entity list (demo agents + the other player). Move in one window and watch the other to see your character replicated from the server.

**One-command readiness check** (from repo root): `.\scripts\verify_demo_ready.ps1` runs `cargo check` and the full verification loop (start backend, build game, launch, capture, close). Exits 0 if all pass.

**Demo ready checklist** (how to verify the demo is running):

- [ ] Backend: manager + cluster running (`.\scripts\run_demo.ps1` from repo root; two windows stay open).
- [ ] Game: built and launched (Play in editor or standalone); map loads.
- [ ] Log: game log contains `Arcane Demo ready: N entities visible, replication active.` (in `Unreal/ArcaneDemo/Saved/Logs/ArcaneDemo.log` or verification capture).
- [ ] In-game: humanoid entities (same mesh as player) visible and moving; color by cluster if multi-cluster.

If all four are true, the demo is ready to show.

**Run without the editor (faster iteration):** You can build and run the demo without opening the Unreal Editor. Close the editor, then from `Unreal/ArcaneDemo` run:
- **`.\RunGameNoEditor.ps1`** — build and launch the game window (no editor UI). Start manager + cluster separately, or use **`.\RunGameNoEditor.ps1 -StartBackend`** to start them in a new window first.
- **`.\RunGameNoEditor.ps1 -BuildOnly`** — only build (e.g. after code changes); run again without `-BuildOnly` to launch.
- **`.\RunGameNoEditor.ps1 -LaunchOnly`** — only launch (skip build; use after a previous build).

From repo root you can also use **`.\scripts\verification-loop\run_verification.ps1 -BuildAndRunGame -CloseAfter`** (builds, launches game, runs a short sequence and capture, then closes).

**Comparing Arcane vs Unreal networking:** In **World Settings → Game Mode** (Arcane Demo Game Mode) you can uncheck **Use Arcane Networking** to use default Unreal replication instead: the GameMode spawns **Replicated Bot Spawner** with **Num Unreal Replicated Bots** (e.g. 20–100). Press Play (listen server); same character class, no Rust backend. Use this to compare FPS and limits of default networking vs Arcane. Entity visuals use the same character/ABP as the player; state is applied via the library's **ApplyEntityStateToActor**.

### Comparing networking modes (details)

**Arcane (default):** GameMode spawns **Arcane Entity Display** if the level has none. You can place one (Place Actors, search Arcane Entity Display) to set **Position Scale**, **Server World Center**, etc. Entities use the same character and AnimBlueprint as the player; state via the library's ApplyEntityStateToActor.

**Unreal mode:** In World Settings, select **Arcane Demo Game Mode** and uncheck **Use Arcane Networking**. Set **Num Unreal Replicated Bots** (e.g. 20, 50, 100). Press Play; bots replicate with default Unreal. Same character class; no Rust backend.

### Entity animations (standard multiplayer approach)

**Standard way (Epic):** For replicated or simulated characters, the Animation Blueprint should set **Speed** from the pawn’s velocity for **all** pawns, not only when **Is Locally Controlled**. Use **Try Get Pawn Owner → Get Velocity → Vector Length → Set Speed** (no branch). That way both the local player and entity/replicated characters animate from the same data. The Third Person template’s **ABP_Unarmed** often only updates Speed when Is Locally Controlled, so entities stay at Speed 0.

**Option A – One-time fix in your project (recommended):**

1. **Duplicate** your player AnimBlueprint (e.g. **ABP_Unarmed**): Content Browser → right‑click → Duplicate.
2. Rename to **ABP_ArcaneEntity** and save in the same folder (e.g. `Content/Characters/Mannequins/Anims/Unarmed/`).
3. Open **ABP_ArcaneEntity** → **Event Graph** (Event Blueprint Update Animation).
4. **Remove the “Is Locally Controlled” branch.** Use: **Try Get Pawn Owner** → **Get Velocity** → **Vector Length** → **Set Speed** (no condition).
5. **Compile** and **Save**.

The demo will use **ABP_ArcaneEntity** for entity characters when it exists. The AnimGraph (blend tree) can stay the same; only the Event Graph must update Speed for all pawns. **Re-apply for ABP:** The demo re-applies Velocity and Acceleration in **TG_PostUpdateWork** (via `ArcaneEntityDisplayLateUpdateComponent`) so the ABP sees correct values after the Character Movement Component has ticked—matching the standard pattern where the CMC can overwrite during its tick and the animation system reads later.

**Option B – Use a multiplayer-ready asset:** If you prefer a ready-made system, use a replicated locomotion asset and point the demo at its Animation Blueprint (e.g. set entity mesh Anim Instance class to that ABP). Examples (free, community):

- **[ALS-Community](https://github.com/dyanikoglu/ALS-Community)** – Replicated Advanced Locomotion System for UE 5.x (C++ and Blueprint).
- **BaseLocomotion** (GitHub) – Network-replicated base locomotion.

If **ABP_ArcaneEntity** is missing, the log errors once (no fallback); create the Blueprint so entity animations can play.

## Open the project

If **double‑clicking `ArcaneDemo.uproject` does nothing** (file not linked to Unreal):

1. **Epic Games Launcher:** Open the **Epic Games Launcher** → **Unreal Engine** → **Library** → **Add** (or **+**). Browse to `Unreal/ArcaneDemo/ArcaneDemo.uproject` and add it. Then launch **Arcane Demo** from the Library.
2. **Script:** Run **`OpenArcaneDemo.ps1`** in this folder (right‑click → **Run with PowerShell**, or from a terminal: `pwsh -File OpenArcaneDemo.ps1`). It looks for Unreal Engine 5.7 in standard install locations and starts the editor with this project. If your engine is elsewhere, the script prints the command so you can edit the path.

Once the project is open, let the editor compile when it asks (first time or after code changes).

**Full rebuild from scratch:** Run `.\CleanRebuild.ps1` from `Unreal/ArcaneDemo` (or from repo root). That deletes `Binaries`, `Intermediate`, and solution files. Then open `ArcaneDemo.uproject` and choose **Yes** when asked to rebuild.

**Building without Visual Studio:** If you see **“Engine modules are out of date, and cannot be compiled while the engine is running. Please build through your IDE.”**, the editor cannot compile while it’s running. Do this instead:

1. **Close the Unreal Editor** (and any PIE session) completely — otherwise you may see “Unable to build while Live Coding is active”.
2. **Build from the command line:** In a terminal, run **`BuildArcaneDemo.ps1`** from this folder:
   ```powershell
   cd E:\code\pgp-demo\Unreal\ArcaneDemo
   .\BuildArcaneDemo.ps1
   ```
   The script finds your Unreal Engine install (e.g. `C:\Program Files\Epic Games\UE_5.7`) and runs Unreal Build Tool to compile the game and plugin modules. No IDE is required.
3. When the build finishes successfully, **open the project again** from the Epic Games Launcher (or `OpenArcaneDemo.ps1`). The editor will load the updated modules.

If the script can’t find the engine, it prints the exact command; edit the engine path and run that command in PowerShell.

### Live Coding (hot reload) — apply C++ changes without closing the editor

To have code changes (e.g. in game module or ArcaneClient plugin) applied while the editor or PIE is running:

1. **Enable Live Coding:** **Edit → Editor Preferences → General → Live Coding** → enable **Enable Live Coding**.
2. After editing `.cpp` (or `.h` where supported), trigger a Live Coding compile: press **Ctrl + Alt + F11** (focus can be in the editor or in your IDE).
3. The Live Coding console will show build progress; when it finishes, the running editor/PIE uses the new code.

**Compile on save (optional):** Unreal doesn’t compile automatically on file save. You can approximate it by running a script when you save a source file, which sends Ctrl+Alt+F11 to the editor:
- Run **`Scripts\TriggerLiveCoding.ps1`** (e.g. from a “Run on Save” extension in Cursor/VS Code when `Source/**/*.cpp` or `Plugins/**/*.cpp` is saved). The Unreal Editor window must be running (title contains “Unreal Editor”).  
- In Cursor/VS Code you can use an extension like **Run on Save** and add a rule that runs: `pwsh -File "E:\code\pgp-demo\Unreal\ArcaneDemo\Scripts\TriggerLiveCoding.ps1"` when matching `**/*.cpp` or `**/*.h` under the Unreal project.

**Limitations:** Changing **constructors** or **header layout** (e.g. new member variables, new virtuals) usually requires a **full restart** (close editor and rebuild). Most **function body changes** in `.cpp` work with Live Coding. If Live Coding fails or the editor becomes unstable, do a full rebuild (close editor, rebuild, reopen).

### "Modules missing or built with different engine version" (keeps coming back)

If the editor keeps showing this and **Yes** doesn’t fix it (or the build fails), the usual cause is that the modules were never built for this engine, or the C++ build didn’t complete:

1. **Close Unreal Editor** completely.
2. **Clean:** Run `.\CleanRebuild.ps1` from `Unreal/ArcaneDemo` (deletes `Binaries`, `Intermediate`, `.sln`).
3. **Open the project:** Double‑click `ArcaneDemo.uproject`. When prompted **“Modules are out of date, rebuild?”** choose **Yes** and wait for the build to finish. No Visual Studio required — the editor runs Unreal Build Tool for you.

If the in‑editor build fails (e.g. missing **NetFxSDK** or **WebSockets**), fix the reported error (e.g. install .NET Framework SDK via Windows; enable **WebSocket Networking** in **Edit → Plugins** in the engine) and try again. The `.uproject` tells the editor which modules to load; UBT compiles them when you choose “Yes” to rebuild.

## Player character

When you press **Play**, you control **ArcaneDemoCharacter**: a third-person character with:

- **WASD** — move
- **Mouse** — turn and look up/down (camera)
- **Space** — jump

The character uses Unreal’s **Character** (gravity, walking, jumping). The camera is a spring arm behind the character. If your project has the Third Person mannequin mesh at `/Game/Characters/Mannequins/Meshes/SKM_Quinn`, it will be used; otherwise you’ll see the default capsule (movement and camera still work).

## Run the demo

1. In the editor: **File → New Level → Empty Level** (or use **Basic**), then **Save** (e.g. `Maps/ArcaneDemoMap`).
2. **Edit → Project Settings → Maps & Modes**: set **Default Map** and **Editor Startup Map** to your saved map (optional; lets you press Play without opening the map).
3. **Play** (Alt+P or toolbar).
4. The **Arcane Demo** game mode runs and spawns **Arcane Entity Display** if the level doesn’t have one. The adapter:
   - Calls **GET http://127.0.0.1:8081/join** (manager),
   - Connects **WebSocket** to the returned host:port (cluster),
   - Receives **STATE_UPDATE** JSON each tick and updates the entity cache,
   - **Arcane Entity Display** draws a green debug sphere (or character mesh) at each entity position (scaled by **Position Scale**, default 1).

You should see entities (spheres or character meshes) in the world. **Center entities on player** (default off). The entity cloud is placed once at your character when entities first arrive, then stays fixed in the world (turn on **Center entities on player** to have it follow you). If you see no entities, check the Output Log for `ArcaneEntityDisplay: displaying N entities...`; ensure **Position Scale** is 1 (or 10). If the cluster or manager isn’t running, the adapter will emit **On Connection Failed**.

## Optional: place the display yourself

- Drag **Arcane Entity Display** (C++ class in **Place Actors** or **Class Viewer**) into the level.
- In Details: **Auto Connect** = true, **Manager URL** on the adapter is taken from the subsystem (set in Blueprint or C++ before Connect). The subsystem’s default **Manager URL** is `http://127.0.0.1:8081`; override in Blueprint by getting the subsystem and calling **Initialize** with your URL before **Connect**.

## Changing the manager URL

The adapter subsystem’s **Manager Url** defaults to `http://127.0.0.1:8081`. To change it:

- In C++: get the subsystem and call `Initialize(TEXT("http://your-host:8081"))` before `Connect()`.
- Or add a **Blueprint** that gets **Arcane Adapter Subsystem** (from Game Instance), calls **Initialize**, then **Connect**, and run it on BeginPlay.

## Unreal units and position scale

Unreal uses **1 unit = 1 cm** by default (Z-up). Our server sends positions in a 0–200 demo world. **Position Scale** on Arcane Entity Display is "server units → Unreal units": 1 ≈ 2 m spread, 10 ≈ 20 m, 100 ≈ 200 m. See **`Docs/UNREAL_UNITS_AND_SCALE.md`** for the full reference and how to avoid scale mistakes.

## Optional: AI verification loop (experiment)

A self-contained experiment in **`scripts/verification-loop/`** lets you capture a game screenshot and log for AI/automation verification. Not required for the project; see **`scripts/verification-loop/README.md`** for setup. To remove it, delete that folder.

## Project layout

| Path | Purpose |
|------|--------|
| `Plugins/ArcaneClient/` | Client adapter plugin: HTTP join, WebSocket state, entity cache (CA-02–style). |
| `Source/ArcaneDemo/` | Game module: GameMode (spawns display), **Arcane Entity Display** actor (draws debug spheres from snapshot). |
| `Config/DefaultEngine.ini` | Sets default game mode to **Arcane Demo Game Mode**. |

## Troubleshooting

- **Entities not animating (idle only)**: The default ABP often only updates **Speed** when **Is Locally Controlled**. Use the **standard approach**: create **ABP_ArcaneEntity** (see **Entity animations**) so Speed is set from Get Velocity for all pawns, or use a multiplayer-ready asset (e.g. ALS-Community).
- **"Plugin 'ArcaneClient' failed to load because module 'ArcaneClient' could not be initialized successfully"**: The ArcaneClient plugin now loads **WebSockets on demand** (when you call **Connect**), so the plugin should load even if the WebSockets module has issues at startup. If you still see this, do a full clean rebuild (see **"Modules missing..."** below). If it persists, when you press **Connect** you may get a clear error: *"WebSockets module could not be loaded"* — in that case enable **WebSocket Networking** (Experimental) in **Edit → Plugins** and restart the editor.
- **"Modules missing or built with different engine version" (repeating)**: See the section above: **"Modules missing or built with different engine version" (keeps coming back)**. Use the manual rebuild steps (clean → Generate Visual Studio project files → build from VS).
- **"Could not find NetFxSDK" / SwarmInterface**: The editor build needs the **.NET Framework SDK** (4.6.0 or higher). Install from a terminal: `winget install -e --id Microsoft.DotNet.Framework.DeveloperPack.4.6 --accept-package-agreements --accept-source-agreements`. Or via **Visual Studio Installer** → **Modify** → **Individual components** → search **.NET Framework** → check **.NET Framework 4.6.2 SDK**. Then rebuild.
- **No spheres**: Ensure `.\scripts\run_demo.ps1` is running (manager + cluster). Check **Output Log** for “Connection failed” or HTTP errors.
- **Compile errors / missing WebSockets**: The code uses the engine’s **WebSockets** module. If the build fails for missing WebSockets, enable **WebSocket Networking** in the engine: open another project with the same engine → **Edit → Plugins** → search **WebSocket** → enable **WebSocket Networking** (Experimental) → restart. Then reopen this project and rebuild. You can also try right‑clicking `ArcaneDemo.uproject` → **Generate Visual Studio project files**.
- **Engine version**: If you use a different release (e.g. 5.5 or 5.6), open `ArcaneDemo.uproject` in a text editor and set `"EngineAssociation": "5.5"` (or your version) to match your install.
