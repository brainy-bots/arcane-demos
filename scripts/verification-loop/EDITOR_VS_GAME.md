# Editor vs. Game: why PIE can behave differently

## What’s the difference?

| | **Standalone game** | **Play In Editor (PIE)** |
|---|----------------------|---------------------------|
| **Process** | One process, one world. Either `UnrealEditor.exe -game` or a packaged `.exe`. | Editor process; a **second world** (the “PIE world”) is created when you press Play. |
| **Context** | Pure game runtime. No editor UI, no editor-only systems. | Game runs **inside** the editor. Editor world still exists; viewport switches to the PIE world. |
| **Initialization** | Single, linear init. | Two “worlds”: editor world (level editing) and PIE world (game). Spawn order, registration, and which scene gets primitives can differ. |
| **Rendering** | One scene, one view. | PIE viewport may use a different scene graph or show flags; dynamically spawned primitives sometimes don’t get attached the same way as in standalone. |
| **Materials / shaders** | Built for game. | With `WITH_EDITOR`, some paths are editor-specific; materials can compile or render differently in the editor context. |
| **Packages** | Normal game packages. | Runtime-spawned actors often live in **transient** (PIE) packages; some systems treat them differently. |

## Why do humanoids show in game but not in PIE?

Typical causes:

1. **Registration / scene attachment**  
   In standalone, when we spawn an actor and set its mesh, the primitive is registered and added to the scene. In PIE, the same sequence can run in a different order or context, so the mesh component might be registered before the PIE viewport’s scene is ready, or the primitive might not be linked to the scene that the PIE viewport is drawing. Result: logic says “visible”, but the renderer never sees it.

2. **Timing**  
   In PIE, BeginPlay and first frame happen with the editor and the PIE world both active. Deferring mesh setup to BeginPlay (or later) can help, but if the underlying issue is “this primitive never gets into the PIE scene,” timing alone may not fix it.

3. **Materials**  
   Default mannequin (or other) materials might be compiled or selected differently in editor builds. Overriding with a simple engine material in `WITH_EDITOR` can fix “mesh is there but not visible”; it doesn’t fix “primitive never added to PIE scene.”

4. **Editor-only flags**  
   If a component or actor is marked editor-only, or if show flags hide “runtime” objects in the editor view, they’ll show in standalone but not in PIE.

So: **in game** you get a single, clean game world and one scene, so the humanoid mesh is visible. **In PIE** the same actor may not end up in the scene the viewport is rendering, or may be affected by editor vs game material/registration differences.

## What we can do

- **Run the verification loop in PIE** so the AI can capture screenshots from the editor (same context where the bug appears). Use **`-RunPIE`** instead of `-BuildAndRunGame` (see README).
- **Debug in editor** with the same run (breakpoints, logs, “Show” flags) to see if the mesh component is attached to the right scene and has a drawable mesh and material.
- **Compare with the player character**: the character that you control is visible in PIE; its mesh is set up in the Blueprint/constructor and is part of the level or default pawn. Replicating that pattern (e.g. component created in constructor, only asset assigned at runtime) can help.
