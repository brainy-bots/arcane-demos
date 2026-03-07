#!/usr/bin/env python3
"""
Capture game state for the AI verification loop: find the game window, optionally send
keyboard + mouse input, run an interaction sequence, and capture one or more screenshots.
Output: scripts/verification-loop/capture/ (screenshot.png and/or screenshot_1.png, ...,
game_log.txt). File Explorer windows are excluded.

Sequence format (--sequence): comma-separated steps.
  wait:N       - wait N seconds (e.g. wait:5)
  play         - send Alt+P (Unreal Editor: start Play In Editor)
  w:2, a:1     - hold key for N seconds (w/a/s/d, space, etc.; 0 = tap)
  mouse:dx:dy   - move mouse relative by dx, dy (for camera look)
  capture       - take a screenshot now (screenshot_1.png, screenshot_2.png, ...)

Example: --initial-delay 35 --sequence "w:2,mouse:100:0,capture,w:1,mouse:-50:30,capture"
  Wait 35s for level load, walk 2s, look right, screenshot, walk 1s, look left/up, screenshot.
"""

from __future__ import annotations

import argparse
import ctypes
import shutil
import subprocess
import sys
import time
from pathlib import Path

# Optional: pywin32 enables capturing the game window when it's in the background (PrintWindow API)
try:
    import win32gui
    import win32ui
    from ctypes import windll
    _HAS_PYWIN32 = True
except ImportError:
    _HAS_PYWIN32 = False

# Optional deps — fail with clear message if missing
try:
    import pyautogui
except ImportError:
    print("pip install pyautogui", file=sys.stderr)
    sys.exit(1)
try:
    import pygetwindow as gw
except ImportError:
    print("pip install pygetwindow", file=sys.stderr)
    sys.exit(1)

# Fail-safe: move mouse to corner to abort
pyautogui.FAILSAFE = True

SCRIPT_DIR = Path(__file__).resolve().parent
REPO_ROOT = SCRIPT_DIR.parent.parent
DEFAULT_OUTPUT_DIR = SCRIPT_DIR / "capture"
DEFAULT_WINDOW_TITLE = "ArcaneDemo"
LOG_TAIL_LINES = 200

# Step types: ("wait", N), ("key", key, duration), ("mouse", dx, dy), ("capture",), ("play",) = Alt+P for PIE
def parse_sequence(spec: str) -> list[tuple]:
    if not spec or not spec.strip():
        return []
    steps = []
    for part in spec.split(","):
        part = part.strip().lower()
        if not part:
            continue
        if part == "capture":
            steps.append(("capture",))
        elif part == "play":
            steps.append(("play",))
        elif part.startswith("wait:"):
            try:
                n = float(part[5:].strip())
                steps.append(("wait", n))
            except ValueError:
                pass
        elif part.startswith("mouse:"):
            # mouse:dx:dy or mouse:dx,dy
            rest = part[6:].strip()
            if ":" in rest:
                a, b = rest.split(":", 1)
            else:
                a, b = rest.replace(" ", "").split(",", 1) if "," in rest else (rest, "0")
            try:
                dx, dy = int(a.strip()), int(b.strip())
                steps.append(("mouse", dx, dy))
            except ValueError:
                pass
        else:
            # key:duration (e.g. w:2, space:0)
            if ":" in part:
                k, d = part.rsplit(":", 1)
                try:
                    steps.append(("key", k.strip(), float(d.strip())))
                except ValueError:
                    steps.append(("key", k.strip(), 0.0))
            else:
                steps.append(("key", part, 0.0))
    return steps


def _is_explorer_window(title: str) -> bool:
    """Exclude File Explorer by title (e.g. 'File Explorer' in caption)."""
    if not title:
        return False
    t = title.lower()
    return "explorer" in t or "file explorer" in t


def _is_explorer_process(window) -> bool:
    """Exclude windows owned by explorer.exe (e.g. folder titled 'Arcane Demo' with no 'explorer' in title)."""
    if sys.platform != "win32":
        return False
    hwnd = getattr(window, "_hWnd", None)
    if hwnd is None:
        return False
    try:
        pid = ctypes.c_ulong()
        ctypes.windll.user32.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
        if not pid.value:
            return False
        # OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION=0x1000), then QueryFullProcessImageNameW
        PROCESS_QUERY_LIMITED_INFORMATION = 0x1000
        handle = ctypes.windll.kernel32.OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, False, pid)
        if not handle:
            return False
        try:
            size = ctypes.c_ulong(ctypes.wintypes.MAX_PATH)
            buf = ctypes.create_unicode_buffer(size.value)
            if ctypes.windll.kernel32.QueryFullProcessImageNameW(handle, 0, buf, ctypes.byref(size)):
                path = buf.value.lower()
                return "explorer.exe" in path or path.endswith("explorer")
        finally:
            ctypes.windll.kernel32.CloseHandle(handle)
    except Exception:
        pass
    return False


