param([string]$Destination = (Join-Path $env:LOCALAPPDATA 'FingerprintV1Submission'), [switch]$Force)
$ErrorActionPreference = 'Stop'
$packageRoot = [IO.Path]::GetFullPath($PSScriptRoot)
$targetRoot = [IO.Path]::GetFullPath($Destination)
if ($targetRoot.TrimEnd('\','/') -eq [IO.Path]::GetPathRoot($targetRoot).TrimEnd('\','/')) {
    throw 'A drive root cannot be an installation directory.'
}
$manifest = Get-Content -LiteralPath (Join-Path $packageRoot 'package.json') -Raw | ConvertFrom-Json
if ($manifest.product -ne 'FingerprintV1Submission') { throw 'Unexpected package.' }
if (Test-Path -LiteralPath $targetRoot) {
    $marker = Join-Path $targetRoot 'installed.json'
    if (-not (Test-Path -LiteralPath $marker)) { throw 'Target exists without an installation marker.' }
    $previous = Get-Content -LiteralPath $marker -Raw | ConvertFrom-Json
    if ($previous.product -ne $manifest.product -or -not $Force) { throw 'Use -Force only for an existing installation of this product.' }
}
foreach ($entry in $manifest.files.PSObject.Properties) {
    $source = [IO.Path]::GetFullPath((Join-Path $packageRoot $entry.Name))
    if (-not $source.StartsWith($packageRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) { throw 'Invalid package path.' }
    if ((Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash.ToLowerInvariant() -ne $entry.Value) { throw "Package checksum mismatch: $($entry.Name)" }
}
New-Item -ItemType Directory -Path $targetRoot -Force | Out-Null
$installed = [ordered]@{}
foreach ($entry in $manifest.files.PSObject.Properties) {
    if ($entry.Name -eq 'install.ps1') { continue }
    $target = [IO.Path]::GetFullPath((Join-Path $targetRoot $entry.Name))
    if (-not $target.StartsWith($targetRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) { throw 'Invalid installation path.' }
    New-Item -ItemType Directory -Path (Split-Path -Parent $target) -Force | Out-Null
    Copy-Item -LiteralPath (Join-Path $packageRoot $entry.Name) -Destination $target -Force
    $installed[$entry.Name] = $entry.Value
}
[ordered]@{product=$manifest.product;version=$manifest.version;directory=$targetRoot;files=$installed} |
    ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $targetRoot 'installed.json') -Encoding UTF8
Write-Output "Installed: $targetRoot"
