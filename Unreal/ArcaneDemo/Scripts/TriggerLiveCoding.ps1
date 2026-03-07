# Sends Ctrl+Alt+F11 to the Unreal Editor window to trigger Live Coding compile.
# Use with "run on save" in Cursor/VS Code: when you save a .cpp/.h, run this script.
# Requires: Unreal Editor (with this project) running and Live Coding enabled in Editor Preferences.

Add-Type -AssemblyName System.Windows.Forms
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class Win32 {
    [DllImport("user32.dll")]
    public static extern IntPtr FindWindow(string lpClassName, string lpWindowName);
    [DllImport("user32.dll")]
    public static extern IntPtr FindWindowEx(IntPtr parent, IntPtr childAfter, string lpClassName, string lpWindowName);
    [DllImport("user32.dll")]
    public static extern bool SetForegroundWindow(IntPtr hWnd);
    [DllImport("user32.dll")]
    public static extern int SendMessage(IntPtr hWnd, int Msg, int wParam, int lParam);
    [DllImport("user32.dll")]
    public static extern void keybd_event(byte bVk, byte bScan, uint dwFlags, UIntPtr dwExtraInfo);
    public const int WM_ACTIVATE = 0x0006;
    public const byte VK_F11 = 0x7A;
    public const uint KEYEVENTF_KEYUP = 0x0002;
}
"@

$caption = "*Unreal Editor*"
$windows = Get-Process | Where-Object { $_.MainWindowTitle -like $caption } | Select-Object -First 1
if (-not $windows) {
    Write-Host "No Unreal Editor window found. Is the editor open?"
    exit 1
}
$hwnd = $windows.MainWindowHandle
[void][Win32]::SetForegroundWindow($hwnd)
Start-Sleep -Milliseconds 100
[System.Windows.Forms.SendKeys]::SendWait("^%{F11}")
Write-Host "Sent Ctrl+Alt+F11 to Unreal Editor."
exit 0