def _is_blocked_other_app(title: str) -> bool:
    """Exclude other games/apps so we don't capture the wrong window when the user has multiple open."""
    if not title:
        return False
    t = title.lower()
    blocked = (
        "albion",  # Albion Online or similar
        "world of warcraft", "wow ",
        "steam",
        "discord",
    )
    return any(b in t for b in blocked)


def _title_matches(title: str, title_substring: str) -> bool:
    """True if window title matches our search (allows 'Arcane Demo' when searching 'ArcaneDemo')."""
    if not title or not title_substring:
        return False
    t = title.lower()
    sub = title_substring.lower()
    if sub in t:
        return True
    # Allow "Arcane Demo" when searching for "ArcaneDemo" (no space)
    if sub.replace(" ", "") == "arcanedemo" and "arcane" in t and "demo" in t:
        return True
    return False


def find_window(title_substring: str, timeout_sec: float = 30):
    """Return first window whose title matches (excluding Explorer and blocklisted apps). Wait up to timeout_sec."""
    deadline = time.monotonic() + timeout_sec
    while time.monotonic() < deadline:
        # Search by substring; pygetwindow matches if the substring appears in the title
        for w in gw.getWindowsWithTitle(title_substring):
            title = (w.title or "").strip()
            if not _title_matches(title, title_substring):
                continue
            if _is_explorer_window(title):
                continue
            if _is_explorer_process(w):
                continue
            if _is_blocked_other_app(title):
                continue
            return w
        # Also try "Arcane Demo" (with space) when looking for Arcane Demo game
        if title_substring.lower().replace(" ", "") == "arcanedemo":
            for w in gw.getWindowsWithTitle("Arcane"):
                title = (w.title or "").strip()
                if not _title_matches(title, title_substring):
                    continue
                if _is_explorer_window(title) or _is_blocked_other_app(title):
                    continue
                if _is_explorer_process(w):
                    continue
                return w
        time.sleep(0.5)
    return None


def _capture_window_printwindow(window):
    """Capture window content via PrintWindow (works when window is in background). Returns PIL Image or None."""
    if not _HAS_PYWIN32:
        return None
    hwnd = getattr(window, "_hWnd", None)
    if hwnd is None:
        return None
    try:
        left, top, right, bottom = win32gui.GetWindowRect(hwnd)
        w = max(1, right - left)
        h = max(1, bottom - top)
        hwnd_dc = win32gui.GetWindowDC(hwnd)
        mfc_dc = win32ui.CreateDCFromHandle(hwnd_dc)
        save_dc = mfc_dc.CreateCompatibleDC()
        bitmap = win32ui.CreateBitmap()
        bitmap.CreateCompatibleBitmap(mfc_dc, w, h)
        save_dc.SelectObject(bitmap)
        windll.user32.PrintWindow(hwnd, save_dc.GetSafeHdc(), 0)
        bmpinfo = bitmap.GetInfo()
        bmpstr = bitmap.GetBitmapBits(True)
        win32gui.ReleaseDC(hwnd, hwnd_dc)
        mfc_dc.DeleteDC()
        save_dc.DeleteDC()
        # BGRX -> RGB for PIL
        from PIL import Image
        img = Image.frombuffer(
            "RGB", (bmpinfo["bmWidth"], bmpinfo["bmHeight"]), bmpstr, "raw", "BGRX", 0, 1
        )
        return img
    except Exception:
        return None


def capture_window_region(window):
    """Capture the window: by content (PrintWindow) when possible, else by screen region.
    Using PrintWindow lets you use the computer while the loop runs—we capture the game
    window even if another window is on top."""
    img = _capture_window_printwindow(window)
    if img is not None:
        return img
    print("(Capturing by screen region; install pywin32 to capture game when in background.)", file=sys.stderr)
    left = getattr(window, "left", 0)
    top = getattr(window, "top", 0)
    width = max(1, getattr(window, "width", 800))
    height = max(1, getattr(window, "height", 600))
    return pyautogui.screenshot(region=(left, top, width, height))


def parse_keys(keys_spec: str):
    """Parse --keys "w:2,s:0.5,space" into list of (key, duration_sec). duration 0 = tap."""
    if not keys_spec.strip():
        return []
    out = []
    for part in keys_spec.split(","):
        part = part.strip()
        if ":" in part:
            k, d = part.rsplit(":", 1)
            out.append((k.strip().lower(), float(d)))
        else:
            out.append((part.strip().lower(), 0.0))
    return out


