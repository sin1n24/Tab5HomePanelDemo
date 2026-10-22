# Build both chip-revision variants and copy the merged images into docs\firmware for the web flasher.
# Run from PowerShell (not Git Bash: idf_tools.py refuses MSYS). ASCII only.
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
Set-Location -LiteralPath $root
uvx --python 3.13 --from platformio==6.1.19 pio run -e tab5 -e tab5_rev3
if ($LASTEXITCODE -ne 0) { throw "build failed" }
$out = Join-Path $root 'docs\firmware'
New-Item -ItemType Directory -Force $out | Out-Null
Copy-Item '.pio\build\tab5\firmware.factory.bin' (Join-Path $out 'tab5-home-panel-demo.bin') -Force
Copy-Item '.pio\build\tab5_rev3\firmware.factory.bin' (Join-Path $out 'tab5-home-panel-demo-rev3.bin') -Force
Get-ChildItem $out | Select-Object Name, Length
