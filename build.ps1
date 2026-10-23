# Echo Engine: Windows / GCC / C++20 one-click build.
[CmdletBinding()]
param(
    [ValidateSet('Release', 'Debug')]
    [string]$Configuration = 'Release',
    [ValidateRange(1, 64)]
    [int]$Jobs = 4,
    [switch]$RunTests,
    [switch]$Pause,
    [switch]$NoPause
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$buildExitCode = 0
$locationPushed = $false
try {
    Push-Location -LiteralPath $PSScriptRoot
    $locationPushed = $true
    foreach ($buildTool in @('cmake', 'gcc', 'g++', 'windres', 'ninja')) {
        if (-not (Get-Command $buildTool -CommandType Application -ErrorAction SilentlyContinue)) {
            throw "Missing tool: $buildTool. Add 64-bit MinGW GCC, CMake >= 3.22 and Ninja to PATH."
        }
    }
    if ($RunTests -and -not (Get-Command ctest -CommandType Application -ErrorAction SilentlyContinue)) {
        throw 'Missing tool: ctest. Install CMake and add it to PATH.'
    }
    $buildPreset = 'gcc-' + $Configuration.ToLowerInvariant()
    Write-Host "Echo Engine | $Configuration | $Jobs parallel jobs"
    & cmake --preset $buildPreset
    if ($LASTEXITCODE -ne 0) { throw "CMake configure failed (exit $LASTEXITCODE)." }
    & cmake --build --preset $buildPreset --parallel $Jobs
    if ($LASTEXITCODE -ne 0) { throw "Build failed (exit $LASTEXITCODE)." }
    $buildExecutable = Join-Path $PSScriptRoot "out\build\$buildPreset\echo_engine\runtime\EchoOfKino.exe"
    # Retry convenience deployment even when Ninja had no source changes.
    & cmake "-DRUNTIME=$buildExecutable" "-DSOURCE=$PSScriptRoot" -P (Join-Path $PSScriptRoot 'tools\deploy_runtime.cmake')
    if ($LASTEXITCODE -ne 0) { throw "Runtime deployment failed (exit $LASTEXITCODE)." }
    if ($RunTests) {
        & ctest --preset $buildPreset --output-on-failure
        if ($LASTEXITCODE -ne 0) { throw "Tests failed (exit $LASTEXITCODE)." }
    }
    $buildExecutable = Join-Path $PSScriptRoot "out\build\$buildPreset\echo_engine\runtime\EchoOfKino.exe"
    if (-not (Test-Path -LiteralPath $buildExecutable -PathType Leaf)) {
        throw "Build reported success but runtime is missing: $buildExecutable"
    }
    Write-Host ''
    Write-Host "Build succeeded: $buildExecutable" -ForegroundColor Cyan
    Write-Host ('Editor: ' + (Join-Path (Split-Path -Parent $buildExecutable) 'EchoEngine.exe'))
    Write-Host ('Game:   ' + $buildExecutable)
    Write-Host 'bin mirrors the latest build when its entries are not in use; build Release for distribution.'
}
catch {
    $buildExitCode = 1
    Write-Host ('Build stopped: ' + $_.Exception.Message) -ForegroundColor Red
}
finally {
    if ($locationPushed) { Pop-Location }
    if ($Pause -and -not $NoPause) {
        try { [void](Read-Host 'Press Enter to close') } catch { }
    }
}
exit $buildExitCode
