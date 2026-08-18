[CmdletBinding()]
param(
    [string]$Destination,
    [switch]$AcceptAndroidSdkLicense,
    [switch]$Force
)

$ErrorActionPreference = 'Stop'
if (-not $AcceptAndroidSdkLicense) {
    throw 'Android SDK License acceptance is required. Review https://developer.android.com/studio/terms and rerun with -AcceptAndroidSdkLicense.'
}

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
if ([string]::IsNullOrWhiteSpace($Destination)) { $Destination = Join-Path $repoRoot 'toolchain' }
$destinationPath = [System.IO.Path]::GetFullPath($Destination)
$lock = Get-Content -Raw -LiteralPath (Join-Path $repoRoot 'toolchain.lock.json') | ConvertFrom-Json
$working = Join-Path ([System.IO.Path]::GetTempPath()) ('lw-web2android-toolchain-' + [guid]::NewGuid().ToString('N'))
$downloads = Join-Path $working 'downloads'
$sdk = Join-Path $working 'android-sdk'
$jreExtract = Join-Path $working 'jre'
New-Item -ItemType Directory -Force -Path $downloads,$sdk | Out-Null

try {
    function Get-VerifiedArchive([string]$Url, [string]$Sha256, [string]$Name) {
        $file = Join-Path $downloads $Name
        Write-Host "Downloading $Name ..."
        & curl.exe -L --fail --retry 3 --output $file $Url
        if ($LASTEXITCODE -ne 0) { throw "Download failed: $Url" }
        $actual = (Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash.ToLowerInvariant()
        if ($actual -ne $Sha256.ToLowerInvariant()) { throw "SHA-256 mismatch for $Name" }
        return $file
    }

    $commandLineArchive = Get-VerifiedArchive `
        $lock.commandLineToolsUrl `
        $lock.commandLineToolsSha256 `
        "commandlinetools-win-$($lock.commandLineToolsVersion).zip"

    $bundledJre = Join-Path $repoRoot 'toolchain/jre'
    if (Test-Path -LiteralPath (Join-Path $bundledJre 'bin/java.exe')) {
        Write-Host 'Using the Temurin JRE included in the application directory.'
        Copy-Item -LiteralPath $bundledJre -Destination $jreExtract -Recurse
    } else {
        $javaArchive = Get-VerifiedArchive `
            $lock.javaRuntimeUrl `
            $lock.javaRuntimeSha256 `
            "temurin-jre-$($lock.javaRuntimeVersion)-windows-x64.zip"
        $extract = Join-Path $working 'jre-extract'
        Expand-Archive -LiteralPath $javaArchive -DestinationPath $extract
        $javaHome = Get-ChildItem -LiteralPath $extract -Directory | Select-Object -First 1
        if (-not $javaHome) { throw 'Temurin JRE archive has an unexpected layout' }
        Move-Item -LiteralPath $javaHome.FullName -Destination $jreExtract
    }

    $commandLineExtract = Join-Path $working 'cmdline-tools-extract'
    Expand-Archive -LiteralPath $commandLineArchive -DestinationPath $commandLineExtract
    $commandLineHome = Join-Path $sdk "cmdline-tools/$($lock.commandLineToolsVersion)"
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $commandLineHome) | Out-Null
    Copy-Item -LiteralPath (Join-Path $commandLineExtract 'cmdline-tools') -Destination $commandLineHome -Recurse

    $sdkManager = Join-Path $commandLineHome 'bin/sdkmanager.bat'
    $previousJavaHome = $env:JAVA_HOME
    $env:JAVA_HOME = $jreExtract
    try {
        Write-Host 'Installing the locked Android Platform and Build Tools from the official repository ...'
        $accept = 1..20 | ForEach-Object { 'y' }
        $accept | & $sdkManager `
            "--sdk_root=$sdk" `
            "platforms;android-$($lock.platformApi)" `
            "build-tools;$($lock.buildToolsVersion)"
        if ($LASTEXITCODE -ne 0) { throw "sdkmanager failed with exit code $LASTEXITCODE" }
    } finally {
        $env:JAVA_HOME = $previousJavaHome
    }

    $packagedRuntime = Join-Path $repoRoot 'toolchain/runtime'
    $developmentRuntime = Join-Path $repoRoot 'build/runtime-dist/runtime-v1'
    if (Test-Path -LiteralPath (Join-Path $packagedRuntime 'classes.dex')) {
        $runtimeDirectory = Join-Path $working 'runtime'
        Copy-Item -LiteralPath $packagedRuntime -Destination $runtimeDirectory -Recurse
    } elseif (Test-Path -LiteralPath (Join-Path $developmentRuntime 'classes.dex')) {
        $runtimeDirectory = $developmentRuntime
    } else {
        throw 'Runtime Bundle was not found. Use the release package or build it with tools/package-runtime.ps1 first.'
    }

    & (Join-Path $PSScriptRoot 'assemble-minimal-toolchain.ps1') `
        -AndroidSdk $sdk `
        -JavaHome $jreExtract `
        -RuntimeDirectory $runtimeDirectory `
        -Destination $destinationPath `
        -Force:$Force
    if ($LASTEXITCODE -ne 0) { throw "Minimal toolchain assembly failed with exit code $LASTEXITCODE" }

    Write-Host 'Minimal toolchain is ready.' -ForegroundColor Green
    Write-Host $destinationPath
} finally {
    if (Test-Path -LiteralPath $working) { Remove-Item -LiteralPath $working -Recurse -Force }
}