def send_keys(keys_list: list[tuple[str, float]], delay_between: float = 0.1):
    """Send key sequence. (key, 0) = tap; (key, sec) = hold for sec."""
    for key, duration in keys_list:
        if duration and duration > 0:
            pyautogui.keyDown(key)
            time.sleep(duration)
            pyautogui.keyUp(key)
        else:
            pyautogui.press(key)
        time.sleep(delay_between)


def move_mouse_relative(dx: int, dy: int, window=None):
    """Move mouse relative (for camera look). Optionally center on window first."""
    if window is not None:
        try:
            cx = getattr(window, "left", 0) + getattr(window, "width", 800) // 2
            cy = getattr(window, "top", 0) + getattr(window, "height", 600) // 2
            pyautogui.moveTo(cx, cy, duration=0.05)
        except Exception:
            pass
    pyautogui.moveRel(dx, dy, duration=0.05)
    time.sleep(0.05)


def _is_input_step(step: tuple) -> bool:
    """True if this step uses keyboard or mouse (so user should not touch them)."""
    return step[0] in ("play", "key", "mouse")


def _block_user_input(block: bool) -> None:
    """Block or unblock all keyboard and mouse input (Windows only). So you can use the PC until we need it."""
    if sys.platform != "win32":
        return
    try:
        ctypes.windll.user32.BlockInput(1 if block else 0)
    except Exception:
        pass


def _ensure_window_focused(window, delay_after: float = 0.5) -> None:
    """Activate the target window so the next key/mouse input goes to it, not to whatever is focused now.
    On Windows, SetForegroundWindow often fails when another app (e.g. Albion) has focus. We minimize then
    restore so the OS brings our window to the front, then call activate() and wait."""
    if sys.platform == "win32":
        try:
            hwnd = getattr(window, "_hWnd", None)
            if hwnd is not None:
                # Minimize then restore: Windows usually brings a restored window to the foreground
                # even when another process had focus (unlike SetForegroundWindow which is restricted).
                ctypes.windll.user32.ShowWindow(hwnd, 6)   # SW_MINIMIZE
                time.sleep(0.25)
                ctypes.windll.user32.ShowWindow(hwnd, 9)   # SW_RESTORE
                time.sleep(0.25)
        except Exception:
            pass
    try:
        window.activate()
        time.sleep(delay_after)
    except Exception:
        pass


def _minimize_window(window) -> None:
    """Minimize the window so it goes to the taskbar and the user's other window (e.g. Albion) comes back."""
    try:
        window.minimize()
        time.sleep(0.2)
    except Exception:
        pass


def _beep_input_cue(kind: str, silent: bool = False) -> None:
    """Play an audible cue so the user knows when to keep hands off (kind='start') or can use again (kind='done')."""
    if kind == "start":
        msg = ">>> Script is using keyboard/mouse now — please don't touch them <<<"
    else:
        msg = ">>> Script done with keyboard/mouse — you can use the PC again <<<"
    if not silent:
        try:
            import winsound
            if kind == "start":
                winsound.Beep(880, 200)
                time.sleep(0.1)
                winsound.Beep(880, 200)
            else:
                winsound.Beep(440, 400)
        except Exception:
            pass
    print(msg, file=sys.stderr)


def run_sequence(
    steps: list[tuple],
    window,
    output_dir: Path,
    capture_region_fn,
    beep_cues: bool = False,
    block_input: bool = False,
    minimize_after_input: bool = False,
) -> list[Path]:
    """Execute sequence steps; return list of screenshot paths taken at each 'capture'.
    By default no beeps, no input blocking, no focus/minimize (fast iteration)."""
    input_indices = [i for i, s in enumerate(steps) if _is_input_step(s)]
    first_input = input_indices[0] if input_indices else -1
    last_input = input_indices[-1] if input_indices else -1

    # Disable PyAutoGUI fail-safe during sequence so moving to window center + relative doesn't trigger (window may be near corner).
    old_failsafe = pyautogui.FAILSAFE
    try:
        pyautogui.FAILSAFE = False
        return _run_sequence_impl(
            steps, window, output_dir, capture_region_fn,
            beep_cues, block_input, minimize_after_input,
            first_input, last_input,
        )
    finally:
        pyautogui.FAILSAFE = old_failsafe


