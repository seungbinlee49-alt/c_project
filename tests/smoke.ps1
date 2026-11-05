param([string]$DataRoot = "$PSScriptRoot/../../tmp/analysis/materials")
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot
$bin = Join-Path $root 'build/Release'
$output = Join-Path $root 'test-output'
New-Item -ItemType Directory -Force $output | Out-Null
Add-Type -AssemblyName System.IO.Compression.FileSystem
$databases = @('FVC2000_DB3_B','FVC2002_DB3_B','FVC2004_DB1_B')
$checks = 0
foreach ($index in 0..2) {
    $db = Join-Path $output $databases[$index]
    New-Item -ItemType Directory -Force $db | Out-Null
    $archive = [IO.Compression.ZipFile]::OpenRead((Join-Path $DataRoot ($databases[$index]+'.zip')))
    try {
        foreach ($name in @('101_1.tif','101_2.tif','102_1.tif')) {
            [IO.Compression.ZipFileExtensions]::ExtractToFile($archive.GetEntry($name),(Join-Path $db $name),$true)
        }
    } finally { $archive.Dispose() }
    foreach ($probe in @('101_2.tif','102_1.tif')) {
        $result = Join-Path $output "normal-$index-$probe.json"
        & "$bin/fp_eval.exe" "$bin/fingerprint.dll" ($index+1) "$db/101_1.tif" "$db/$probe" $result 5000
        $record = Get-Content -Raw $result | ConvertFrom-Json
        if ($LASTEXITCODE -ne 0 -or $record.result.status -ne 'OK' -or $record.result.score -lt 0 -or $record.result.score -gt 1) {throw 'Normal comparison failed'}
        $checks++
    }
}
$db = Join-Path $output $databases[0]
foreach ($case in @(@('fail','FAIL'),@('hang','TIMEOUT'),@('crash','CRASH'))) {
    $result = Join-Path $output ($case[0]+'.json')
    & "$bin/fp_eval.exe" "$bin/fixture_$($case[0]).dll" 1 "$db/101_1.tif" "$db/101_2.tif" $result 1000
    $record = Get-Content -Raw $result | ConvertFrom-Json
    if ($LASTEXITCODE -ne 1 -or $record.result.status -ne $case[1]) {throw "Incorrect status: $($case[0])"}
    $checks++
}
& "$bin/fp_eval.exe" "$bin/fingerprint.dll" 1 "$db/101_1.tif" "$db/101_1.tif" "$output/same.json" 1000
if ($LASTEXITCODE -ne 2) {throw 'Same-image comparison accepted'}
$checks++
& "$bin/fp_eval.exe" "$bin/fingerprint.dll" 2 "$db/101_1.tif" "$db/101_2.tif" "$output/wrong-type.json" 1000
if ((Get-Content -Raw "$output/wrong-type.json" | ConvertFrom-Json).result.status -ne 'FAIL') {throw 'Wrong dimensions accepted'}
$checks++
Write-Host "PASS: $checks integration checks"
exit 0
