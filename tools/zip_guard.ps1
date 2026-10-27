param([Parameter(Mandatory=$true)][string]$Archive,[Parameter(Mandatory=$true)][string]$Target)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
$root = [System.IO.Path]::GetFullPath($Target)
$zip = [System.IO.Compression.ZipFile]::OpenRead($Archive)
try {
  if($zip.Entries.Count -gt 50000) { throw 'Too many files' }
  $total = [long]0
  $seen = New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase)
  foreach($entry in $zip.Entries) {
    $name = $entry.FullName.Replace('\','/')
    if([string]::IsNullOrWhiteSpace($name) -or $name.StartsWith('/') -or $name.Contains(':') -or
       ($name.Split('/') | Where-Object { $_ -eq '..' -or $_ -eq '.' })) { throw "Unsafe ZIP entry: $name" }
    if(-not $seen.Add($name.TrimEnd('/'))) { throw "Duplicate ZIP entry: $name" }
    # Unix symlinks in archives are forbidden.
    $mode = (($entry.ExternalAttributes -shr 16) -band 0xf000)
    if($mode -eq 0xa000) { throw 'ZIP symlinks are forbidden' }
    if($entry.Length -gt 1GB) { throw 'Oversized file' }
    $total += $entry.Length
    if($total -gt 2GB) { throw 'Oversized archive' }
    $dest = [System.IO.Path]::GetFullPath([System.IO.Path]::Combine($root,$name.Replace('/',[System.IO.Path]::DirectorySeparatorChar)))
    if(-not $dest.StartsWith($root.TrimEnd('\','/') + [System.IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)) { throw 'Path escape' }
  }
  foreach($entry in $zip.Entries) {
    $dest = [System.IO.Path]::GetFullPath([System.IO.Path]::Combine($root,$entry.FullName.Replace('/',[System.IO.Path]::DirectorySeparatorChar)))
    if($entry.FullName.EndsWith('/')) { [void][System.IO.Directory]::CreateDirectory($dest); continue }
    [void][System.IO.Directory]::CreateDirectory([System.IO.Path]::GetDirectoryName($dest))
    $source = $entry.Open()
    try {
      $output = [System.IO.File]::Open($dest,[System.IO.FileMode]::CreateNew)
      try { $source.CopyTo($output) } finally { $output.Dispose() }
    } finally { $source.Dispose() }
  }
} finally { $zip.Dispose() }
