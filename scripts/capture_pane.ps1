# Captures the Windows Terminal window with PrintWindow, so windows on top of it are not in the
# image, then crops to the given rectangle of terminal cells.
param([string]$Out, [int]$Cols, [int]$Rows, [int]$Left, [int]$Top, [int]$Width, [int]$Height,
      [int]$PadLeft = 23, [int]$PadRight = 47, [int]$PadTop = 68, [int]$PadBottom = 27)
Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class W {
  [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint flags);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
}
"@
Add-Type -AssemblyName System.Drawing
[W]::SetProcessDPIAware() | Out-Null
$p = Get-Process -Name WindowsTerminal -ErrorAction Stop | Where-Object { $_.MainWindowHandle -ne 0 } | Select-Object -First 1
$r = New-Object W+RECT
[W]::GetWindowRect($p.MainWindowHandle, [ref]$r) | Out-Null
$w = $r.R - $r.L; $h = $r.B - $r.T
$full = New-Object System.Drawing.Bitmap $w, $h
$g = [System.Drawing.Graphics]::FromImage($full)
$hdc = $g.GetHdc()
$ok = [W]::PrintWindow($p.MainWindowHandle, $hdc, 2)
$g.ReleaseHdc($hdc)
if (-not $ok) { throw "PrintWindow failed" }
# Pad* are the Windows Terminal tab bar, padding and scrollbar in pixels, measured at this display scale.
$cw = ($w - $PadLeft - $PadRight) / $Cols
$ch = ($h - $PadTop - $PadBottom) / $Rows
$x = [int]($PadLeft + ($Left - 0.5) * $cw); $y = [int]($PadTop + $Top * $ch)
$cw2 = [int]([Math]::Min(($Width + 0.5) * $cw, $w - $x)); $ch2 = [int]([Math]::Min($Height * $ch, $h - $y))
$crop = $full.Clone((New-Object System.Drawing.Rectangle $x, $y, $cw2, $ch2), $full.PixelFormat)
$crop.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png)
"{0}x{1} from window {2}x{3}" -f $cw2, $ch2, $w, $h