def _run_sequence_impl(
    steps: list[tuple],
    window,
    output_dir: Path,
    capture_region_fn,
    beep_cues: bool,
    block_input: bool,
    minimize_after_input: bool,
    first_input: int,
    last_input: int,
) -> list[Path]:
    captured = []
    capture_index = 0
    for i, step in enumerate(steps):
        if i == first_input and beep_cues:
            _beep_input_cue("start", silent=False)
        if step[0] == "wait":
            _, sec = step
            time.sleep(float(sec))
        elif step[0] == "play":
            if block_input:
                _block_user_input(True)
            try:
                # Unreal Editor: Alt+P starts Play In Editor (PIE). Viewport already focused by _ensure_window_focused.
                # Click center of window (viewport area) so editor responds to shortcut
                try:
                    x = getattr(window, "left", 0) + max(1, getattr(window, "width", 800)) // 2
                    y = getattr(window, "top", 0) + max(1, getattr(window, "height", 600)) // 2
                    pyautogui.click(x, y)
                    time.sleep(0.3)
                except Exception:
                    pass
                # Send Alt+P with explicit key sequence (more reliable than hotkey on some setups)
                pyautogui.keyDown("alt")
                time.sleep(0.05)
                pyautogui.keyDown("p")
                time.sleep(0.1)
                pyautogui.keyUp("p")
                time.sleep(0.05)
                pyautogui.keyUp("alt")
                time.sleep(0.5)
            finally:
                if block_input:
                    _block_user_input(False)
                if minimize_after_input:
                    _minimize_window(window)
        elif step[0] == "key":
            if block_input:
                _block_user_input(True)
            try:
                _, key, duration = step
                if duration and duration > 0:
                    pyautogui.keyDown(key)
                    time.sleep(duration)
                    pyautogui.keyUp(key)
                else:
                    pyautogui.press(key)
                time.sleep(0.1)
            finally:
                if block_input:
                    _block_user_input(False)
                if minimize_after_input:
                    _minimize_window(window)
        elif step[0] == "mouse":
            if block_input:
                _block_user_input(True)
            try:
                _, dx, dy = step
                move_mouse_relative(int(dx), int(dy), window)
            finally:
                if block_input:
                    _block_user_input(False)
                if minimize_after_input:
                    _minimize_window(window)
        elif step[0] == "capture":
            capture_index += 1
            img = capture_region_fn(window)
            path = output_dir / f"screenshot_{capture_index}.png"
            img.save(str(path))
            captured.append(path)
            print(f"Screenshot: {path}")
            time.sleep(0.2)
            if minimize_after_input:
                _minimize_window(window)
        if i == last_input and _is_input_step(step) and beep_cues:
            _beep_input_cue("done", silent=False)
    return captured


def tail_file(path: Path, n: int = LOG_TAIL_LINES) -> str:
    """Last n lines of file. Returns '' if file missing or unreadable."""
    if not path or not path.exists():
        return ""
    try:
        with open(path, "r", encoding="utf-8", errors="replace") as f:
            lines = f.readlines()
        return "".join(lines[-n:])
    except Exception:
        return ""


