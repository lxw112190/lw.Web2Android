[CmdletBinding()]
param(
    [string]$BuildDirectory = 'build/packer-verified-v026/Release',
    [string]$ToolchainDirectory = 'toolchain',
    [string]$DestinationDirectory = 'build/releases'
)

$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$version = '0.2.6'
$packageName = "lw-Web2Android-v$version-vs2022-complete-private"
$utf8NoBom = [System.Text.UTF8Encoding]::new($false)

function Resolve-RepositoryPath([string]$Path) {
    if ([System.IO.Path]::IsPathRooted($Path)) { return (Resolve-Path -LiteralPath $Path).Path }
    return (Resolve-Path -LiteralPath (Join-Path $repoRoot $Path)).Path
}

$build = Resolve-RepositoryPath $BuildDirectory
$toolchain = Resolve-RepositoryPath $ToolchainDirectory
$releaseRoot = if ([System.IO.Path]::IsPathRooted($DestinationDirectory)) {
    [System.IO.Path]::GetFullPath($DestinationDirectory)
} else {
    [System.IO.Path]::GetFullPath((Join-Path $repoRoot $DestinationDirectory))
}
$package = Join-Path $releaseRoot $packageName
$archive = "$package.zip"

foreach ($binary in @('lw.Web2Android.exe', 'lw.Web2Android.GUI.exe')) {
    if (-not (Test-Path -LiteralPath (Join-Path $build $binary) -PathType Leaf)) {
        throw "Release binary was not found: $(Join-Path $build $binary)"
    }
}
$requiredToolchainFiles = @(
    'aapt2.exe', 'zipalign.exe', 'android.jar', 'apksigner/apksigner.jar',
    'jre/bin/java.exe', 'runtime/classes.dex', 'runtime/metadata.json',
    'runtime-v6.zip', 'metadata.json'
)
foreach ($relative in $requiredToolchainFiles) {
    if (-not (Test-Path -LiteralPath (Join-Path $toolchain $relative) -PathType Leaf)) {
        throw "Complete toolchain file was not found: $relative"
    }
}
$runtimeMetadata = Get-Content -Raw -LiteralPath (Join-Path $toolchain 'runtime/metadata.json') | ConvertFrom-Json
if ([string]$runtimeMetadata.runtimeVersion -ne '6') {
    throw "The development package requires Runtime v6; found $($runtimeMetadata.runtimeVersion)"
}
if (-not (Test-Path -LiteralPath (Join-Path $repoRoot '.deps/spdlog.tar.gz') -PathType Leaf)) {
    throw 'Offline dependency archive was not found: .deps/spdlog.tar.gz'
}

New-Item -ItemType Directory -Force -Path $releaseRoot | Out-Null
if (Test-Path -LiteralPath $package) { Remove-Item -LiteralPath $package -Recurse -Force }
if (Test-Path -LiteralPath $archive) { Remove-Item -LiteralPath $archive -Force }
New-Item -ItemType Directory -Force -Path $package | Out-Null

$sourceDirectories = @('.github', 'assets', 'packer', 'runtime', 'samples', 'third_party', 'tools', 'vs2022')
foreach ($directory in $sourceDirectories) {
    Copy-Item -LiteralPath (Join-Path $repoRoot $directory) -Destination $package -Recurse
}
New-Item -ItemType Directory -Force -Path (Join-Path $package '.deps') | Out-Null
$spdlogArchive = Join-Path $repoRoot '.deps/spdlog.tar.gz'
$packageDependencies = Join-Path $package '.deps'
Copy-Item -LiteralPath $spdlogArchive -Destination $packageDependencies
$tar = Get-Command tar.exe -ErrorAction SilentlyContinue
if (-not $tar) { throw 'Windows tar.exe was not found; unable to prepare the VS2022 IntelliSense headers.' }
& $tar.Source -xzf $spdlogArchive -C $packageDependencies
if ($LASTEXITCODE -ne 0) { throw "Unable to extract the offline spdlog archive; tar exit code: $LASTEXITCODE" }
$extractedSpdlog = Join-Path $packageDependencies 'spdlog-1.17.0'
if (-not (Test-Path -LiteralPath (Join-Path $extractedSpdlog 'include/spdlog/spdlog.h') -PathType Leaf)) {
    throw 'The offline spdlog archive has an unexpected layout.'
}
Move-Item -LiteralPath $extractedSpdlog -Destination (Join-Path $packageDependencies 'spdlog-src')
Copy-Item -LiteralPath $toolchain -Destination (Join-Path $package 'toolchain') -Recurse
$obsoleteRuntimeArchive = Join-Path $package 'toolchain/runtime-v1.zip'
if (Test-Path -LiteralPath $obsoleteRuntimeArchive) {
    Remove-Item -LiteralPath $obsoleteRuntimeArchive -Force
}

$rootFiles = @(
    '.gitignore', '.vsconfig', 'CMakeLists.txt', 'CMakePresets.json', 'DEVELOPMENT.md',
    'lw.Web2Android.sln',
    'LICENSE', 'README.md', 'README_EN.md', 'THIRD-PARTY-NOTICES.md', 'toolchain.lock.json'
)
foreach ($file in $rootFiles) {
    Copy-Item -LiteralPath (Join-Path $repoRoot $file) -Destination $package
}
Copy-Item -LiteralPath (Join-Path $build 'lw.Web2Android.exe'),
                      (Join-Path $build 'lw.Web2Android.GUI.exe') -Destination $package

$notice = @'
# Private complete VS2022 development package

This package includes Android SDK components after the local user accepted the Android SDK License.
It is intended for private development use by users who have independently accepted and comply with
the applicable licenses. Do not upload this complete package as a public GitHub Release.

Android SDK License: https://developer.android.com/studio/terms
Eclipse Temurin license and notices are preserved under toolchain/jre/.
'@
[System.IO.File]::WriteAllText((Join-Path $package 'PRIVATE-TOOLCHAIN-NOTICE.md'),
                               $notice + [Environment]::NewLine, $utf8NoBom)

$packageRoot = (Resolve-Path -LiteralPath $package).Path
$checksums = Get-ChildItem -LiteralPath $packageRoot -Recurse -File | Sort-Object FullName | ForEach-Object {
    # Windows PowerShell 5.1 runs on .NET Framework, where Path.GetRelativePath
    # is unavailable. Every item is enumerated below $packageRoot, so a guarded
    # prefix removal is both portable and unambiguous.
    if (-not $_.FullName.StartsWith($packageRoot + [System.IO.Path]::DirectorySeparatorChar,
                                    [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Unexpected package file outside the package root: $($_.FullName)"
    }
    $relative = $_.FullName.Substring($packageRoot.Length + 1).Replace('\', '/')
    $hash = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
    "$hash  $relative"
}
[System.IO.File]::WriteAllLines((Join-Path $package 'SHA256SUMS.txt'), [string[]]$checksums, $utf8NoBom)
Compress-Archive -LiteralPath $package -DestinationPath $archive -CompressionLevel Optimal
$archiveHash = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant()
"$archiveHash  $packageName.zip" | Set-Content -LiteralPath "$archive.sha256" -Encoding ascii

Write-Host "VS2022 complete development folder: $package" -ForegroundColor Green
Write-Host "VS2022 complete development archive: $archive" -ForegroundColor Green
Write-Host "SHA-256: $archiveHash"
