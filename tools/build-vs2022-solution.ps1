[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')][string]$Configuration = 'Release',
    [switch]$Clean,
    [switch]$Rebuild
)

$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$buildRoot = [System.IO.Path]::GetFullPath((Join-Path $repoRoot 'build'))
$buildDirectory = [System.IO.Path]::GetFullPath((Join-Path $buildRoot 'vs2022-x64'))
$logDirectory = Join-Path $repoRoot 'logs'
$logFile = Join-Path $logDirectory 'vs2022-build.log'
if (-not $buildDirectory.StartsWith($buildRoot + [System.IO.Path]::DirectorySeparatorChar,
                                    [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "Unexpected VS2022 build directory: $buildDirectory"
}

function Rotate-BuildLog {
    New-Item -ItemType Directory -Force -Path $logDirectory | Out-Null
    if (-not (Test-Path -LiteralPath $logFile -PathType Leaf)) { return }
    if ((Get-Item -LiteralPath $logFile).Length -lt 2MB) { return }

    $oldest = "$logFile.5"
    if (Test-Path -LiteralPath $oldest) { Remove-Item -LiteralPath $oldest -Force }
    for ($index = 4; $index -ge 1; --$index) {
        $source = "$logFile.$index"
        if (Test-Path -LiteralPath $source) {
            Move-Item -LiteralPath $source -Destination "$logFile.$($index + 1)" -Force
        }
    }
    Move-Item -LiteralPath $logFile -Destination "$logFile.1" -Force
}

function Normalize-ProcessPath {
    # Some launchers can inject both PATH and Path. MSBuild treats that as a
    # duplicate environment key when it starts CL.exe, so keep one canonical
    # variable while preserving and de-duplicating all path entries.
    $environment = [Environment]::GetEnvironmentVariables('Process')
    $pathKeys = @($environment.Keys | Where-Object { [string]$_ -ieq 'Path' })
    $entries = [System.Collections.Generic.List[string]]::new()
    $seen = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)
    foreach ($key in $pathKeys) {
        foreach ($entry in ([string]$environment[$key] -split ';')) {
            $trimmed = $entry.Trim()
            if ($trimmed -and $seen.Add($trimmed)) { $entries.Add($trimmed) }
        }
    }
    foreach ($key in $pathKeys) {
        [Environment]::SetEnvironmentVariable([string]$key, $null, 'Process')
    }
    [Environment]::SetEnvironmentVariable('Path', ($entries -join ';'), 'Process')
    Write-Host "Normalized process Path ($($pathKeys.Count) source variable(s), $($entries.Count) unique entries)."
}

function Find-CMake {
    # This solution targets VS2022, so its bundled CMake must take precedence.
    # Python packages and other applications sometimes add an incomplete
    # cmake.exe shim to Path; selecting that shim makes configuration fail
    # before the compiler is ever started.
    $programFilesX86 = [Environment]::GetEnvironmentVariable('ProgramFiles(x86)', 'Process')
    if ($programFilesX86) {
        $vswhere = Join-Path $programFilesX86 'Microsoft Visual Studio/Installer/vswhere.exe'
        if (Test-Path -LiteralPath $vswhere -PathType Leaf) {
            $installation = (& $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.CMake.Project -property installationPath | Select-Object -First 1)
            if ($installation) {
                $candidate = Join-Path $installation 'Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'
                if (Test-Path -LiteralPath $candidate -PathType Leaf) { return $candidate }
            }
        }
    }

    $command = Get-Command cmake.exe -ErrorAction SilentlyContinue
    if ($command) {
        Write-Warning "The VS2022 CMake component was not found; falling back to CMake from Path: $($command.Source)"
        return $command.Source
    }
    throw 'CMake was not found. Add the Visual Studio 2022 CMake component and reopen the solution.'
}

function Invoke-NativeLogged([string]$FilePath, [string[]]$Arguments) {
    # Windows PowerShell 5.1 turns redirected native stderr into a non-terminating
    # NativeCommandError. Temporarily allow it through the pipeline, then write
    # every line via the host so Start-Transcript records stdout and stderr.
    $previousPreference = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        & $FilePath @Arguments 2>&1 | ForEach-Object { Write-Host $_ }
        return $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $previousPreference
    }
}

$exitCode = 0
$transcriptStarted = $false
Rotate-BuildLog
try {
    Start-Transcript -LiteralPath $logFile -Append | Out-Null
    $transcriptStarted = $true
    Write-Host "=== lw.Web2Android VS2022 build ==="
    Write-Host "Time: $([DateTimeOffset]::Now.ToString('yyyy-MM-dd HH:mm:ss.fff zzz'))"
    Write-Host "Source: $repoRoot"
    Write-Host "Configuration: $Configuration"

    Normalize-ProcessPath
    $cmake = Find-CMake
    Write-Host "CMake: $cmake"

    if ($repoRoot.Length -gt 100) {
        Write-Warning "The source path is long ($($repoRoot.Length) characters). If MSBuild reports MSB6003, extract the package to a shorter path such as D:\src\lw.Web2Android."
    }

    if ($Clean -or $Rebuild) {
        if (Test-Path -LiteralPath $buildDirectory) {
            Remove-Item -LiteralPath $buildDirectory -Recurse -Force
        }
        if ($Clean -and -not $Rebuild) {
            Write-Host "Cleaned: $buildDirectory"
        }
    }

    if (-not ($Clean -and -not $Rebuild)) {
        Push-Location $repoRoot
        try {
            $configureExitCode = Invoke-NativeLogged $cmake @('--preset', 'vs2022-x64')
            if ($configureExitCode -ne 0) {
                throw "CMake configuration failed with exit code $configureExitCode."
            }
            $buildPreset = if ($Configuration -eq 'Debug') { 'vs2022-debug' } else { 'vs2022-release' }
            $buildExitCode = Invoke-NativeLogged $cmake @('--build', '--preset', $buildPreset)
            if ($buildExitCode -ne 0) {
                throw "CMake build failed with exit code $buildExitCode."
            }
        } finally {
            Pop-Location
        }
        Write-Host "VS2022 $Configuration build completed: $buildDirectory\packer\$Configuration"
    }
} catch {
    $exitCode = 1
    Write-Host "VS2022 build failed: $($_.Exception.Message)" -ForegroundColor Red
    Write-Host "Complete build log: $logFile" -ForegroundColor Yellow
} finally {
    if ($transcriptStarted) { Stop-Transcript | Out-Null }
}

if ($exitCode -ne 0) { exit $exitCode }
