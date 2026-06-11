param([Parameter(Mandatory=$true)][string]$Action,
      [int]$DurationMs = 100,
      [string]$Arg = '')
Add-Type -AssemblyName System.Drawing -ErrorAction SilentlyContinue
Add-Type -AssemblyName System.Windows.Forms -ErrorAction SilentlyContinue
$src = @"
using System;
using System.Runtime.InteropServices;
public class WE {
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
    [DllImport(`"user32.dll`")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport(`"user32.dll`")] public static extern bool SetForegroundWindow(IntPtr h);
    [DllImport(`"user32.dll`")] public static extern bool ShowWindow(IntPtr h, int n);
    [DllImport(`"user32.dll`")] public static extern bool SetCursorPos(int x, int y);
    [DllImport(`"user32.dll`")] public static extern uint MapVirtualKey(uint c, uint t);
    [StructLayout(LayoutKind.Sequential)] public struct MI { public int dx, dy; public uint mouseData, dwFlags, time; public IntPtr ex; }
    [StructLayout(LayoutKind.Sequential)] public struct KI { public ushort vk, sc; public uint fl, ti; public IntPtr ex; }
    [StructLayout(LayoutKind.Sequential)] public struct HI { public uint m; public ushort l, h; }
    [StructLayout(LayoutKind.Explicit)] public struct UN {
        [FieldOffset(0)] public MI mi;
        [FieldOffset(0)] public KI ki;
        [FieldOffset(0)] public HI hi;
    }
    [StructLayout(LayoutKind.Sequential)] public struct IN { public uint type; public UN u; }
    [DllImport(`"user32.dll`")] public static extern uint SendInput(uint n, IN[] a, int s);
}
"@
Add-Type -TypeDefinition $src -ErrorAction SilentlyContinue
$p = Get-Process tileset-editor -ErrorAction SilentlyContinue
if (-not $p) { 'tileset-editor not running'; exit 1 }
[WE]::ShowWindow($p.MainWindowHandle, 9) | Out-Null
[WE]::SetForegroundWindow($p.MainWindowHandle) | Out-Null
Start-Sleep -Milliseconds 350

function ClickAt([int]$sx, [int]$sy, [bool]$right) {
    [WE]::SetCursorPos($sx, $sy) | Out-Null
    Start-Sleep -Milliseconds 80
    $d = New-Object WE+IN
    $d.type = 0  # MOUSE
    if ($right) { $d.u.mi.dwFlags = 8 } else { $d.u.mi.dwFlags = 2 }  # RIGHTDOWN / LEFTDOWN
    [WE]::SendInput(1, @($d), 40) | Out-Null
    Start-Sleep -Milliseconds 60
    $u = New-Object WE+IN
    $u.type = 0
    if ($right) { $u.u.mi.dwFlags = 16 } else { $u.u.mi.dwFlags = 4 }  # RIGHTUP / LEFTUP
    [WE]::SendInput(1, @($u), 40) | Out-Null
}
function Snap([string]$name) {
    $r = New-Object WE+RECT
    [WE]::GetWindowRect($p.MainWindowHandle, [ref]$r) | Out-Null
    $w = $r.Right - $r.Left
    $h = $r.Bottom - $r.Top
    $bmp = New-Object System.Drawing.Bitmap $w, $h
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.CopyFromScreen($r.Left, $r.Top, 0, 0, $bmp.Size)
    $g.Dispose()
    $out = "$env:TEMP\editor-$name.png"
    $bmp.Save($out, [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
    "saved: $out"
}

switch ($Action) {
    'snap' { Snap $Arg }
    'rmb' {
        # $Arg = "X,Y" (window-local px). Convert to screen coords.
        $bits = $Arg.Split(',')
        $r = New-Object WE+RECT
        [WE]::GetWindowRect($p.MainWindowHandle, [ref]$r) | Out-Null
        $sx = $r.Left + [int]$bits[0]
        $sy = $r.Top + [int]$bits[1]
        ClickAt $sx $sy $true
        Start-Sleep -Milliseconds 200
    }
    'lmb' {
        $bits = $Arg.Split(',')
        $r = New-Object WE+RECT
        [WE]::GetWindowRect($p.MainWindowHandle, [ref]$r) | Out-Null
        $sx = $r.Left + [int]$bits[0]
        $sy = $r.Top + [int]$bits[1]
        ClickAt $sx $sy $false
        Start-Sleep -Milliseconds 200
    }
    'move' {
        $bits = $Arg.Split(',')
        $r = New-Object WE+RECT
        [WE]::GetWindowRect($p.MainWindowHandle, [ref]$r) | Out-Null
        $sx = $r.Left + [int]$bits[0]
        $sy = $r.Top + [int]$bits[1]
        [WE]::SetCursorPos($sx, $sy) | Out-Null
        Start-Sleep -Milliseconds 100
    }
    'keys' {
        # Type literal text via SendKeys.
        [System.Windows.Forms.SendKeys]::SendWait($Arg)
        Start-Sleep -Milliseconds 200
    }
    default { "unknown action: $Action"; exit 1 }
}
