$ErrorActionPreference = 'Stop'
$targetRoot = [IO.Path]::GetFullPath($PSScriptRoot)
$markerPath = Join-Path $targetRoot 'installed.json'
$marker = Get-Content -LiteralPath $markerPath -Raw | ConvertFrom-Json
if ($marker.product -ne 'FingerprintV1Submission' -or $marker.directory -ne $targetRoot) { throw 'Invalid installation marker.' }
foreach ($entry in $marker.files.PSObject.Properties) {
    $target = [IO.Path]::GetFullPath((Join-Path $targetRoot $entry.Name))
    if (-not $target.StartsWith($targetRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) { throw 'Invalid removal path.' }
    if (Test-Path -LiteralPath $target -PathType Leaf) {
        if ((Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash.ToLowerInvariant() -eq $entry.Value) {
            Remove-Item -LiteralPath $target -Force
        } else {
            Write-Warning "Preserved modified file: $($entry.Name)"
        }
    }
}
Remove-Item -LiteralPath $markerPath
foreach ($relative in @('bin/win32','bin/x64','bin','licenses','results')) {
    $directory = [IO.Path]::GetFullPath((Join-Path $targetRoot $relative))
    if ($directory.StartsWith($targetRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase) -and
        (Test-Path -LiteralPath $directory -PathType Container) -and
        @(Get-ChildItem -LiteralPath $directory -Force).Count -eq 0) {
        Remove-Item -LiteralPath $directory
    }
}
if (@(Get-ChildItem -LiteralPath $targetRoot -Force).Count -eq 0) { Remove-Item -LiteralPath $targetRoot }
Write-Output 'Uninstalled; user-added or modified files, if any, were preserved.'
