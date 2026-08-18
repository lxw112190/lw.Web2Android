[CmdletBinding()]
param(
    [string]$Destination,
    [switch]$AcceptAndroidSdkLicense,
    [switch]$Force
)

$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$logFile = Join-Path $repoRoot 'logs/toolchain-init.log'
$logMaxFileSize = 2MB
$logMaxArchives = 5
$logEncoding = [System.Text.UTF8Encoding]::new($false)
$working = $null

function Get-ToolchainLogArchive([int]$Index) {
    $directory = [System.IO.Path]::GetDirectoryName($logFile)
    $baseName = [System.IO.Path]::GetFileNameWithoutExtension($logFile)
    $extension = [System.IO.Path]::GetExtension($logFile)
    return Join-Path $directory "$baseName.$Index$extension"
}

function Rotate-ToolchainLog([int]$IncomingBytes) {
    if (-not (Test-Path -LiteralPath $logFile -PathType Leaf)) { return }
    if ((Get-Item -LiteralPath $logFile).Length + $IncomingBytes -le $logMaxFileSize) { return }

    $oldest = Get-ToolchainLogArchive $logMaxArchives
    if (Test-Path -LiteralPath $oldest) { Remove-Item -LiteralPath $oldest -Force }
    for ($index = $logMaxArchives - 1; $index -ge 1; --$index) {
        $source = Get-ToolchainLogArchive $index
        if (Test-Path -LiteralPath $source) {
            Move-Item -LiteralPath $source -Destination (Get-ToolchainLogArchive ($index + 1)) -Force
        }
    }
    Move-Item -LiteralPath $logFile -Destination (Get-ToolchainLogArchive 1) -Force
}

