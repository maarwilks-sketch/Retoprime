param(
    [Parameter(Mandatory=$true)][string]$Installed,
    [Parameter(Mandatory=$true)][string]$AppDirectory
)
$ErrorActionPreference = 'Stop'
$Installed = (Resolve-Path $Installed).Path
$AppDirectory = (Resolve-Path $AppDirectory).Path
# The minimal qtbase[widgets] manifest does not include windeployqt.
# Copy the release runtime and plugins explicitly from the same pinned triplet.
Get-ChildItem (Join-Path $Installed 'bin') -Filter *.dll -File |
    Copy-Item -Destination $AppDirectory -Force
$pluginRoot = Join-Path $Installed 'Qt6\plugins'
if (-not (Test-Path (Join-Path $pluginRoot 'platforms\qwindows.dll'))) {
    throw "Qt Windows platform plugin is missing from $pluginRoot"
}
$plugins = Join-Path $AppDirectory 'plugins'
New-Item -ItemType Directory -Force $plugins | Out-Null
Copy-Item (Join-Path $pluginRoot '*') $plugins -Recurse -Force
"[Paths]`nPlugins=plugins`n" | Set-Content (Join-Path $AppDirectory 'qt.conf') -Encoding ascii
if (-not $env:VCToolsRedistDir) { throw 'MSVC redistributable directory is unavailable' }
$crt = Get-ChildItem (Join-Path $env:VCToolsRedistDir 'x64') -Directory -Filter 'Microsoft.VC*.CRT' |
    Sort-Object Name | Select-Object -Last 1
if (-not $crt) { throw 'MSVC x64 runtime DLLs were not found' }
Get-ChildItem $crt.FullName -Filter *.dll -File | Copy-Item -Destination $AppDirectory -Force
$engine = Join-Path $AppDirectory 'engine'
if (Test-Path $engine) {
    Get-ChildItem $AppDirectory -Filter *.dll -File | Copy-Item -Destination $engine -Force
}
foreach ($required in @('Qt6Core.dll','Qt6Gui.dll','Qt6Widgets.dll','vcruntime140.dll','plugins\platforms\qwindows.dll')) {
    if (-not (Test-Path (Join-Path $AppDirectory $required))) { throw "Missing runtime file: $required" }
}
Write-Host 'Deployed Qt plugins, release dependency DLLs and MSVC runtime.'
