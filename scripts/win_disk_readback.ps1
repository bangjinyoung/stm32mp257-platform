# Read the first N bytes of a physical disk (e.g. an SD card in a PC reader) into a file. Read only.
# Needs an elevated (administrator) PowerShell.
# usage: powershell -File win_disk_readback.ps1 -Disk 3 -Bytes 20971520 -Out <file>
param([int]$Disk, [long]$Bytes, [string]$Out)
$d = Get-Disk -Number $Disk
Write-Host ("Disk {0}: {1} {2} GB bus={3} removable={4}" -f $d.Number, $d.FriendlyName, [math]::Round($d.Size/1GB,1), $d.BusType, ($d.BusType -in 'SD','USB','MMC'))
if ($d.BusType -notin 'SD','USB','MMC') { throw "refusing: disk $Disk is not an SD/USB/MMC disk" }
$src = New-Object System.IO.FileStream ("\\.\PhysicalDrive$Disk"), 'Open', 'Read', 'ReadWrite'
$dst = [System.IO.File]::Create($Out)
$buf = New-Object byte[] (1MB)
$left = $Bytes
while ($left -gt 0) {
  $n = $src.Read($buf, 0, [int][math]::Min([long]$buf.Length, $left))
  if ($n -le 0) { break }
  $dst.Write($buf, 0, $n); $left -= $n
  if ((($Bytes - $left) % 512MB) -eq 0) { Write-Host ("{0} {1} MiB" -f (Get-Date -Format 'HH:mm:ss'), (($Bytes - $left) / 1MB)) }
}
$dst.Close(); $src.Close()
Write-Host ("read {0} bytes to {1}" -f ($Bytes - $left), $Out)