function Write-ToolchainLog([string]$Level, [string]$Message) {
    try {
        $line = '{0} [{1}] [pid {2}] {3}{4}' -f `
            [DateTime]::UtcNow.ToString('yyyy-MM-ddTHH:mm:ss.fffZ'), `
            $Level.ToUpperInvariant(), `
            $PID, `
            $Message, `
            [Environment]::NewLine
        $bytes = $logEncoding.GetByteCount($line)
        New-Item -ItemType Directory -Force -Path ([System.IO.Path]::GetDirectoryName($logFile)) | Out-Null
        Rotate-ToolchainLog $bytes
        [System.IO.File]::AppendAllText($logFile, $line, $logEncoding)
    } catch {
        [System.Diagnostics.Debug]::WriteLine("lw.Web2Android toolchain logging failed: $($_.Exception.Message)")
    }
}

function Write-LoggedHost([string]$Level, [string]$Message) {
    Write-ToolchainLog $Level $Message
    if ($Level -eq 'ERROR') {
        Write-Host $Message -ForegroundColor Red
    } elseif ($Level -eq 'WARN') {
        Write-Host $Message -ForegroundColor Yellow
    } else {
        Write-Host $Message
    }
}

function Invoke-LoggedExternal {
    param(
        [Parameter(Mandatory = $true)][string]$FilePath,
        [Parameter(Mandatory = $true)][object[]]$ArgumentList,
        [Parameter(Mandatory = $true)][string]$Label,
        [string[]]$InputLines
    )

    if (-not (Get-Command $FilePath -ErrorAction SilentlyContinue)) {
        throw "$Label executable was not found: $FilePath"
    }

    Write-ToolchainLog 'INFO' "Starting ${Label}: $FilePath $($ArgumentList -join ' ')"
    $previousErrorActionPreference = $ErrorActionPreference
    $exitCode = $null
    try {
        # Windows PowerShell 5.1 wraps native stderr as ErrorRecord objects. Keep
        # those records in the log and decide success from the native exit code;
        # otherwise curl's normal progress output becomes a terminating error.
        $ErrorActionPreference = 'Continue'
        if ($null -ne $InputLines) {
            $InputLines | & $FilePath @ArgumentList 2>&1 | ForEach-Object {
                $line = $_.ToString()
                Write-Host $line
                Write-ToolchainLog 'INFO' "[$Label] $line"
            }
        } else {
            & $FilePath @ArgumentList 2>&1 | ForEach-Object {
                $line = $_.ToString()
                Write-Host $line
                Write-ToolchainLog 'INFO' "[$Label] $line"
            }
        }
        $exitCode = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $previousErrorActionPreference
    }
    if ($null -eq $exitCode) { $exitCode = -1 }
    Write-ToolchainLog 'INFO' "$Label exited with code $exitCode"
    if ($exitCode -ne 0) { throw "$Label failed with exit code $exitCode" }
}

function Get-VerifiedArchive([string]$Url, [string]$Sha256, [string]$Name) {
    $file = Join-Path $downloads $Name
    Write-LoggedHost 'INFO' "Downloading $Name from $Url"
    Invoke-LoggedExternal `
        -FilePath 'curl.exe' `
        -ArgumentList @('-L', '--fail', '--retry', '3', '--silent', '--show-error', '--output', $file, $Url) `
        -Label "download $Name"
    $actual = (Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash.ToLowerInvariant()
    Write-ToolchainLog 'INFO' "Downloaded $Name; SHA-256=$actual"
    if ($actual -ne $Sha256.ToLowerInvariant()) {
        throw "SHA-256 mismatch for $Name; expected $($Sha256.ToLowerInvariant()), actual $actual"
    }
    Write-ToolchainLog 'INFO' "SHA-256 verified for $Name"
    return $file
}

Write-LoggedHost 'INFO' "Toolchain initialization log: $logFile"
Write-ToolchainLog 'INFO' "Initialization started; applicationRoot=$repoRoot; licenseAccepted=$([bool]$AcceptAndroidSdkLicense); force=$([bool]$Force)"

try {
    if (-not $AcceptAndroidSdkLicense) {
        throw 'Android SDK License acceptance is required. Review https://developer.android.com/studio/terms and rerun with -AcceptAndroidSdkLicense.'
    }

    if ([string]::IsNullOrWhiteSpace($Destination)) { $Destination = Join-Path $repoRoot 'toolchain' }
    $destinationPath = [System.IO.Path]::GetFullPath($Destination)
    Write-ToolchainLog 'INFO' "Destination resolved: $destinationPath"

    $lockFile = Join-Path $repoRoot 'toolchain.lock.json'
    $lock = Get-Content -Raw -LiteralPath $lockFile | ConvertFrom-Json
    Write-ToolchainLog 'INFO' "Toolchain lock loaded; version=$($lock.toolchainVersion); platformApi=$($lock.platformApi); buildTools=$($lock.buildToolsVersion); commandLineTools=$($lock.commandLineToolsVersion)"

    $working = Join-Path ([System.IO.Path]::GetTempPath()) ('lw-web2android-toolchain-' + [guid]::NewGuid().ToString('N'))
    $downloads = Join-Path $working 'downloads'
    $sdk = Join-Path $working 'android-sdk'
    $jreExtract = Join-Path $working 'jre'
    New-Item -ItemType Directory -Force -Path $downloads,$sdk | Out-Null
    Write-ToolchainLog 'INFO' "Temporary workspace created: $working"

    $commandLineArchive = Get-VerifiedArchive `
        $lock.commandLineToolsUrl `
        $lock.commandLineToolsSha256 `
        "commandlinetools-win-$($lock.commandLineToolsVersion).zip"

    $bundledJre = Join-Path $repoRoot 'toolchain/jre'
    if (Test-Path -LiteralPath (Join-Path $bundledJre 'bin/java.exe')) {
        Write-LoggedHost 'INFO' 'Using the Temurin JRE included in the application directory.'
        Copy-Item -LiteralPath $bundledJre -Destination $jreExtract -Recurse
    } else {
        Write-ToolchainLog 'INFO' 'Bundled JRE was not found; downloading the locked Temurin JRE.'
        $javaArchive = Get-VerifiedArchive `
            $lock.javaRuntimeUrl `
            $lock.javaRuntimeSha256 `
            "temurin-jre-$($lock.javaRuntimeVersion)-windows-x64.zip"
        $extract = Join-Path $working 'jre-extract'
        Write-ToolchainLog 'INFO' "Extracting Temurin JRE: $javaArchive"
        Expand-Archive -LiteralPath $javaArchive -DestinationPath $extract
        $javaHome = Get-ChildItem -LiteralPath $extract -Directory | Select-Object -First 1
        if (-not $javaHome) { throw 'Temurin JRE archive has an unexpected layout' }
        Move-Item -LiteralPath $javaHome.FullName -Destination $jreExtract
    }
    Write-ToolchainLog 'INFO' "Java runtime ready: $jreExtract"

    $commandLineExtract = Join-Path $working 'cmdline-tools-extract'
    Write-ToolchainLog 'INFO' "Extracting Android command-line tools: $commandLineArchive"
    Expand-Archive -LiteralPath $commandLineArchive -DestinationPath $commandLineExtract
    $commandLineHome = Join-Path $sdk "cmdline-tools/$($lock.commandLineToolsVersion)"
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $commandLineHome) | Out-Null
    Copy-Item -LiteralPath (Join-Path $commandLineExtract 'cmdline-tools') -Destination $commandLineHome -Recurse

    $sdkManager = Join-Path $commandLineHome 'bin/sdkmanager.bat'
    $previousJavaHome = $env:JAVA_HOME
    $env:JAVA_HOME = $jreExtract
    try {
        Write-LoggedHost 'INFO' 'Installing the locked Android Platform and Build Tools from the official repository ...'
        $accept = 1..20 | ForEach-Object { 'y' }
        Invoke-LoggedExternal `
            -FilePath $sdkManager `
            -ArgumentList @(
                "--sdk_root=$sdk",
                "platforms;android-$($lock.platformApi)",
                "build-tools;$($lock.buildToolsVersion)") `
            -Label 'sdkmanager' `
            -InputLines $accept
    } finally {
        $env:JAVA_HOME = $previousJavaHome
        Write-ToolchainLog 'DEBUG' 'JAVA_HOME restored after sdkmanager.'
    }

    $packagedRuntime = Join-Path $repoRoot 'toolchain/runtime'
    $developmentRuntime = Join-Path $repoRoot 'build/runtime-dist/runtime-v3'
    if (Test-Path -LiteralPath (Join-Path $packagedRuntime 'classes.dex')) {
        $runtimeDirectory = Join-Path $working 'runtime'
        Copy-Item -LiteralPath $packagedRuntime -Destination $runtimeDirectory -Recurse
        Write-ToolchainLog 'INFO' "Using Runtime Bundle from the application directory: $packagedRuntime"
    } elseif (Test-Path -LiteralPath (Join-Path $developmentRuntime 'classes.dex')) {
        $runtimeDirectory = $developmentRuntime
        Write-ToolchainLog 'INFO' "Using development Runtime Bundle: $developmentRuntime"
    } else {
        throw 'Runtime Bundle was not found. Use the release package or build it with tools/package-runtime.ps1 first.'
    }

    Write-LoggedHost 'INFO' 'Assembling the minimal application toolchain ...'
    $assembleOutput = @(& (Join-Path $PSScriptRoot 'assemble-minimal-toolchain.ps1') `
        -AndroidSdk $sdk `
        -JavaHome $jreExtract `
        -RuntimeDirectory $runtimeDirectory `
        -Destination $destinationPath `
        -Force:$Force *>&1)
    foreach ($line in $assembleOutput) {
        Write-Host $line
        Write-ToolchainLog 'INFO' "[assemble] $($line.ToString())"
    }

    Write-LoggedHost 'INFO' 'Minimal toolchain is ready.'
    Write-LoggedHost 'INFO' $destinationPath
    Write-ToolchainLog 'INFO' 'Initialization completed successfully.'
} catch {
    Write-LoggedHost 'ERROR' "Toolchain initialization failed: $($_.Exception.Message)"
    Write-ToolchainLog 'ERROR' $_.Exception.ToString()
    Write-Host "Initialization log: $logFile" -ForegroundColor Yellow
    throw
} finally {
    if ($working -and (Test-Path -LiteralPath $working)) {
        try {
            Remove-Item -LiteralPath $working -Recurse -Force
            Write-ToolchainLog 'INFO' "Temporary workspace removed: $working"
        } catch {
            Write-ToolchainLog 'WARN' "Unable to remove temporary workspace $working`: $($_.Exception.Message)"
            Write-Warning "Unable to remove temporary workspace: $working"
        }
    }
    Write-ToolchainLog 'INFO' 'Initialization process finished.'
}
