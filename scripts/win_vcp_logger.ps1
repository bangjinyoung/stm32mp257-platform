# Windows-side VCP logger: appends every line from a COM port to a file with a host timestamp.
# Used when usbipd re-attach is too slow to catch the first seconds of a boot.
# usage: powershell -File win_vcp_logger.ps1 -Port COM5 -Out <file> [-Seconds 120]
param([string]$Port, [string]$Out, [int]$Seconds = 120)
$sp = New-Object System.IO.Ports.SerialPort $Port, 115200, 'None', 8, 'One'
$sp.ReadTimeout = 100
$deadline = (Get-Date).AddSeconds($Seconds)
$pending = ''
while ((Get-Date) -lt $deadline) {
  if (-not $sp.IsOpen) {
    try { $sp.Open(); Add-Content -Path "$Out.events" -Value ("{0} open {1}" -f (Get-Date -Format 'yyyy-MM-dd HH:mm:ss.fff'), $Port) }
    catch { Start-Sleep -Milliseconds 20; continue }
  }
  # a surprise-removed ST-LINK can leave a stale handle that never throws, so also check the port still exists
  if ([System.IO.Ports.SerialPort]::GetPortNames() -notcontains $Port) {
    try { $sp.Close() } catch {}; Add-Content -Path "$Out.events" -Value ("{0} gone {1}" -f (Get-Date -Format 'yyyy-MM-dd HH:mm:ss.fff'), $Port)
    $sp = New-Object System.IO.Ports.SerialPort $Port, 115200, 'None', 8, 'One'; $sp.ReadTimeout = 100; continue
  }
  try { $pending += $sp.ReadExisting() }
  catch { try { $sp.Close() } catch {}; Add-Content -Path "$Out.events" -Value ("{0} lost {1}" -f (Get-Date -Format 'yyyy-MM-dd HH:mm:ss.fff'), $Port); continue }
  while ($pending.Contains("`n")) {
    $i = $pending.IndexOf("`n")
    $line = $pending.Substring(0, $i).TrimEnd("`r")
    $pending = $pending.Substring($i + 1)
    Add-Content -Path $Out -Value ("{0} {1}" -f (Get-Date -Format 'yyyy-MM-dd HH:mm:ss.fff'), $line)
  }
  Start-Sleep -Milliseconds 10
}
if ($sp.IsOpen) { $sp.Close() }