def main():
    ap = argparse.ArgumentParser(description="Capture game window screenshot and log for AI verification.")
    ap.add_argument("--exe", type=str, default="", help="Path to game .exe (standalone).")
    ap.add_argument("--launch", action="store_true", help="Launch --exe before waiting for window.")
    ap.add_argument("--close-after", action="store_true", help="Close the game process after capture (only if we launched it).")
    ap.add_argument("--window-title", type=str, default=DEFAULT_WINDOW_TITLE, help="Window title substring to find (default: ArcaneDemo).")
    ap.add_argument("--wait", type=float, default=15.0, help="Seconds to wait for window to appear (default 15).")
    ap.add_argument("--keys", type=str, default="", help="Key sequence (legacy): key1:duration_sec,key2,... e.g. 'w:2,s:0.5,space'.")
    ap.add_argument("--sequence", type=str, default="", help="Interaction sequence: wait:N, key:duration, mouse:dx:dy, capture. e.g. 'w:2,mouse:100:0,capture'.")
    ap.add_argument("--initial-delay", type=float, default=0.0, help="Seconds to wait after focusing window (e.g. for level load). Default 0.")
    ap.add_argument("--after-keys-delay", type=float, default=0.5, help="Seconds to wait after keys before screenshot (legacy).")
    ap.add_argument("--log", type=str, default="", help="Path to game log file. If empty and --exe set, inferred next to exe.")
    ap.add_argument("--output-dir", type=Path, default=DEFAULT_OUTPUT_DIR, help="Directory for screenshot(s) and game_log.txt.")
    ap.add_argument("--no-screenshot", action="store_true", help="Skip screenshot (only write log).")
    ap.add_argument("--beep", action="store_true", help="Beep when script uses keyboard/mouse (off by default for fast iteration).")
    ap.add_argument("--block-input", action="store_true", help="Block keyboard/mouse during input steps (off by default).")
    ap.add_argument("--minimize-after-input", action="store_true", help="Minimize the game after each input/capture step (off by default).")
    args = ap.parse_args()

    args.output_dir = args.output_dir.resolve()
    args.output_dir.mkdir(parents=True, exist_ok=True)

    process = None
    if args.launch and args.exe:
        exe_path = Path(args.exe).resolve()
        if not exe_path.exists():
            print(f"Exe not found: {exe_path}", file=sys.stderr)
            sys.exit(1)
        cmd = [str(exe_path), "-windowed", "-log"]
        process = subprocess.Popen(
            cmd,
            cwd=str(exe_path.parent),
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
        print(f"Launched: {' '.join(cmd)}")
        time.sleep(2)

    window = find_window(args.window_title, timeout_sec=args.wait)
    if not window:
        print(f"No window with title containing '{args.window_title}' found within {args.wait}s.", file=sys.stderr)
        if process and args.close_after:
            process.terminate()
        sys.exit(1)

    # Wait for level to load (so we don't capture a black screen)
    if args.initial_delay and args.initial_delay > 0:
        print(f"Waiting {args.initial_delay}s for level load...")
        time.sleep(args.initial_delay)

    captured_paths = []
    if args.sequence.strip():
        steps = parse_sequence(args.sequence)
        if steps:
            print(f"Running sequence ({len(steps)} steps)...")
            captured_paths = run_sequence(
                steps, window, args.output_dir, capture_window_region,
                beep_cues=args.beep,
                block_input=args.block_input,
                minimize_after_input=args.minimize_after_input,
            )
            # If sequence had no capture, take one at the end
            if not captured_paths and not args.no_screenshot:
                img = capture_window_region(window)
                out_png = args.output_dir / "screenshot.png"
                img.save(str(out_png))
                captured_paths = [out_png]
                print(f"Screenshot: {out_png}")
            elif captured_paths:
                # Also write last capture as screenshot.png for backward compat
                shutil.copy(str(captured_paths[-1]), str(args.output_dir / "screenshot.png"))
        else:
            if not args.no_screenshot:
                img = capture_window_region(window)
                out_png = args.output_dir / "screenshot.png"
                img.save(str(out_png))
                captured_paths = [out_png]
                print(f"Screenshot: {out_png}")
    else:
        # Legacy: --keys then one screenshot
        if args.keys:
            send_keys(parse_keys(args.keys))
            time.sleep(args.after_keys_delay)
        if not args.no_screenshot:
            img = capture_window_region(window)
            out_png = args.output_dir / "screenshot.png"
            img.save(str(out_png))
            captured_paths = [out_png]
            print(f"Screenshot: {out_png}")

    # Log path: if not set and we have exe, use Saved/Logs/ProjectName.log next to exe
    log_path = None
    if args.log:
        log_path = Path(args.log)
    elif args.exe:
        exe_dir = Path(args.exe).resolve().parent
        # Packaged: .../ArcaneDemo/Binaries/Win64/ArcaneDemo.exe -> .../ArcaneDemo/Saved/Logs/
        for candidate in [
            exe_dir / "Saved" / "Logs" / "ArcaneDemo.log",
            exe_dir.parent / "Saved" / "Logs" / "ArcaneDemo.log",
            exe_dir.parent.parent / "Saved" / "Logs" / "ArcaneDemo.log",
        ]:
            if candidate.exists():
                log_path = candidate
                break
        if not log_path:
            log_path = exe_dir.parent / "Saved" / "Logs" / "ArcaneDemo.log"
    # Editor / PIE: repo Unreal project Saved/Logs
    if not log_path or not log_path.exists():
        editor_log = REPO_ROOT / "Unreal" / "ArcaneDemo" / "Saved" / "Logs" / "ArcaneDemo.log"
        if editor_log.exists():
            log_path = editor_log

    log_text = tail_file(log_path, LOG_TAIL_LINES) if log_path else ""
    log_out = args.output_dir / "game_log.txt"
    with open(log_out, "w", encoding="utf-8") as f:
        f.write(log_text)
    print(f"Log tail: {log_out}")

    if process and args.close_after:
        process.terminate()
        print("Game process terminated.")

    out_log = args.output_dir / "game_log.txt"
    if captured_paths:
        print("Done. AI can read:", " ".join(str(p) for p in captured_paths), out_log)
    else:
        print("Done. AI can read:", out_log)


if __name__ == "__main__":
    main()
