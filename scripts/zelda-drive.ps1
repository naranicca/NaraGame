param([Parameter(Mandatory=$true)][string]$Action,
      [int]$DurationMs = 1000,
      [string]$ScreenshotName = '')
Add-Type -AssemblyName System.Drawing -ErrorAction SilentlyContinue
$src = @"
using System;
using System.Runtime.InteropServices;
public class W {
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
    [DllImport(`"user32.dll`")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport(`"user32.dll`")] public static extern bool SetForegroundWindow(IntPtr h);
    [DllImport(`"user32.dll`")] public static extern bool ShowWindow(IntPtr h, int n);
    [DllImport(`"user32.dll`")] public static extern uint MapVirtualKey(uint c, uint t);
    [StructLayout(LayoutKind.Sequential)] public struct MI { public int dx, dy; public uint d, f, t; public IntPtr e; }
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
$p = Get-Process zelda -ErrorAction SilentlyContinue
if (-not $p) { "zelda not running"; exit 1 }
[W]::ShowWindow($p.MainWindowHandle, 9) | Out-Null
[W]::SetForegroundWindow($p.MainWindowHandle) | Out-Null
Start-Sleep -Milliseconds 350

function Hold([uint16]$vk, [int]$ms) {
    # VK-based down/up. SCANCODE-only paths skip WM_KEYDOWN; some Windows
    # games (and our engine) need the VK form. Re-focusing during the hold
    # actively breaks input on Win11 -- SetForegroundWindow calls outside
    # a user-input window get demoted to FlashWindow, and they nudge focus
    # state in ways that lose the held key. Don't re-focus inside Hold.
    $sc = [uint16]([W]::MapVirtualKey([uint32]$vk, 0))
    $d = New-Object W+IN
    $d.type = 1
    $d.u.ki.vk = $vk
    $d.u.ki.sc = $sc
    $d.u.ki.fl = 8     # SCANCODE: this is what the engine listens for
    [W]::SendInput(1, @($d), 40) | Out-Null
    Start-Sleep -Milliseconds $ms
    $u = New-Object W+IN
    $u.type = 1
    $u.u.ki.vk = $vk
    $u.u.ki.sc = $sc
    $u.u.ki.fl = 10    # SCANCODE | KEYUP
    [W]::SendInput(1, @($u), 40) | Out-Null
}
function Snap([string]$name) {
    $r = New-Object W+RECT
    [W]::GetWindowRect($p.MainWindowHandle, [ref]$r) | Out-Null
    $w = $r.Right - $r.Left
    $h = $r.Bottom - $r.Top
    $bmp = New-Object System.Drawing.Bitmap $w, $h
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.CopyFromScreen($r.Left, $r.Top, 0, 0, $bmp.Size)
    $g.Dispose()
    $out = "$env:TEMP\zelda-$name.png"
    $bmp.Save($out, [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
    "saved: $out"
}

$vkMap = @{
    'W' = 0x57; 'A' = 0x41; 'S' = 0x53; 'D' = 0x44
    'Up' = 0x26; 'Down' = 0x28; 'Left' = 0x25; 'Right' = 0x27
    'Space' = 0x20; 'Enter' = 0x0D; 'Esc' = 0x1B
}

switch ($Action) {
    'hold' {
        # $ScreenshotName carries the key name (e.g. 'D').
        $vk = [uint16]$vkMap[$ScreenshotName]
        if (-not $vk) { "unknown key $ScreenshotName"; exit 1 }
        Hold $vk $DurationMs
        Start-Sleep -Milliseconds 200
    }
    'tap' {
        # SendKeys is more reliable for instantaneous taps; it goes via the
        # focused-window message queue rather than the input stream.
        Add-Type -AssemblyName System.Windows.Forms
        $skMap = @{
            'Enter' = '{ENTER}'; 'Space' = ' '; 'Esc' = '{ESC}'
            'W' = 'w'; 'A' = 'a'; 'S' = 's'; 'D' = 'd'
            'Up' = '{UP}'; 'Down' = '{DOWN}'; 'Left' = '{LEFT}'; 'Right' = '{RIGHT}'
            'Num1' = '1'; 'Num2' = '2'; 'Num3' = '3'; 'Num4' = '4'
        }
        $sk = $skMap[$ScreenshotName]
        if (-not $sk) { "unknown key $ScreenshotName"; exit 1 }
        [System.Windows.Forms.SendKeys]::SendWait($sk)
        Start-Sleep -Milliseconds 200
    }
    'snap' {
        Snap $ScreenshotName
    }
    'tapAndSnap' {
        # $DurationMs = key tap hold time, $ScreenshotName = "vk:snapname"
        $bits = $ScreenshotName.Split(':')
        $vk = [uint16]$vkMap[$bits[0]]
        if (-not $vk) { "unknown key $($bits[0])"; exit 1 }
        Hold $vk $DurationMs
        Start-Sleep -Milliseconds 300
        Snap $bits[1]
    }
    default { "unknown action: $Action"; exit 1 }
}
