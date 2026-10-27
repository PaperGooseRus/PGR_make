# Optional GTK4 runtime bundler for UCRT64 builds. Run in PowerShell:
# powershell -ExecutionPolicy Bypass -File tools\bundle_gtk_runtime.ps1
# Requires MSYS2 UCRT64's objdump.exe. Review GTK/GLib dependency licenses.
param(
    [string]$Ucrt = 'C:\msys64\ucrt64',
    [string]$Deploy = ''
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
if (-not $Deploy) { $Deploy = Join-Path $root 'deploy\PGR' }
$exe = Join-Path $Deploy 'PGR_Make_GUI.exe'
$cli = Join-Path $Deploy 'PGR_Make.exe'
$objdump = Join-Path $Ucrt 'bin\objdump.exe'
$bin = Join-Path $Ucrt 'bin'
$dest = Join-Path $Deploy 'Pack\GTK4'
if (!(Test-Path $exe) -or !(Test-Path $cli)) { throw 'Build both executables before bundling the runtime.' }
if (!(Test-Path $objdump)) { throw "objdump.exe missing: $objdump" }
New-Item -ItemType Directory -Force (Join-Path $dest 'bin') | Out-Null
$queue = [System.Collections.Generic.Queue[string]]::new()
$visited = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
$queue.Enqueue($exe)
$queue.Enqueue($cli)
# Include optional plugin DLLs as dependencies of the GTK4 runtime.
foreach ($pluginRoot in @('lib\gdk-pixbuf-2.0', 'lib\gio\modules')) {
    $origin = Join-Path $Ucrt $pluginRoot
    if (Test-Path $origin) {
        $target = Join-Path $dest $pluginRoot
        New-Item -ItemType Directory -Force (Split-Path -Parent $target) | Out-Null
        Copy-Item $origin $target -Recurse -Force
        Get-ChildItem $origin -Recurse -Filter '*.dll' | ForEach-Object { $queue.Enqueue($_.FullName) }
    }
}
$count = 0
while ($queue.Count -gt 0) {
    $file = $queue.Dequeue()
    if (!$visited.Add($file)) { continue }
    $imports = & $objdump -p $file 2>$null
    if ($LASTEXITCODE -ne 0) { throw "Cannot inspect dependencies: $file" }
    foreach ($item in $imports) {
        if ($item -match 'DLL Name:\s*(\S+)') {
            $name = $Matches[1]
            $runtime = Join-Path $bin $name
            if (Test-Path $runtime) {
                Copy-Item $runtime (Join-Path $dest 'bin') -Force
                $queue.Enqueue($runtime)
                $count++
            }
        }
    }
}
# GTK uses schemas, icons and themes in addition to DLL imports.
foreach ($relative in @('share\glib-2.0\schemas', 'share\icons', 'share\themes', 'etc\gtk-4.0')) {
    $src = Join-Path $Ucrt $relative
    if (Test-Path $src) {
        $parent = Split-Path -Parent (Join-Path $dest $relative)
        New-Item -ItemType Directory -Force $parent | Out-Null
        Copy-Item $src (Join-Path $dest $relative) -Recurse -Force
    }
}
Write-Host "Bundled GTK4 runtime imports ($count dependency edges) into $dest"
Write-Host 'Test start_gui.bat on a clean Windows installation. Verify licenses and GTK resource loading.'
