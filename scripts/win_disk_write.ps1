# Write a raw image to a removable disk (e.g. an SD card in a PC reader), then read it back and compare SHA-256.
# Needs an elevated (administrator) PowerShell. Erases the target disk.
# usage: powershell -File win_disk_write.ps1 -Disk 1 -ExpectSize 31914983424 -Image <file.img>
param([int]$Disk, [long]$ExpectSize, [string]$Image)
$ErrorActionPreference = 'Stop'
$d = Get-Disk -Number $Disk
Write-Host ("Disk {0}: {1} {2} B bus={3} system={4} boot={5}" -f $d.Number, $d.FriendlyName, $d.Size, $d.BusType, $d.IsSystem, $d.IsBoot)
if ($d.BusType -notin 'SD','USB','MMC') { throw "refusing: disk $Disk is not an SD/USB/MMC disk" }
if ($d.IsSystem -or $d.IsBoot) { throw "refusing: disk $Disk is a system/boot disk" }
if ($d.Size -ne $ExpectSize) { throw "refusing: disk $Disk size $($d.Size) != expected $ExpectSize" }
$len = (Get-Item $Image).Length
if ($len -gt $d.Size) { throw "refusing: image $len B is larger than disk" }
if ($len % 512) { throw "refusing: image size is not a multiple of 512" }

Set-Disk -Number $Disk -IsReadOnly $false
Set-Disk -Number $Disk -IsOffline $true
Write-Host ("{0} offline, writing {1} B" -f (Get-Date -Format 'HH:mm:ss'), $len)
$src = [System.IO.File]::OpenRead($Image)
$dst = New-Object System.IO.FileStream ("\\.\PhysicalDrive$Disk"), 'Open', 'ReadWrite', 'ReadWrite'
$buf = New-Object byte[] (4MB)
$done = [long]0
while ($done -lt $len) {
  $n = $src.Read($buf, 0, $buf.Length)
  if ($n -le 0) { break }
  if ($n % 512) { [Array]::Clear($buf, $n, 512 - ($n % 512)); $n += 512 - ($n % 512) }
  $dst.Write($buf, 0, $n); $done += $n
  if (($done % 512MB) -eq 0) { Write-Host ("{0} wrote {1} MiB" -f (Get-Date -Format 'HH:mm:ss'), ($done / 1MB)) }
}
$dst.Flush(); $dst.Close(); $src.Close()
Write-Host ("{0} write done {1} B" -f (Get-Date -Format 'HH:mm:ss'), $done)

# read back exactly the image length and compare hashes
$sha = [System.Security.Cryptography.SHA256]::Create()
$rd = New-Object System.IO.FileStream ("\\.\PhysicalDrive$Disk"), 'Open', 'Read', 'ReadWrite'
$left = $len
while ($left -gt 0) {
  $n = $rd.Read($buf, 0, [int][math]::Min([long]$buf.Length, $left))
  if ($n -le 0) { break }
  [void]$sha.TransformBlock($buf, 0, $n, $null, 0); $left -= $n
}
[void]$sha.TransformFinalBlock($buf, 0, 0); $rd.Close()
$cardHash = ($sha.Hash | ForEach-Object { $_.ToString('x2') }) -join ''
$imgHash = (Get-FileHash -Algorithm SHA256 $Image).Hash.ToLower()
Write-Host ("{0} readback sha256 {1}" -f (Get-Date -Format 'HH:mm:ss'), $cardHash)
Write-Host ("image sha256    {0}" -f $imgHash)
if ($cardHash -eq $imgHash) { Write-Host "VERIFY MATCH" } else { Write-Host "VERIFY MISMATCH" }
Set-Disk -Number $Disk -IsOffline $false
Write-Host ("{0} disk online" -f (Get-Date -Format 'HH:mm:ss'))
