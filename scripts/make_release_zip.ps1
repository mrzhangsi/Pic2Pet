# Package dist\ into a release zip, for manual upload to GitHub Releases (no CI).
#
# Usage:
#   powershell -ExecutionPolicy Bypass -File scripts\make_release_zip.ps1
#   powershell -ExecutionPolicy Bypass -File scripts\make_release_zip.ps1 -Version v1.0.1
#
# Prerequisite: dist\ must already be built & deployed, i.e.
#   powershell -ExecutionPolicy Bypass -File temp\deploy.ps1
param(
  [string]$Version = 'v1.0.0'
)

$ErrorActionPreference = 'Stop'

$root = 'E:\project\Pic2Pet'
$dist = Join-Path $root 'dist'
$exe  = Join-Path $dist 'Pic2Pet.exe'

if (-not (Test-Path $exe)) {
  Write-Host "[ERROR] dist not built: $exe"
  Write-Host "        run first: powershell -ExecutionPolicy Bypass -File temp\deploy.ps1"
  exit 1
}

if (-not (Test-Path (Join-Path $dist 'Qt6Core.dll'))) {
  Write-Host "[WARN] Qt6Core.dll missing in dist -- windeployqt did not run?"
}
if (-not (Test-Path (Join-Path $dist 'platforms\qwindows.dll'))) {
  Write-Host "[WARN] platforms\qwindows.dll missing -- app will not start on a clean machine"
}

$zip = Join-Path $root ("Pic2Pet-$Version.zip")
if (Test-Path $zip) { Remove-Item $zip -Force }

# -Path "dist\*" puts the exe at the TOP level of the zip.
# Do NOT zip the dist folder itself, or users get an extra nesting level.
Compress-Archive -Path (Join-Path $dist '*') -DestinationPath $zip -Force

$f = Get-Item $zip
Write-Host ("[OK] " + $f.FullName)
Write-Host ("     " + [math]::Round($f.Length / 1MB, 1) + " MB  (exe built " + (Get-Item $exe).LastWriteTime + ")")

Add-Type -AssemblyName System.IO.Compression.FileSystem
$z = [IO.Compression.ZipFile]::OpenRead($zip)
Write-Host "     top-level entries:"
$z.Entries | Where-Object { $_.FullName -notmatch '/' } | Sort-Object FullName | ForEach-Object {
  Write-Host ("       " + $_.FullName)
}
$z.Dispose()

Write-Host ""
Write-Host "Upload to GitHub Releases:"
Write-Host "  web : https://github.com/<your-user>/Pic2Pet/releases/new  -- pick the tag, attach the zip, Publish"
Write-Host "  gh  : gh release create $Version <zip-path> --title `"Pic2Pet $Version`" --notes `"...`""
Write-Host ""
Write-Host "NOTE: publishing a release triggers the 'Release build' workflow."
Write-Host "      Disable it first (Actions -> Release build -> ... -> Disable workflow)"
Write-Host "      if you do not want CI to attach/overwrite its own zip."
