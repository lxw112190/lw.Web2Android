[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$AndroidSdk,
    [Parameter(Mandatory = $true)][string]$JavaHome,
    [Parameter(Mandatory = $true)][string]$RuntimeDirectory,
    [Parameter(Mandatory = $true)][string]$Destination,
    [switch]$Force
)

$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$lock = Get-Content -Raw -LiteralPath (Join-Path $repoRoot 'toolchain.lock.json') | ConvertFrom-Json
$sdk = (Resolve-Path -LiteralPath $AndroidSdk).Path
$java = (Resolve-Path -LiteralPath $JavaHome).Path
$runtime = (Resolve-Path -LiteralPath $RuntimeDirectory).Path
$destinationPath = [System.IO.Path]::GetFullPath($Destination)
$buildTools = Join-Path $sdk "build-tools/$($lock.buildToolsVersion)"
$platform = Join-Path $sdk "platforms/android-$($lock.platformApi)"

$required = @(
    (Join-Path $buildTools 'aapt2.exe'),
    (Join-Path $buildTools 'zipalign.exe'),
    (Join-Path $buildTools 'lib/apksigner.jar'),
    (Join-Path $platform 'android.jar'),
    (Join-Path $java 'bin/java.exe'),
    (Join-Path $runtime 'classes.dex'),
    (Join-Path $runtime 'metadata.json')
)
foreach ($file in $required) {
    if (-not (Test-Path -LiteralPath $file -PathType Leaf)) { throw "Required toolchain file was not found: $file" }
}

$replaceExisting = Test-Path -LiteralPath $destinationPath
if ($replaceExisting -and -not $Force) {
    throw "Destination already exists; use -Force to replace it: $destinationPath"
}
$parent = Split-Path -Parent $destinationPath
New-Item -ItemType Directory -Force -Path $parent | Out-Null
$staging = Join-Path $parent ('.toolchain-staging-' + [guid]::NewGuid().ToString('N'))

try {
    New-Item -ItemType Directory -Force -Path $staging,(Join-Path $staging 'apksigner'),(Join-Path $staging 'runtime') | Out-Null
    Copy-Item -LiteralPath (Join-Path $buildTools 'aapt2.exe'),(Join-Path $buildTools 'zipalign.exe') -Destination $staging
    Get-ChildItem -LiteralPath $buildTools -Filter '*.dll' -File | Copy-Item -Destination $staging
    Copy-Item -LiteralPath (Join-Path $platform 'android.jar') -Destination $staging
    Copy-Item -LiteralPath (Join-Path $buildTools 'lib/apksigner.jar') -Destination (Join-Path $staging 'apksigner/apksigner.jar')
    Copy-Item -LiteralPath $java -Destination (Join-Path $staging 'jre') -Recurse
    Copy-Item -LiteralPath (Join-Path $runtime 'classes.dex'),(Join-Path $runtime 'metadata.json') -Destination (Join-Path $staging 'runtime')

    $notices = Join-Path $staging 'notices'
    New-Item -ItemType Directory -Force -Path $notices | Out-Null
    foreach ($notice in @((Join-Path $buildTools 'NOTICE.txt'),(Join-Path $platform 'package.xml'))) {
        if (Test-Path -LiteralPath $notice) { Copy-Item -LiteralPath $notice -Destination $notices }
    }
    if (Test-Path -LiteralPath (Join-Path $sdk 'licenses')) {
        Copy-Item -LiteralPath (Join-Path $sdk 'licenses') -Destination $notices -Recurse
    }

    $metadata = [ordered]@{
        schemaVersion = 1
        toolchainVersion = [string]$lock.toolchainVersion
        platformApi = [int]$lock.platformApi
        buildToolsVersion = [string]$lock.buildToolsVersion
        javaRuntimeVersion = [string]$lock.javaRuntimeVersion
        runtimeVersion = [string]$lock.runtimeVersion
        assembledAtUtc = [DateTime]::UtcNow.ToString('yyyy-MM-ddTHH:mm:ssZ')
        distribution = 'private-local-use'
    }
    $metadata | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $staging 'metadata.json') -Encoding utf8NoBOM
    $backup = $null
    if ($replaceExisting) {
        $backup = Join-Path $parent ('.toolchain-backup-' + [guid]::NewGuid().ToString('N'))
        Move-Item -LiteralPath $destinationPath -Destination $backup
    }
    try {
        Move-Item -LiteralPath $staging -Destination $destinationPath
        if ($backup -and (Test-Path -LiteralPath $backup)) { Remove-Item -LiteralPath $backup -Recurse -Force }
    } catch {
        if ($backup -and (Test-Path -LiteralPath $backup) -and -not (Test-Path -LiteralPath $destinationPath)) {
            Move-Item -LiteralPath $backup -Destination $destinationPath
        }
        throw
    }
} catch {
    if (Test-Path -LiteralPath $staging) { Remove-Item -LiteralPath $staging -Recurse -Force }
    throw
}

Write-Host "Minimal toolchain assembled: $destinationPath"
