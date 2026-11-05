param([string]$Root = "$PSScriptRoot/..")
$ErrorActionPreference='Stop'
$Root=[IO.Path]::GetFullPath($Root)
$test=Join-Path $Root ('test-output/acceptance-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force $test | Out-Null
$samples=Join-Path $Root 'data/FVC2000_DB3_B'
foreach ($arch in @('build','build-win32')) {
    $bin=Join-Path $Root "$arch/Release"
    & "$bin/fp_minutiae_test.exe"
    if ($LASTEXITCODE -ne 0) {throw "Matcher unit failure: $arch"}
    & "$bin/fp_abi_test.exe" "$bin/fingerprint_minutiae.dll"
    if ($LASTEXITCODE -ne 0) {throw "ABI failure: $arch"}
    & "$bin/enroll.exe" "$samples/101_1.tif" "$test/$arch-a.bin" "$test/$arch-enroll.log"
    & "$bin/enroll.exe" "$samples/101_2.tif" "$test/$arch-b.bin" "$test/$arch-enroll.log"
    & "$bin/match.exe" "$test/$arch-a.bin" "$test/$arch-b.bin" "$test/$arch-match.log"
    if ($LASTEXITCODE -ne 0 -or (Get-Content "$test/$arch-match.log") -notmatch ' OK ([0-9.eE+-]+)$') {throw "External protocol failure: $arch"}
    & "$bin/match.exe" "$test/$arch-a.bin" "$test/$arch-b.bin" "$test/$arch-match.log"
    if (@(Get-Content "$test/$arch-match.log").Count -ne 2) {throw 'External adapter did not append'}
    [IO.File]::WriteAllBytes("$test/corrupt.bin",[byte[]](1,2,3,4))
    & "$bin/match.exe" "$test/corrupt.bin" "$test/$arch-b.bin" "$test/$arch-bad.log"
    if ((Get-Content "$test/$arch-bad.log") -notmatch ' FAIL 0$') {throw 'Corrupt template was accepted'}
}
# Interoperate across architecture using the same serialized schema.
$x64=Join-Path $Root 'build/Release'
& "$x64/match.exe" "$test/build-win32-a.bin" "$test/build-win32-b.bin" "$test/cross.log"
if ((Get-Content "$test/cross.log") -notmatch ' OK ([0-9.eE+-]+)$') {throw 'Win32 templates unreadable by x64 matcher'}
$crossScore=[double]$Matches[1]
if ((Get-Content "$test/build-match.log" -TotalCount 1) -notmatch ' OK ([0-9.eE+-]+)$') {throw 'Missing x64 score'}
if ([math]::Abs($crossScore-[double]$Matches[1]) -gt 0.00001) {throw 'Architecture score mismatch'}
# Hand-calculated example: calibration must not be fitted again to evaluation.
$header="pair_id`tkind`tstatus`tstage`tscore`tcompare_ms`tmatching_ms"
[IO.File]::WriteAllLines("$test/calibration.tsv",@($header,"1`tG`tOK`tcompare`t0.9`t1`t2","2`tG`tOK`tcompare`t0.8`t1`t2","3`tI`tOK`tcompare`t0.4`t1`t2","4`tI`tOK`tcompare`t0.2`t1`t2"),[Text.UTF8Encoding]::new($false))
[IO.File]::WriteAllLines("$test/evaluation.tsv",@($header,"5`tG`tOK`tcompare`t0.3`t1`t2","6`tG`tOK`tcompare`t0.9`t1`t2","7`tI`tOK`tcompare`t0.7`t1`t2","8`tI`tOK`tcompare`t0.1`t1`t2"),[Text.UTF8Encoding]::new($false))
& "$x64/fp_calibrate.exe" fit "$test/calibration.tsv" 0 "$test/threshold.tsv" synthetic-unit-test
if ($LASTEXITCODE -ne 0) {throw 'Calibration failed'}
& "$x64/fp_calibrate.exe" evaluate "$test/threshold.tsv" "$test/evaluation.tsv" "$test/fixed.json" synthetic-unit-test
$m=Get-Content -Raw "$test/fixed.json" | ConvertFrom-Json
if ($LASTEXITCODE -ne 0 -or $m.FMR -ne 0.5 -or $m.FNMR -ne 0.5 -or $m.threshold_refitted) {throw 'Frozen threshold was not preserved'}
& "$x64/fp_calibrate.exe" evaluate "$test/threshold.tsv" "$test/calibration.tsv" "$test/leak.json" synthetic-unit-test
if ($LASTEXITCODE -ne 2) {throw 'Calibration data reused as evaluation'}
$exposed="$Root/protocol/exposed-db1.tsv"
if (-not (Test-Path $exposed)) { $exposed="$test/exposed.tsv"; & "$x64/fp_protocol.exe" scan $samples 1 $exposed FVC2000_DB3_B; if ($LASTEXITCODE -ne 0) {throw 'Could not create exposure test inventory'} }
& "$x64/fp_protocol.exe" check $exposed $exposed
if ($LASTEXITCODE -ne 1) {throw 'Known exposure not detected'}
Write-Host "PASS: x64/Win32 ABI, adapters, append/FAIL protocol, template interoperability, frozen threshold and exposure checks. $test"
exit 0
