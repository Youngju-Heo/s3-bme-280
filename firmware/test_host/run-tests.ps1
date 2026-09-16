param([string]$Name = "")
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$idf = if ($env:IDF_PATH) { $env:IDF_PATH } else { "C:\esp\v6.0.1\esp-idf" }
$gcc = "C:\msys64\mingw64\bin\gcc.exe"
$unity = "$idf\components\unity\unity\src"
$build = "$PSScriptRoot\build"
New-Item -ItemType Directory -Force $build | Out-Null

$common = @("-std=c11", "-Wall", "-Wextra", "-Werror", "-I$unity",
    "-I$root\components\log_store\include", "-I$root\components\bme280\include",
    "-I$root\main", "-I$PSScriptRoot", "$unity\unity.c")

$tests = [ordered]@{
    "test-log-record" = @("$PSScriptRoot\test-log-record.c", "$root\components\log_store\log-record.c")
    "test-log-store" = @("$PSScriptRoot\test-log-store.c", "$root\components\log_store\log-record.c", "$root\components\log_store\log-store.c")
    "test-bme280-compensate" = @("$PSScriptRoot\test-bme280-compensate.c", "$root\components\bme280\bme280-compensate.c")
    "test-bme280" = @("$PSScriptRoot\test-bme280.c", "$root\components\bme280\bme280-compensate.c", "$root\components\bme280\bme280.c")
    "test-json-mini" = @("$PSScriptRoot\test-json-mini.c", "$root\main\json-mini.c")
    "test-protocol" = @("$PSScriptRoot\test-protocol.c", "$root\main\protocol.c", "$root\main\json-mini.c",
                        "$root\components\log_store\log-record.c", "$root\components\log_store\log-store.c")
    "test-sampler" = @("$PSScriptRoot\test-sampler.c", "$root\main\sampler.c",
                       "$root\components\bme280\bme280-compensate.c", "$root\components\bme280\bme280.c",
                       "$root\components\log_store\log-record.c", "$root\components\log_store\log-store.c")
}

$failed = 0
foreach ($t in $tests.Keys) {
    if ($Name -and $t -ne $Name) { continue }
    $exe = "$build\$t.exe"
    $srcs = $tests[$t]
    & $gcc @common @srcs -o $exe
    if ($LASTEXITCODE -ne 0) { throw "compile failed: $t" }
    & $exe
    if ($LASTEXITCODE -ne 0) { $failed++ }
}
if ($failed -gt 0) { Write-Host "FAILED: $failed test binaries"; exit 1 }
Write-Host "ALL PASSED"
