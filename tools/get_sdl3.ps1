# Downloads the SDL3 MinGW development package into engine\third_party\SDL3.
$ErrorActionPreference = 'Stop'
$Version = '3.4.18'
$Dest = Join-Path $PSScriptRoot '..\engine\third_party'
$Archive = Join-Path $Dest 'sdl.zip'
Invoke-WebRequest "https://github.com/libsdl-org/SDL/releases/download/release-$Version/SDL3-devel-$Version-mingw.zip" -OutFile $Archive
Expand-Archive $Archive -DestinationPath $Dest -Force
Remove-Item $Archive
$Target = Join-Path $Dest 'SDL3'
if (Test-Path $Target) { Remove-Item $Target -Recurse -Force }
Rename-Item (Join-Path $Dest "SDL3-$Version") 'SDL3'
Write-Host "SDL3 $Version ready in engine\third_party\SDL3"
