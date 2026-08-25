[CmdletBinding()]
param(
    [string]$BuildDirectory = 'build/packer/Release',
    [string]$ToolchainDirectory
)

$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$build = (Resolve-Path -LiteralPath (Join-Path $repoRoot $BuildDirectory)).Path
if ([string]::IsNullOrWhiteSpace($ToolchainDirectory)) { $ToolchainDirectory = Join-Path $repoRoot 'toolchain' }
$toolchain = (Resolve-Path -LiteralPath $ToolchainDirectory).Path
$version = '0.2.10'
$packageName = "lw-Web2Android-v$version-windows-x64-complete-private"
$releaseRoot = Join-Path $repoRoot 'build/releases'
$package = Join-Path $releaseRoot $packageName
$archive = Join-Path $releaseRoot "$packageName.zip"
$utf8NoBom = [System.Text.UTF8Encoding]::new($false)

if (-not (Test-Path -LiteralPath (Join-Path $toolchain 'metadata.json'))) {
    throw "Minimal toolchain is incomplete: $toolchain"
}
foreach ($binary in @('lw.Web2Android.exe','lw.Web2Android.GUI.exe')) {
    if (-not (Test-Path -LiteralPath (Join-Path $build $binary))) { throw "Release binary was not found: $binary" }
}

New-Item -ItemType Directory -Force -Path $releaseRoot | Out-Null
if (Test-Path -LiteralPath $package) { Remove-Item -LiteralPath $package -Recurse -Force }
if (Test-Path -LiteralPath $archive) { Remove-Item -LiteralPath $archive -Force }
New-Item -ItemType Directory -Force -Path "$package/bin","$package/tools","$package/docs/assets","$package/third_party/licenses" | Out-Null

Copy-Item -LiteralPath (Join-Path $build 'lw.Web2Android.exe'),(Join-Path $build 'lw.Web2Android.GUI.exe') -Destination "$package/bin"
Copy-Item -LiteralPath $toolchain -Destination "$package/toolchain" -Recurse
Copy-Item -LiteralPath (Join-Path $repoRoot 'tools/install-minimal-toolchain.ps1'),(Join-Path $repoRoot 'tools/assemble-minimal-toolchain.ps1') -Destination "$package/tools"
Copy-Item -LiteralPath (Join-Path $repoRoot 'README.md') -Destination "$package/docs/README.md"
Copy-Item -LiteralPath (Join-Path $repoRoot 'README_EN.md') -Destination "$package/docs/README_EN.md"
Copy-Item -LiteralPath (Join-Path $repoRoot 'packer/README.md') -Destination "$package/docs/PACKER.md"
Copy-Item -LiteralPath (Join-Path $repoRoot 'assets/sponsor.jpg') -Destination "$package/docs/assets"
New-Item -ItemType Directory -Force -Path "$package/assets" | Out-Null
Copy-Item -LiteralPath (Join-Path $repoRoot 'assets/default-app-icon.png') -Destination "$package/assets"
Copy-Item -LiteralPath (Join-Path $repoRoot 'LICENSE'),(Join-Path $repoRoot 'THIRD-PARTY-NOTICES.md'),(Join-Path $repoRoot 'toolchain.lock.json') -Destination $package
Copy-Item -LiteralPath (Join-Path $repoRoot 'third_party/licenses/spdlog-LICENSE.txt') -Destination "$package/third_party/licenses"

$notice = @'
# Private complete toolchain package

This archive was assembled locally after the local user accepted the Android SDK License.
It is intended for that user's private use. Do not upload or redistribute the included Android SDK
components unless you have independently confirmed that the applicable licenses permit it.

Android SDK License: https://developer.android.com/studio/terms
Eclipse Temurin notices and licenses are preserved inside toolchain/jre/.
'@
[System.IO.File]::WriteAllText("$package/PRIVATE-TOOLCHAIN-NOTICE.md", $notice + [Environment]::NewLine, $utf8NoBom)

& (Join-Path $repoRoot 'tools/write-distribution-metadata.ps1') -PackageDirectory $package -Version $version -Commit 'local' -IncludesPrivateToolchain
$root = (Resolve-Path $package).Path
$checksums = Get-ChildItem -LiteralPath $root -Recurse -File | Sort-Object FullName | ForEach-Object {
    $relative = [System.IO.Path]::GetRelativePath($root, $_.FullName).Replace('\', '/')
    $hash = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
    "$hash  $relative"
}
[System.IO.File]::WriteAllLines("$package/SHA256SUMS.txt", [string[]]$checksums, $utf8NoBom)
Compress-Archive -LiteralPath $package -DestinationPath $archive -CompressionLevel Optimal
$archiveHash = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant()
"$archiveHash  $packageName.zip" | Set-Content -LiteralPath "$archive.sha256" -Encoding ascii

Write-Host "Private complete release: $archive" -ForegroundColor Green
Write-Host "SHA-256: $archiveHash"
