param([string]$Proc = 'zelda',
      [string]$Action = 'walk-east',
      [int]$DurationMs = 6000,
      [string]$SnapName = 'walk')
Add-Type -AssemblyName System.Drawing -ErrorAction SilentlyContinue
$src = @"
using System;
using System.Runtime.InteropServices;
public class PM {
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
    [DllImport(`"user32.dll`")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport(`"user32.dll`")] public static extern IntPtr PostMessage(IntPtr h, uint msg, IntPtr wp, IntPtr lp);
    [DllImport(`"user32.dll`")] public static extern uint MapVirtualKey(uint c, uint t);
    [DllImport(`"user32.dll`")] public static extern bool SetForegroundWindow(IntPtr h);
    [DllImport(`"user32.dll`")] public static extern bool ShowWindow(IntPtr h, int n);
    [DllImport(`"user32.dll`")] public static extern bool BringWindowToTop(IntPtr h);
}
"@
Add-Type -TypeDefinition $src -ErrorAction SilentlyContinue
$p = Get-Process $Proc -ErrorAction SilentlyContinue
if (-not $p) { 'process not running'; exit 1 }
$hwnd = $p.MainWindowHandle
$WM_KEYDOWN = [uint32]0x0100
$WM_KEYUP   = [uint32]0x0101

function KeyDown([uint32]$vk) {
    $sc = [PM]::MapVirtualKey($vk, 0)
    # lParam: bits 0-15 repeat count (1), 16-23 scancode, 24 extended, 30 previous-state (set after first), 31 transition state (0 = down)
    $lp = ([int64]1) -bor (($sc -band 0xFF) -shl 16)
    [PM]::PostMessage($hwnd, $WM_KEYDOWN, [IntPtr]$vk, [IntPtr]$lp) | Out-Null
}
function KeyUp([uint32]$vk) {
    $sc = [PM]::MapVirtualKey($vk, 0)
    $lp = ([int64]1) -bor (($sc -band 0xFF) -shl 16) -bor 0xC0000000   # bits 30 and 31
    [PM]::PostMessage($hwnd, $WM_KEYUP, [IntPtr]$vk, [IntPtr]$lp) | Out-Null
}
function Snap([string]$name) {
    # Bring zelda to top BEFORE snapping; otherwise CopyFromScreen at zelda's
    # window rect captures whatever happens to be on top there now.
    [PM]::ShowWindow($hwnd, 9) | Out-Null
    [PM]::BringWindowToTop($hwnd) | Out-Null
    [PM]::SetForegroundWindow($hwnd) | Out-Null
    Start-Sleep -Milliseconds 250
    $r = New-Object PM+RECT
    [PM]::GetWindowRect($hwnd, [ref]$r) | Out-Null
    $w = $r.Right - $r.Left
    $h = $r.Bottom - $r.Top
    $bmp = New-Object System.Drawing.Bitmap $w, $h
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.CopyFromScreen($r.Left, $r.Top, 0, 0, $bmp.Size)
    $g.Dispose()
    $out = "$env:TEMP\$Proc-$name.png"
    $bmp.Save($out, [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
    "saved: $out"
}

$VK_D     = [uint32]0x44
$VK_RETURN = [uint32]0x0D

if ($Action -eq 'enter') {
    KeyDown $VK_RETURN
    Start-Sleep -Milliseconds 80
    KeyUp $VK_RETURN
    Start-Sleep -Milliseconds 200
} elseif ($Action -eq 'walk-east') {
    KeyDown $VK_D
    Start-Sleep -Milliseconds $DurationMs
    KeyUp $VK_D
    Start-Sleep -Milliseconds 600
    Snap $SnapName
} elseif ($Action -eq 'walk-east-pulse') {
    # Pulse KEYDOWN every 50ms instead of holding -- some apps' WndProc
    # treats a missed-WM_KEYUP as a stuck key; pulsing also keeps the
    # "Down" state through any internal timeouts the engine might have.
    $elapsed = 0
    while ($elapsed -lt $DurationMs) {
        KeyDown $VK_D
        Start-Sleep -Milliseconds 50
        $elapsed += 50
    }
    KeyUp $VK_D
    Start-Sleep -Milliseconds 600
    Snap $SnapName
} elseif ($Action -eq 'snap') {
    Snap $SnapName
} else {
    'unknown action'; exit 1
}
