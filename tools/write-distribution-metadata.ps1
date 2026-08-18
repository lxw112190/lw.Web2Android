[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$PackageDirectory,

    [Parameter(Mandatory = $true)]
    [string]$Version,

    [string]$Commit = 'local'
)

$ErrorActionPreference = 'Stop'
$package = (Resolve-Path -LiteralPath $PackageDirectory).Path
$repository = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$lock = Get-Content -Raw -LiteralPath (Join-Path $repository 'toolchain.lock.json') | ConvertFrom-Json
$builtAt = [DateTime]::UtcNow.ToString('yyyy-MM-ddTHH:mm:ssZ')

$metadata = [ordered]@{
    schemaVersion    = 1
    name             = 'lw.Web2Android'
    version          = $Version
    platform         = 'windows'
    architecture     = 'x64'
    runtimeVersion   = [string]$lock.runtimeVersion
    toolchainVersion = [string]$lock.toolchainVersion
    commit           = $Commit
    buildTimeUtc     = $builtAt
}
$metadata | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $package 'release.json') -Encoding utf8NoBOM

$markdown = @"
# lw.Web2Android $Version

This is the Windows x64 distribution of lw.Web2Android.

## Release identity

| Field | Value |
| --- | --- |
| Version | ``$Version`` |
| Platform | ``windows-x64`` |
| Runtime Version | ``$($lock.runtimeVersion)`` |
| Toolchain Version | ``$($lock.toolchainVersion)`` |
| Commit | ``$Commit`` |
| Build Time (UTC) | ``$builtAt`` |

## Verification

Verify every distributed file against ``SHA256SUMS.txt`` before use. Each sample APK also includes its own SHA-256 sidecar, machine-readable release metadata, and human-readable release notes.

## Toolchain notice

This package does not redistribute the Android SDK or Google Build Tools. Android integration is verified in GitHub Actions with versions pinned by ``toolchain.lock.json``.
"@
$markdown | Set-Content -LiteralPath (Join-Path $package 'RELEASE.md') -Encoding utf8NoBOM
