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
if (-not $buildDirectory.StartsWith($buildRoot + [System.IO.Path]::DirectorySeparatorChar,
                                    [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "Unexpected VS2022 build directory: $buildDirectory"
}

function Find-CMake {
    $command = Get-Command cmake.exe -ErrorAction SilentlyContinue
    if ($command) { return $command.Source }

    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    if (Test-Path -LiteralPath $vswhere -PathType Leaf) {
        $installation = (& $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.CMake.Project -property installationPath | Select-Object -First 1)
        if ($installation) {
            $candidate = Join-Path $installation 'Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'
            if (Test-Path -LiteralPath $candidate -PathType Leaf) { return $candidate }
        }
    }
    throw 'CMake was not found. Add the Visual Studio 2022 CMake component and reopen the solution.'
}

$cmake = Find-CMake
if ($Clean -or $Rebuild) {
    if (Test-Path -LiteralPath $buildDirectory) {
        Remove-Item -LiteralPath $buildDirectory -Recurse -Force
    }
    if ($Clean -and -not $Rebuild) {
        Write-Host "Cleaned: $buildDirectory"
        exit 0
    }
}

Push-Location $repoRoot
try {
    & $cmake --preset vs2022-x64
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    $buildPreset = if ($Configuration -eq 'Debug') { 'vs2022-debug' } else { 'vs2022-release' }
    & $cmake --build --preset $buildPreset
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
} finally {
    Pop-Location
}

Write-Host "VS2022 $Configuration build completed: $buildDirectory\packer\$Configuration"
