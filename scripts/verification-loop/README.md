# Verification loop (experiment)

**Self-contained experiment** for the AI verification loop: capture game window screenshot + log so the AI can verify behavior. **Not required for the main project.** To remove it, delete this folder (`scripts/verification-loop/`). No other code depends on it.

## What’s here

| Item | Purpose |
|------|--------|
| **`capture_game.py`** | Finds game window by title (excludes File Explorer and blocklisted apps like Albion). **Excludes explorer.exe windows** so a folder titled "Arcane Demo" is never used—only the actual game. Matches both "ArcaneDemo" and "Arcane Demo". Runs an optional **sequence** (wait, keys, mouse, capture), takes one or more screenshots, tails game log. |
| **`run_verification.ps1`** | Runner: **(1)** verifies manager + cluster are running, **(2)** builds game, **(3)** launches game, **(4)** runs **sequence** and capture. Sequence from `-Sequence` or **`sequence.txt`** (AI can edit this file to define actions). |
| **`sequence.txt`** | Optional. One line (or multiple lines joined): comma-separated steps. If present and `-Sequence` not set, the runner uses it. Copy from `sequence.txt.example`. For PIE, sequence should start with `play,wait:N` (e.g. `play,wait:45,...`). |
| **`EDITOR_VS_GAME.md`** | Short explanation of why Play In Editor (PIE) can behave differently from the standalone game (and why humanoids may not show in PIE). |
| **`requirements-capture.txt`** | Python deps: `pyautogui`, `pygetwindow`, `Pillow`. |
| **`capture/`** | Output: `screenshot.png`, `screenshot_1.png`, … (per `capture` in sequence), `game_log.txt`. |

Standalone packaging stays in the Unreal project: **`Unreal/ArcaneDemo/PackageArcaneDemo.ps1`** (you can run that from there; it’s not part of this experiment folder).

## One-time setup

```powershell
pip install -r scripts/verification-loop/requirements-capture.txt
```

For standalone runs, package once from the Unreal project (see `Unreal/ArcaneDemo/README.md`).

## How to run (from repo root)

- **Full loop (verify servers → build → launch → run sequence → capture → close):**  
  `.\scripts\verification-loop\run_verification.ps1 -BuildAndRunGame -CloseAfter`  
  The script first checks that the manager (HTTP 8081) and cluster (port from `/join`) are reachable. If not, it exits with instructions (or use `-StartBackend` to start them automatically). Then: build, launch, short delay for level load, default sequence (walk, look, two captures), close.

- **Start backend automatically if not running:**  
  `.\scripts\verification-loop\run_verification.ps1 -BuildAndRunGame -StartBackend -CloseAfter`  
  If the manager and cluster are already running, the script skips starting new terminals (no accumulation of windows).

- **Multi-cluster verification (Redis + 3 clusters, 150 entities):**  
  Ensure Redis is running and single-cluster is closed, then:  
  `.\scripts\verification-loop\run_verification_multi.ps1`  
  Starts the 4 multi-cluster windows, waits for them, then runs build + game + sequence + capture. Use `-Force` if manager is already running. **Pass:** HUD shows "Entities: 150 | Clusters: 3 | FPS: …"; log contains "entities from 3 clusters"; entities colorized red/green/blue by cluster.

- **Skip server check** (e.g. no backend):  
  `.\scripts\verification-loop\run_verification.ps1 -BuildAndRunGame -SkipServerCheck -CloseAfter`

- **Run in EDITOR + PIE** (same context where humanoids don’t show; so the AI can capture that):  
  `.\scripts\verification-loop\run_verification.ps1 -RunPIE -CloseAfter`  
  Builds, launches the Unreal Editor (no `-game`), waits for it to load, then the sequence sends **Alt+P** (Play) to start PIE. The script focuses the editor, waits 1s, clicks the center of the window (so the viewport has focus), then sends Alt+P. If Play doesn’t start, ensure no other window steals focus during the run, or check **Edit > Editor Preferences > Keyboard Shortcuts** for the actual Play shortcut. Window title used: `Unreal Editor`. See **`EDITOR_VS_GAME.md`** for why Editor vs. game can behave differently.

- **Sequence from file (AI can determine the sequence):**  
  Copy `sequence.txt.example` to `sequence.txt`, edit the steps, then run without `-Sequence`:  
  `.\scripts\verification-loop\run_verification.ps1 -BuildAndRunGame -CloseAfter`  
  The runner uses `scripts/verification-loop/sequence.txt` when present.

- **Longer load time:**  
  `.\scripts\verification-loop\run_verification.ps1 -BuildAndRunGame -InitialDelaySeconds 45 -CloseAfter`

- **Custom sequence on command line:**  
  `.\scripts\verification-loop\run_verification.ps1 -BuildAndRunGame -Sequence "w:3,mouse:80:0,capture,a:1,capture" -CloseAfter`

- Game already running (single screenshot):  
  `.\scripts\verification-loop\run_verification.ps1`

**Avoid capturing the wrong window:** The script excludes known other apps (e.g. Albion, Steam, Discord). If you have another game or app in the foreground, minimize it or close it before running the loop so the Arcane Demo window is the one that matches.

**Use the computer while the loop runs:** With **pywin32** installed (`pip install pywin32`), the script captures the game window via the Windows PrintWindow API, so it gets the *game’s* content even when another window (e.g. Albion) is on top. You can keep using the PC; only during key/mouse steps does the script briefly focus the game. Without pywin32 we capture the screen region, so whatever is on top at that moment is captured—keep the game visible then.

**When the script uses input:** By default the script does **not** beep, block input, or focus/minimize the game (fast iteration). Keys and mouse go to whatever window has focus—keep the game window focused if you want input there. To restore the previous behavior (focus game, block input, minimize after each step, beeps), pass `--beep --block-input --minimize-after-input` to the Python script or `-Beep -BlockInput -MinimizeAfterInput` to the PowerShell runner.

**Sequence format** (comma-separated): `wait:N` (seconds), `w:2` / `a:1` (hold key), `mouse:dx:dy` (relative move for camera), `capture` (screenshot now). Example: `wait:5,w:2,mouse:100:0,capture,w:1,capture`.

## For the AI

- **Determine the sequence:** Edit **`scripts/verification-loop/sequence.txt`** (or create it from `sequence.txt.example`). One line of comma-separated steps: `wait:N`, `play`, `w:2`, `mouse:dx:dy`, `capture`, etc. The runner uses this file when `-Sequence` is not passed. For PIE use **`-RunPIE`** and start the sequence with `play,wait:45,...`.
- **Run verification (game):** `.\scripts\verification-loop\run_verification.ps1 -BuildAndRunGame -CloseAfter` (ensure manager + cluster are running, or use `-StartBackend`).
- **Run verification (PIE):** `.\scripts\verification-loop\run_verification.ps1 -RunPIE -CloseAfter` to capture the same Editor/PIE context where humanoids may not be visible.
- **After a run:** Read **`scripts/verification-loop/capture/screenshot.png`** (and **`screenshot_1.png`**, **`screenshot_2.png`**, …) and **`scripts/verification-loop/capture/game_log.txt`** to verify game state.

## Removing this experiment

Delete the folder:

```powershell
Remove-Item -Recurse -Force scripts\verification-loop
```

Then remove the two optional lines from the repo root **`.gitignore`** that mention `scripts/verification-loop/capture/`, and the optional “AI verification loop” line in **`Unreal/ArcaneDemo/README.md`** if you added it. Nothing else in the project references this folder.
