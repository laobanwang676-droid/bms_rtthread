param(
    [switch]$Clean
)

$ErrorActionPreference = "Continue"

$workspaceRoot = Split-Path -Parent $PSScriptRoot
$buildDir = Join-Path $workspaceRoot "build"

$cmakeArgs = @("--build", $buildDir, "--parallel", "8")
if ($Clean) {
    $cmakeArgs = @("--build", $buildDir, "--clean-first", "--parallel", "8")
}

$logLines = New-Object System.Collections.Generic.List[string]

Write-Host "========== CMake Build =========="
Write-Host "Workspace : $workspaceRoot"
Write-Host "Build dir : $buildDir"
Write-Host "Command   : cmake $($cmakeArgs -join ' ')"
Write-Host "================================="

& cmake @cmakeArgs 2>&1 | ForEach-Object {
    $line = $_.ToString()
    $logLines.Add($line)
    Write-Host $line
}

$exitCode = $LASTEXITCODE

$warningPatterns = @(
    "(?i)\bwarning:",
    "(?i)\bwarning\s+\[",
    "(?i)\bwarn(?:ing)?\b"
)

$errorPatterns = @(
    "(?i)\berror:",
    "(?i)\bfatal error:",
    "(?i)undefined reference",
    "(?i)collect2(?:\.exe)?: error",
    "(?i)ld(?:\.exe)?: .*error",
    "(?i)No rule to make target",
    "(?i)recipe for target .* failed",
    "(?i)mingw32-make(?:\.exe)?: \*\*\*"
)

$ignoreErrorPatterns = @(
    "(?i)does not take linker garbage collection into account"
)

$warningCount = 0
$errorCount = 0

foreach ($line in $logLines) {
    foreach ($pattern in $warningPatterns) {
        if ($line -match $pattern) {
            $warningCount++
            break
        }
    }

    $ignored = $false
    foreach ($pattern in $ignoreErrorPatterns) {
        if ($line -match $pattern) {
            $ignored = $true
            break
        }
    }

    if (-not $ignored) {
        foreach ($pattern in $errorPatterns) {
            if ($line -match $pattern) {
                $errorCount++
                break
            }
        }
    }
}

$result = if ($exitCode -eq 0) { "SUCCESS" } else { "FAILED" }

Write-Host ""
Write-Host "========== Build Summary =========="
Write-Host "Errors   : $errorCount"
Write-Host "Warnings : $warningCount"
Write-Host "Result   : $result"
Write-Host "ExitCode : $exitCode"
Write-Host "==================================="

exit $exitCode
