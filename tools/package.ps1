param([string]$Tag='candidate-0.4.1')
$ErrorActionPreference='Stop'
if ($Tag -notmatch '^[a-zA-Z0-9][a-zA-Z0-9._-]*$') {throw 'Invalid package tag'}
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$dist=Join-Path $root 'dist'
$package=Join-Path $dist $Tag
if (Test-Path $package) {throw 'Package already exists; choose a new tag'}
New-Item -ItemType Directory -Force "$package/bin/x64","$package/bin/win32","$package/licenses" | Out-Null
$files=@('fp_gui.exe','fp_bench.exe','fp_task.exe','fp_protocol.exe','fp_calibrate.exe','fp_inspect.exe','fp_plan.exe','fingerprint_graph.dll','fingerprint_minutiae.dll','FRFXLL.dll','enroll.exe','match.exe')
foreach ($architecture in @('x64','win32')) {
    $build=if ($architecture -eq 'x64') {'build'} else {'build-win32'}
    foreach ($name in $files) {Copy-Item -LiteralPath "$root/$build/Release/$name" -Destination "$package/bin/$architecture/$name"}
    Copy-Item -LiteralPath "$root/$build/Release/inspect-assets" -Destination "$package/bin/$architecture/inspect-assets" -Recurse
}
Copy-Item -LiteralPath "$root/README.md","$root/NOTICE.md" -Destination $package
Copy-Item -LiteralPath "$root/docs" -Destination "$package/docs" -Recurse
foreach ($name in @('COPYRIGHT.txt','COPYING.txt','COPYING.LESSER.txt')) {Copy-Item -LiteralPath "$root/third_party/FingerJetFXOSE-master/$name" -Destination "$package/licenses/$name"}
$sourceRoot=Join-Path $package 'source'
New-Item -ItemType Directory -Force "$sourceRoot/third_party" | Out-Null
Copy-Item -LiteralPath "$root/CMakeLists.txt","$root/.clang-format","$root/README.md","$root/NOTICE.md","$root/src","$root/assets","$root/tests","$root/docs","$root/tools","$root/results" -Destination $sourceRoot -Recurse
New-Item -ItemType Directory -Force "$sourceRoot/protocol" | Out-Null
Copy-Item -LiteralPath "$root/protocol/graph-v1-design.json" -Destination "$sourceRoot/protocol"
Copy-Item -LiteralPath "$root/third_party/FingerJetFXOSE-master" -Destination "$sourceRoot/third_party" -Recurse
Compress-Archive -LiteralPath $sourceRoot -DestinationPath "$package/corresponding-source.zip" -CompressionLevel Optimal
$manifest=[ordered]@{version=$Tag;submitted=$false;certification_status='NOT_ASSESSED';independent_validation_completed=$false;gui_click_validation_completed=$false;algorithms='Feature extraction library + our local-neighborhood/geometric-consensus matcher and separate graph-v1 candidate';source_archive_sha256=(Get-FileHash "$package/corresponding-source.zip").Hash;dll_x64_sha256=(Get-FileHash "$package/bin/x64/fingerprint_minutiae.dll").Hash;extractor_x64_sha256=(Get-FileHash "$package/bin/x64/FRFXLL.dll").Hash;dll_win32_sha256=(Get-FileHash "$package/bin/win32/fingerprint_minutiae.dll").Hash;runtime_requirement='Microsoft Visual C++ 2015-2022 runtime for each architecture; clean-PC validation pending'}
$manifest | ConvertTo-Json | Set-Content -LiteralPath "$package/release.json" -Encoding utf8
$hashes=Get-ChildItem -LiteralPath $package -Recurse -File | Sort-Object FullName | ForEach-Object { '{0}  {1}' -f (Get-FileHash -LiteralPath $_.FullName).Hash,$_.FullName.Substring($package.Length+1) }
$hashes | Set-Content -LiteralPath "$package/SHA256SUMS.txt" -Encoding utf8
Compress-Archive -LiteralPath $package -DestinationPath "$dist/$Tag.zip" -CompressionLevel Optimal
Write-Host "Created local package: $dist/$Tag.zip"
