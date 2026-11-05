param([string]$Plan = "$PSScriptRoot/../experiments/db1.tsv")
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot
$bin = Join-Path $root 'build/Release'
$out = Join-Path $root ('test-output/batch-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force $out | Out-Null
if (-not (Test-Path $Plan)) { $Plan=Join-Path $out 'full.tsv'; & "$bin/fp_bench.exe" plan "$root/data/FVC2000_DB3_B" 1 $Plan 20261103 first; if ($LASTEXITCODE -ne 0) {throw 'Could not create test plan'} }
$lines = Get-Content -Encoding utf8 $Plan
$genuine = $lines | Select-Object -Skip 1 | Where-Object { ($_ -split "`t")[3] -eq 'G' } | Select-Object -First 1
$impostor = $lines | Select-Object -Skip 1 | Where-Object { ($_ -split "`t")[3] -eq 'I' } | Select-Object -First 1
$small = Join-Path $out 'partial.tsv'
[IO.File]::WriteAllLines($small,@($lines[0],$genuine,$impostor),[Text.UTF8Encoding]::new($false))
$cases = @(@('fail','FAIL','enroll'),@('hang','TIMEOUT','enroll'),@('crash','CRASH','enroll'),@('badsize','FAIL','enroll'),@('badscore','FAIL','compare'),@('comparehang','TIMEOUT','compare'),@('comparecrash','CRASH','compare'),@('overrun','CRASH','enroll'),@('inputwrite','CRASH','enroll'))
$checks=0
foreach ($case in $cases) {
    $folder = Join-Path $out $case[0]
    & "$bin/fp_bench.exe" run $small "$bin/fixture_$($case[0]).dll" $folder 600 2 clean 42
    if ($LASTEXITCODE -ne 0) {throw "Incomplete fixture run $($case[0])"}
    $records = Get-Content -Encoding utf8 "$folder/records.jsonl" | ForEach-Object {$_ | ConvertFrom-Json}
    $pairs = @($records | Where-Object kind -eq 'pair')
    if ($pairs.Count -ne 2) {throw 'Lost planned attempts'}
    foreach ($pair in $pairs) {if ($pair.outcome.status -ne $case[1] -or $pair.outcome.stage -ne $case[2]) {throw "Wrong failure attribution $($case[0])"}}
    $m=Get-Content -Raw "$folder/metrics.json" | ConvertFrom-Json
    if ($m.planned_genuine -ne 1 -or $m.planned_impostor -ne 1 -or $m.successful_genuine -ne 0 -or $m.successful_impostor -ne 0) {throw 'Incorrect failure denominators'}
    if ($case[2] -eq 'enroll' -and $m.FTE -ne 1) {throw 'Enrollment failure missing from FTE'}
    if ($case[2] -eq 'compare' -and ($m.REJgenuine -ne 1 -or $m.REJimpostor -ne 1)) {throw 'Matching failures missing from REJ'}
    if ($case[0] -eq 'fail' -and @($records | Where-Object { $_.kind -eq 'enroll' -and $_.outcome.error_code -eq 42 }).Count -eq 0) {throw 'DLL error code was lost'}
    $checks++
}
foreach ($workers in @(1,4)) {
    & "$bin/fp_bench.exe" run $small "$bin/fingerprint_baseline.dll" "$out/replay-$workers" 5000 $workers gaussian15 42
    if ($LASTEXITCODE -ne 0) {throw 'Replay failed'}
}
$one = @(Get-Content -Encoding utf8 "$out/replay-1/records.jsonl" | ForEach-Object {$_ | ConvertFrom-Json} | Where-Object kind -eq 'pair' | Sort-Object pair_id)
$four = @(Get-Content -Encoding utf8 "$out/replay-4/records.jsonl" | ForEach-Object {$_ | ConvertFrom-Json} | Where-Object kind -eq 'pair' | Sort-Object pair_id)
for ($i=0;$i -lt $one.Count;$i++) {if ($one[$i].outcome.score -ne $four[$i].outcome.score -or $one[$i].outcome.status -ne 'OK') {throw 'Parallel replay changed results'}}
$checks++
# A cancellation file supports automated tests and the future GUI stop button.
$cancel = Join-Path $out 'cancelled'
$arguments = @('run',$small,"$bin/fixture_comparehang.dll",$cancel,'30000','2','clean','42') | ForEach-Object {'"'+$_+'"'}
$process = Start-Process -FilePath "$bin/fp_bench.exe" -ArgumentList $arguments -WindowStyle Hidden -PassThru -RedirectStandardOutput "$out/cancel-stdout.txt" -RedirectStandardError "$out/cancel-stderr.txt"
try {
    $deadline = [DateTime]::UtcNow.AddSeconds(10)
    while (-not (Test-Path "$cancel/records.jsonl")) {
        if ($process.HasExited -or [DateTime]::UtcNow -gt $deadline) {throw 'Cancellation test did not start'}
        Start-Sleep -Milliseconds 50
    }
    [IO.File]::WriteAllText("$cancel/cancel.request",'stop')
    if (-not $process.WaitForExit(10000)) {throw 'Cancellation did not terminate workers'}
    $state=Get-Content -Raw "$cancel/run.json" | ConvertFrom-Json
    if ($state.status -ne 'CANCELLED' -or (Test-Path "$cancel/metrics.json")) {throw 'Cancelled run presented final metrics'}
} finally {if (-not $process.HasExited) {$process.Kill()}}
$checks++
& "$bin/fp_metrics_test.exe"
if ($LASTEXITCODE -ne 0) {throw 'Metric unit checks failed'}
Write-Host "PASS: $checks batch scenarios, metric edge cases and cancellation. Artifacts: $out"
exit 0
