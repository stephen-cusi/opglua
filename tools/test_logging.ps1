$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$fixture = Join-Path $repoRoot 'build\windows\tests\filesystem_contract.exe'
$dll = Join-Path $repoRoot 'src\lua51.dll'
$outputRoot = Join-Path $repoRoot 'build\windows\tests\logging'
New-Item -ItemType Directory -Path $outputRoot -Force | Out-Null
$savedEnvironment = $env:OPGLUA_LOG

function Test-LoggingMode {
    param([string]$Name, [string[]]$Mode, [bool]$Enabled, [bool]$Verbose)
    $log = Join-Path $outputRoot ($Name + ' trace.log')
    $report = Join-Path $outputRoot ($Name + '.txt')
    if (Test-Path -LiteralPath $log) { Remove-Item -LiteralPath $log }
    & $fixture $dll replacement @Mode -opglua-logfile $log > $report
    if ($LASTEXITCODE -ne 0) { throw "Logging case $Name failed its native contracts" }
    if ((Test-Path -LiteralPath $log) -ne $Enabled) {
        throw "Logging case $Name created an unexpected set of log files"
    }
    if ($Enabled) {
        $text = Get-Content -LiteralPath $log -Raw
        if ($text -notmatch 'what=CreateInterface') { throw "Logging case $Name has no session marker" }
        if (($text -match 'SLOT:') -ne $Verbose) { throw "Logging case $Name has the wrong trace level" }
    }
    Write-Output "PASS logging $Name"
}

try {
    # A stale diagnostic environment must not enable logging in normal launches.
    $env:OPGLUA_LOG = 'verbose'
    Test-LoggingMode -Name 'default' -Mode @() -Enabled $false -Verbose $false
    Test-LoggingMode -Name 'off' -Mode @('-opglua-log', '0') -Enabled $false -Verbose $false
    Test-LoggingMode -Name 'invalid' -Mode @('-opglua-log', 'invalid') -Enabled $false -Verbose $false
    Test-LoggingMode -Name 'lifecycle' -Mode @('-opglua-log', '1') -Enabled $true -Verbose $false
    Test-LoggingMode -Name 'verbose' -Mode @('-opglua-log', '2') -Enabled $true -Verbose $true
    Write-Output 'logging.failures|0'
} finally {
    $env:OPGLUA_LOG = $savedEnvironment
}
