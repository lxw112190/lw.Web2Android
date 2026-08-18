[CmdletBinding()]
param(
    [string]$SdkRoot = $env:ANDROID_SDK_ROOT,
    [string]$BuildToolsVersion,
    [int]$PlatformApi = 0
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$lock = Get-Content -Raw -LiteralPath (Join-Path $repoRoot 'toolchain.lock.json') | ConvertFrom-Json

if ([string]::IsNullOrWhiteSpace($SdkRoot)) {
    $SdkRoot = $env:ANDROID_HOME
}
if ([string]::IsNullOrWhiteSpace($SdkRoot)) {
    throw 'ANDROID_SDK_ROOT or ANDROID_HOME must point to an Android SDK installation.'
}
if ([string]::IsNullOrWhiteSpace($BuildToolsVersion)) {
    $BuildToolsVersion = [string]$lock.buildToolsVersion
}
if ($PlatformApi -eq 0) {
    $PlatformApi = [int]$lock.platformApi
}

$sdkPath = (Resolve-Path -LiteralPath $SdkRoot).Path
$androidJar = Join-Path $sdkPath "platforms/android-$PlatformApi/android.jar"
$buildTools = Join-Path $sdkPath "build-tools/$BuildToolsVersion"

function Resolve-AndroidTool([string]$Name) {
    foreach ($candidate in @(
        (Join-Path $buildTools $Name),
        (Join-Path $buildTools "$Name.exe"),
        (Join-Path $buildTools "$Name.bat")
    )) {
        if (Test-Path -LiteralPath $candidate -PathType Leaf) {
            return $candidate
        }
    }
    throw "Android build tool '$Name' was not found in '$buildTools'."
}

if (-not (Test-Path -LiteralPath $androidJar -PathType Leaf)) {
    throw "Android platform jar was not found: $androidJar"
}

$aapt2 = Resolve-AndroidTool 'aapt2'
$d8 = Resolve-AndroidTool 'd8'
$zipalign = Resolve-AndroidTool 'zipalign'
$apksigner = Resolve-AndroidTool 'apksigner'

$buildRoot = [System.IO.Path]::GetFullPath((Join-Path $repoRoot 'build/m0'))
$expectedBuildRoot = [System.IO.Path]::GetFullPath((Join-Path $repoRoot 'build/m0'))
if ($buildRoot -ne $expectedBuildRoot -or -not $buildRoot.StartsWith($repoRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "Refusing to clean unexpected build path: $buildRoot"
}
if (Test-Path -LiteralPath $buildRoot) {
    Remove-Item -LiteralPath $buildRoot -Recurse -Force
}

$classDir = Join-Path $buildRoot 'classes'
$dexDir = Join-Path $buildRoot 'dex'
$assetsDir = Join-Path $buildRoot 'assets'
$compiledDir = Join-Path $buildRoot 'compiled-res'
$outputDir = Join-Path $buildRoot 'out'
foreach ($directory in @($classDir, $dexDir, $assetsDir, $compiledDir, $outputDir)) {
    New-Item -ItemType Directory -Path $directory | Out-Null
}
New-Item -ItemType Directory -Path (Join-Path $assetsDir 'www') | Out-Null
Copy-Item -LiteralPath (Join-Path $repoRoot 'samples/hello/index.html') -Destination (Join-Path $assetsDir 'www/index.html')

$javaSource = Join-Path $repoRoot 'runtime/src/com/lw/web2android/runtime/MainActivity.java'
$javaSourceText = Get-Content -Raw -LiteralPath $javaSource
if ($javaSourceText -notmatch '(?m)^package\s+com\.lw\.web2android\.runtime\s*;') {
    throw 'Runtime namespace must remain com.lw.web2android.runtime.'
}
if ($javaSourceText -match '\bR\s*\.') {
    throw 'Runtime source must not reference generated R resources.'
}
& javac -encoding UTF-8 -source 8 -target 8 -bootclasspath $androidJar -d $classDir $javaSource
if ($LASTEXITCODE -ne 0) { throw "javac failed with exit code $LASTEXITCODE." }

$runtimeJar = Join-Path $buildRoot 'runtime.jar'
& jar --create --file $runtimeJar -C $classDir .
if ($LASTEXITCODE -ne 0) { throw "jar failed with exit code $LASTEXITCODE." }
& $d8 --min-api 23 --lib $androidJar --output $dexDir $runtimeJar
if ($LASTEXITCODE -ne 0) { throw "D8 failed with exit code $LASTEXITCODE." }

& $aapt2 compile --dir (Join-Path $repoRoot 'm0/res') -o $compiledDir
if ($LASTEXITCODE -ne 0) { throw "AAPT2 compile failed with exit code $LASTEXITCODE." }

$flatResources = @(Get-ChildItem -LiteralPath $compiledDir -Filter '*.flat' -File -Recurse | ForEach-Object FullName)
if ($flatResources.Count -eq 0) {
    throw 'AAPT2 compile produced no .flat resources.'
}

$resourceApk = Join-Path $buildRoot 'resources.apk'
$linkArgs = @(
    'link', '-o', $resourceApk,
    '--manifest', (Join-Path $repoRoot 'm0/AndroidManifest.xml'),
    '-I', $androidJar,
    '-A', $assetsDir,
    '--min-sdk-version', '23',
    '--target-sdk-version', [string]$PlatformApi,
    '--version-code', '1',
    '--version-name', '0.1.0'
) + $flatResources
& $aapt2 @linkArgs
if ($LASTEXITCODE -ne 0) { throw "AAPT2 link failed with exit code $LASTEXITCODE." }

$unsignedApk = Join-Path $buildRoot 'unsigned.apk'
Copy-Item -LiteralPath $resourceApk -Destination $unsignedApk
Add-Type -AssemblyName System.IO.Compression.FileSystem
$apkArchive = [System.IO.Compression.ZipFile]::Open($unsignedApk, [System.IO.Compression.ZipArchiveMode]::Update)
try {
    [System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile(
        $apkArchive,
        (Join-Path $dexDir 'classes.dex'),
        'classes.dex',
        [System.IO.Compression.CompressionLevel]::Optimal
    ) | Out-Null
}
finally {
    $apkArchive.Dispose()
}

$alignedApk = Join-Path $buildRoot 'aligned.apk'
& $zipalign -f -p 4 $unsignedApk $alignedApk
if ($LASTEXITCODE -ne 0) { throw "zipalign failed with exit code $LASTEXITCODE." }
& $zipalign -c -p 4 $alignedApk
if ($LASTEXITCODE -ne 0) { throw "zipalign verification failed with exit code $LASTEXITCODE." }

$testKeyStore = Join-Path $buildRoot 'm0-ephemeral-debug.p12'
& keytool -genkeypair -noprompt -storetype PKCS12 -keystore $testKeyStore -storepass android -keypass android -alias m0-debug -keyalg RSA -keysize 2048 -validity 10000 -dname 'CN=lw.Web2Android M0,OU=Development,O=lw,L=Local,ST=Local,C=CN'
if ($LASTEXITCODE -ne 0) { throw "keytool failed with exit code $LASTEXITCODE." }

$signedApk = Join-Path $outputDir 'sample-debug.apk'
& $apksigner sign --ks $testKeyStore --ks-key-alias m0-debug --ks-pass pass:android --key-pass pass:android --out $signedApk $alignedApk
if ($LASTEXITCODE -ne 0) { throw "apksigner sign failed with exit code $LASTEXITCODE." }
& $apksigner verify --verbose --print-certs $signedApk
if ($LASTEXITCODE -ne 0) { throw "apksigner verify failed with exit code $LASTEXITCODE." }

$requiredEntries = @('AndroidManifest.xml', 'classes.dex', 'resources.arsc', 'assets/www/index.html')
$verifyArchive = [System.IO.Compression.ZipFile]::OpenRead($signedApk)
try {
    $entryNames = @($verifyArchive.Entries | ForEach-Object FullName)
    foreach ($requiredEntry in $requiredEntries) {
        if ($requiredEntry -notin $entryNames) {
            throw "Signed APK is missing required entry: $requiredEntry"
        }
    }
}
finally {
    $verifyArchive.Dispose()
}

$hash = (Get-FileHash -LiteralPath $signedApk -Algorithm SHA256).Hash.ToLowerInvariant()
Set-Content -LiteralPath "$signedApk.sha256" -Value "$hash  sample-debug.apk" -Encoding ascii
Write-Host "M0 APK created: $signedApk"
Write-Host "SHA-256: $hash"
