[CmdletBinding()]
param(
    [string]$RuntimeApk,
    [string]$OutputDirectory
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
if ([string]::IsNullOrWhiteSpace($RuntimeApk)) {
    $RuntimeApk = Join-Path $repoRoot 'runtime/app/build/outputs/apk/release/app-release-unsigned.apk'
}
if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    $OutputDirectory = Join-Path $repoRoot 'build/runtime-dist/runtime-v1'
}
if (-not (Test-Path -LiteralPath $RuntimeApk -PathType Leaf)) {
    throw "Runtime APK was not found: $RuntimeApk"
}

$runtimeApkPath = (Resolve-Path -LiteralPath $RuntimeApk).Path
$outputPath = [System.IO.Path]::GetFullPath($OutputDirectory)
$allowedOutputRoot = [System.IO.Path]::GetFullPath((Join-Path $repoRoot 'build/runtime-dist'))
if (-not $outputPath.StartsWith($allowedOutputRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "Output directory must remain under '$allowedOutputRoot'."
}
if (Test-Path -LiteralPath $outputPath) {
    Remove-Item -LiteralPath $outputPath -Recurse -Force
}
New-Item -ItemType Directory -Path $outputPath | Out-Null

Add-Type -AssemblyName System.IO.Compression.FileSystem
$archive = [System.IO.Compression.ZipFile]::OpenRead($runtimeApkPath)
try {
    $dexEntries = @($archive.Entries |
        Where-Object { $_.FullName -match '^classes([0-9]*)\.dex$' } |
        Sort-Object FullName)
    if ($dexEntries.Count -eq 0) {
        throw 'Runtime APK contains no DEX files.'
    }

    $materialResourceEntries = @($archive.Entries |
        Where-Object {
            $normalizedName = $_.FullName.Replace('\', '/')
            $normalizedName -like 'res/*' -and -not $normalizedName.EndsWith('/')
        })
    if ($materialResourceEntries.Count -gt 0) {
        $resourcePreview = (($materialResourceEntries.FullName | Select-Object -First 20) -join ', ')
        throw "Runtime currently requires APK resources, but runtime-res packaging is not implemented: $resourcePreview"
    }

    foreach ($entry in $dexEntries) {
        $destination = Join-Path $outputPath $entry.Name
        $inputStream = $entry.Open()
        $outputStream = [System.IO.File]::Create($destination)
        try {
            $inputStream.CopyTo($outputStream)
        }
        finally {
            $outputStream.Dispose()
            $inputStream.Dispose()
        }

        $dexText = [System.Text.Encoding]::ASCII.GetString([System.IO.File]::ReadAllBytes($destination))
        if ($dexText.Contains('Lcom/lw/web2android/runtime/R;')) {
            throw "Runtime DEX references the dynamically generated application R class: $($entry.Name)"
        }
    }
}
finally {
    $archive.Dispose()
}

$lock = Get-Content -Raw -LiteralPath (Join-Path $repoRoot 'toolchain.lock.json') | ConvertFrom-Json
$dexFiles = @(Get-ChildItem -LiteralPath $outputPath -Filter 'classes*.dex' -File |
    Sort-Object Name |
    ForEach-Object {
        [ordered]@{
            name = $_.Name
            size = $_.Length
            sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
        }
    })

$metadata = [ordered]@{
    schemaVersion = 1
    runtimeVersion = [string]$lock.runtimeVersion
    namespace = 'com.lw.web2android.runtime'
    mainActivity = 'com.lw.web2android.runtime.MainActivity'
    minSdk = 23
    targetSdk = [int]$lock.platformApi
    androidXWebKitVersion = [string]$lock.androidXWebKitVersion
    dexFiles = $dexFiles
}
$metadata | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $outputPath 'metadata.json') -Encoding utf8

$bundleZip = Join-Path $allowedOutputRoot 'runtime-v1.zip'
if (Test-Path -LiteralPath $bundleZip) {
    Remove-Item -LiteralPath $bundleZip -Force
}
$bundleFiles = @(Get-ChildItem -LiteralPath $outputPath -File | ForEach-Object FullName)
Compress-Archive -LiteralPath $bundleFiles -DestinationPath $bundleZip -CompressionLevel Optimal

Write-Host "Runtime bundle created: $bundleZip"
Write-Host "DEX files: $($dexFiles.Count)"
