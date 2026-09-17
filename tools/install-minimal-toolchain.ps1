[CmdletBinding()]
param(
    [string]$Destination,
    [switch]$AcceptAndroidSdkLicense,
    [switch]$Force,
    [switch]$KeepWorkDirectoryOnFailure
)

$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$logFile = Join-Path $repoRoot 'logs/toolchain-init.log'
$logMaxFileSize = 2MB
$logMaxArchives = 5
$logEncoding = [System.Text.UTF8Encoding]::new($false)
$working = $null
$workspaceDrive = $null
$ioRoot = $null
$initializationSucceeded = $false
$currentStage = 'startup'
# This is a conservative warning threshold rather than a Windows hard limit;
# archive entries and the final JRE tree add substantially deeper paths.
$shortDriveThreshold = 80
$destinationPathWarningThreshold = 100

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

function Set-InitializationStage([string]$Stage) {
    $script:currentStage = $Stage
    Write-ToolchainLog 'INFO' "Initialization stage: $Stage"
}

function Get-PathLength([string]$Path) {
    if ([string]::IsNullOrWhiteSpace($Path)) { return 0 }
    try {
        return ([System.IO.Path]::GetFullPath($Path)).Length
    } catch {
        return $Path.Length
    }
}

function New-ToolchainWorkspace {
    $tempRoot = [System.IO.Path]::GetTempPath()
    if ([string]::IsNullOrWhiteSpace($tempRoot)) {
        throw 'Windows temporary directory could not be resolved.'
    }

    $workingRoot = Join-Path $tempRoot 'lw.Web2Android'
    New-Item -ItemType Directory -Force -Path $workingRoot | Out-Null
    for ($attempt = 0; $attempt -lt 16; ++$attempt) {
        $sessionId = [guid]::NewGuid().ToString('N').Substring(0, 8)
        $candidate = Join-Path $workingRoot "toolchain-$sessionId"
        if (-not (Test-Path -LiteralPath $candidate)) {
            New-Item -ItemType Directory -Path $candidate | Out-Null
            return $candidate
        }
    }

    throw 'Unable to allocate a unique temporary toolchain workspace.'
}

function Set-WorkspaceIoRoot([string]$Root) {
    if ([string]::IsNullOrWhiteSpace($Root)) {
        throw 'Workspace I/O root cannot be empty.'
    }

    $script:ioRoot = $Root
    $script:downloads = Join-Path $Root 'downloads'
    $script:sdk = Join-Path $Root 'android-sdk'
    $script:jreExtract = Join-Path $Root 'jre'
}

function Test-DriveLetterAvailable([string]$Letter) {
    $name = $Letter.TrimEnd(':')
    if (Get-PSDrive -Name $name -PSProvider FileSystem -ErrorAction SilentlyContinue) {
        return $false
    }
    if (Test-Path "$name`:\") { return $false }
    return $true
}

function New-TemporaryDriveMapping([string]$TargetPath) {
    if ([string]::IsNullOrWhiteSpace($TargetPath)) { return $null }
    $substExecutable = Join-Path $env:SystemRoot 'System32\subst.exe'
    if (-not (Test-Path -LiteralPath $substExecutable -PathType Leaf)) {
        Write-ToolchainLog 'WARN' "subst.exe was not found: $substExecutable"
        return $null
    }

    foreach ($letter in @('W', 'V', 'U', 'T', 'S', 'R')) {
        if (-not (Test-DriveLetterAvailable $letter)) { continue }
        $drive = "${letter}:"
        try {
            Write-ToolchainLog 'INFO' "Trying temporary drive mapping; drive=$drive; target=$TargetPath"
            $output = @(& $substExecutable $drive $TargetPath 2>&1)
            $exitCode = $LASTEXITCODE
            if ($exitCode -eq 0 -and (Test-Path "$drive\")) {
                Write-LoggedHost 'INFO' "Using temporary short-path mapping: $drive -> $TargetPath"
                Write-ToolchainLog 'INFO' "Temporary drive mapping created; drive=$drive; target=$TargetPath"
                return $drive
            }
            Write-ToolchainLog 'WARN' "subst failed; drive=$drive; exitCode=$exitCode; output=$($output -join ' ')"
            if ($exitCode -eq 0) {
                & $substExecutable $drive /D 2>&1 | Out-Null
            }
        } catch {
            Write-ToolchainLog 'WARN' "Unable to create temporary drive mapping $drive`: $($_.Exception.Message)"
        }
    }

    return $null
}

function Remove-TemporaryDriveMapping([string]$Drive) {
    if ([string]::IsNullOrWhiteSpace($Drive)) { return }
    try {
        $substExecutable = Join-Path $env:SystemRoot 'System32\subst.exe'
        if (-not (Test-Path -LiteralPath $substExecutable -PathType Leaf)) {
            Write-ToolchainLog 'WARN' "Unable to remove temporary drive mapping because subst.exe was not found: $Drive"
            return
        }
        $output = @(& $substExecutable $Drive /D 2>&1)
        $exitCode = $LASTEXITCODE
        if ($exitCode -eq 0) {
            Write-ToolchainLog 'INFO' "Temporary drive mapping removed: $Drive"
        } else {
            Write-ToolchainLog 'WARN' "Unable to remove temporary drive mapping; drive=$Drive; exitCode=$exitCode; output=$($output -join ' ')"
        }
    } catch {
        Write-ToolchainLog 'WARN' "Unable to remove temporary drive mapping $Drive`: $($_.Exception.Message)"
    }
}

function Require-Directory([string]$Path, [string]$Label) {
    if (-not (Test-Path -LiteralPath $Path -PathType Container)) {
        throw "$Label was not found: $Path"
    }
}

function Require-File([string]$Path, [string]$Label) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "$Label was not found: $Path"
    }
}

function Test-PathLengthFailure([System.Management.Automation.ErrorRecord]$ErrorRecord) {
    $exception = $ErrorRecord.Exception
    while ($exception) {
        if ($exception -is [System.IO.PathTooLongException]) { return $true }
        if (('{0:X8}' -f ($exception.HResult -band 0xffffffff)) -eq '800700CE') { return $true }
        $exception = $exception.InnerException
    }

    $details = $ErrorRecord.ToString()
    return $details -match '(?i)(path|file name|filename).{0,40}too long|too long.{0,40}(path|file name|filename)|文件名或扩展名太长|路径太长'
}

function Expand-WorkspaceArchive(
    [string]$ArchivePath,
    [string]$RelativeDestination,
    [string]$Label
) {
    $destination = Join-Path $script:ioRoot $RelativeDestination
    Write-LoggedHost 'INFO' "Extracting $Label ..."
    Write-ToolchainLog 'INFO' "Extracting $Label; archive=$ArchivePath; destination=$destination; destinationLength=$(Get-PathLength $destination)"
    try {
        Expand-Archive -LiteralPath $ArchivePath -DestinationPath $destination -Force
        return $destination
    } catch {
        $firstError = $_
        Write-ToolchainLog 'WARN' "Initial $Label extraction failed: $($firstError.Exception.Message)"
        if ($script:workspaceDrive -or -not (Test-PathLengthFailure $firstError)) { throw }

        Write-LoggedHost 'WARN' "$Label extraction encountered a Windows long-path error; trying a temporary short-path mapping."
        $mappedDrive = New-TemporaryDriveMapping $script:working
        if (-not $mappedDrive) {
            Write-ToolchainLog 'WARN' 'Short-path retry was unavailable; preserving the original extraction error.'
            throw
        }

        $script:workspaceDrive = $mappedDrive
        Set-WorkspaceIoRoot "$mappedDrive\"
        $mappedArchive = Join-Path $script:downloads ([System.IO.Path]::GetFileName($ArchivePath))
        $destination = Join-Path $script:ioRoot $RelativeDestination
        if (Test-Path -LiteralPath $destination) {
            Remove-Item -LiteralPath $destination -Recurse -Force -ErrorAction Stop
        }
        Require-File $mappedArchive "$Label archive for short-path retry"
        Write-ToolchainLog 'INFO' "Retrying $Label extraction; archive=$mappedArchive; destination=$destination"
        Expand-Archive -LiteralPath $mappedArchive -DestinationPath $destination -Force
        return $destination
    }
}

function Remove-WorkspaceSafely([string]$Path) {
    if ([string]::IsNullOrWhiteSpace($Path) -or -not (Test-Path -LiteralPath $Path)) { return }
    try {
        Remove-Item -LiteralPath $Path -Recurse -Force -ErrorAction Stop
        Write-ToolchainLog 'INFO' "Temporary workspace removed: $Path"
    } catch {
        Write-ToolchainLog 'WARN' "Unable to remove temporary workspace '$Path': $($_.Exception.Message)"
        Write-Warning "Unable to remove temporary workspace automatically: $Path"
    }
}

function Invoke-LoggedExternal {
    param(
        [Parameter(Mandatory = $true)][string]$FilePath,
        [Parameter(Mandatory = $true)][object[]]$ArgumentList,
        [Parameter(Mandatory = $true)][string]$Label,
        [string[]]$InputLines,
        [switch]$AllowFailure
    )

    if (-not (Get-Command $FilePath -ErrorAction SilentlyContinue)) {
        throw "$Label executable was not found: $FilePath"
    }

    Write-ToolchainLog 'INFO' "Starting ${Label}: $FilePath $($ArgumentList -join ' ')"
    $previousErrorActionPreference = $ErrorActionPreference
    $exitCode = $null
    $outputLines = [System.Collections.Generic.List[string]]::new()
    try {
        # Windows PowerShell 5.1 wraps native stderr as ErrorRecord objects. Keep
        # those records in the log and decide success from the native exit code;
        # otherwise curl's normal progress output becomes a terminating error.
        $ErrorActionPreference = 'Continue'
        if ($null -ne $InputLines) {
            $InputLines | & $FilePath @ArgumentList 2>&1 | ForEach-Object {
                $line = $_.ToString()
                $outputLines.Add($line)
                Write-Host $line
                Write-ToolchainLog 'INFO' "[$Label] $line"
            }
        } else {
            & $FilePath @ArgumentList 2>&1 | ForEach-Object {
                $line = $_.ToString()
                $outputLines.Add($line)
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
    if ($AllowFailure) {
        return [pscustomobject]@{
            ExitCode = $exitCode
            Output = [string[]]$outputLines
        }
    }
    if ($exitCode -ne 0) { throw "$Label failed with exit code $exitCode" }
}

function Get-VerifiedArchive([string]$Url, [string]$Sha256, [string]$Name) {
    $file = Join-Path $downloads $Name
    Write-LoggedHost 'INFO' "Downloading $Name from $Url"
    $curlArguments = @(
        '-L', '--fail', '--retry', '3', '--silent', '--show-error',
        '--output', $file, $Url)
    $download = Invoke-LoggedExternal `
        -FilePath 'curl.exe' `
        -ArgumentList $curlArguments `
        -Label "download $Name" `
        -AllowFailure

    if ($download.ExitCode -ne 0) {
        $revocationOffline = $download.ExitCode -eq 35 -and
            (($download.Output -join "`n") -match 'CRYPT_E_REVOCATION_OFFLINE')
        if (-not $revocationOffline) {
            throw "download $Name failed with exit code $($download.ExitCode)"
        }

        # Windows curl uses Schannel. Some corporate networks block access to the
        # certificate revocation service even though the HTTPS endpoint itself is
        # reachable. Retry only this specific failure without the online revocation
        # lookup. Certificate-chain and hostname validation remain enabled, and the
        # downloaded archive must still match the pinned SHA-256 below.
        Write-LoggedHost 'WARN' "Windows certificate revocation service is unavailable; retrying $Name with Schannel online revocation checks disabled. TLS certificate validation and pinned SHA-256 verification remain enabled."
        if (Test-Path -LiteralPath $file) { Remove-Item -LiteralPath $file -Force }
        $retryArguments = @(
            '-L', '--fail', '--retry', '3', '--silent', '--show-error',
            '--ssl-no-revoke', '--output', $file, $Url)
        Invoke-LoggedExternal `
            -FilePath 'curl.exe' `
            -ArgumentList $retryArguments `
            -Label "download $Name (revocation-offline fallback)"
    }

    $actual = (Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash.ToLowerInvariant()
    Write-ToolchainLog 'INFO' "Downloaded $Name; SHA-256=$actual"
    if ($actual -ne $Sha256.ToLowerInvariant()) {
        throw "SHA-256 mismatch for $Name; expected $($Sha256.ToLowerInvariant()), actual $actual"
    }
    Write-ToolchainLog 'INFO' "SHA-256 verified for $Name"
    return $file
}

Write-LoggedHost 'INFO' "Toolchain initialization log: $logFile"
Write-ToolchainLog 'INFO' "Initialization started; applicationRoot=$repoRoot; licenseAccepted=$([bool]$AcceptAndroidSdkLicense); force=$([bool]$Force); keepWorkDirectoryOnFailure=$([bool]$KeepWorkDirectoryOnFailure); powershellVersion=$($PSVersionTable.PSVersion); powershellEdition=$($PSVersionTable.PSEdition); os=$([Environment]::OSVersion.VersionString)"

try {
    Set-InitializationStage 'validate-license'
    if (-not $AcceptAndroidSdkLicense) {
        throw 'Android SDK License acceptance is required. Review https://developer.android.com/studio/terms and rerun with -AcceptAndroidSdkLicense.'
    }

    Set-InitializationStage 'resolve-destination'
    if ([string]::IsNullOrWhiteSpace($Destination)) { $Destination = Join-Path $repoRoot 'toolchain' }
    $destinationPath = [System.IO.Path]::GetFullPath($Destination)
    $applicationRootLength = Get-PathLength $repoRoot
    $destinationLength = Get-PathLength $destinationPath
    Write-ToolchainLog 'INFO' "Destination resolved; path=$destinationPath; pathLength=$destinationLength; applicationRootLength=$applicationRootLength"
    if ($destinationLength -gt $destinationPathWarningThreshold) {
        Write-LoggedHost 'WARN' "The final toolchain path is relatively long ($destinationLength characters). Windows may reject deeply nested JRE files; consider extracting the application closer to a drive root if initialization fails during final assembly."
    }

    Set-InitializationStage 'load-toolchain-lock'
    $lockFile = Join-Path $repoRoot 'toolchain.lock.json'
    $lock = Get-Content -Raw -LiteralPath $lockFile | ConvertFrom-Json
    Write-ToolchainLog 'INFO' "Toolchain lock loaded; version=$($lock.toolchainVersion); platformApi=$($lock.platformApi); buildTools=$($lock.buildToolsVersion); commandLineTools=$($lock.commandLineToolsVersion)"

    Set-InitializationStage 'create-workspace'
    $working = New-ToolchainWorkspace
    $physicalWorkspaceLength = Get-PathLength $working
    Write-LoggedHost 'INFO' "Temporary workspace: $working"
    Write-ToolchainLog 'INFO' "Temporary workspace created; path=$working; pathLength=$physicalWorkspaceLength"

    $ioRoot = $working
    if ($physicalWorkspaceLength -gt $shortDriveThreshold) {
        Write-LoggedHost 'WARN' "Temporary workspace path is relatively long ($physicalWorkspaceLength characters). Trying a short temporary drive mapping."
        $workspaceDrive = New-TemporaryDriveMapping $working
        if ($workspaceDrive) {
            $ioRoot = "$workspaceDrive\"
        } else {
            Write-LoggedHost 'WARN' 'Unable to create a temporary short-path drive. Continuing with the original workspace path.'
            Write-ToolchainLog 'WARN' "Short-path mapping unavailable; continuing with physical workspace=$working"
        }
    }
    Set-WorkspaceIoRoot $ioRoot
    New-Item -ItemType Directory -Force -Path $downloads,$sdk | Out-Null
    Write-ToolchainLog 'INFO' "Workspace I/O root selected; physicalRoot=$working; ioRoot=$ioRoot"

    Set-InitializationStage 'download-command-line-tools'
    $commandLineArchive = Get-VerifiedArchive `
        $lock.commandLineToolsUrl `
        $lock.commandLineToolsSha256 `
        "commandlinetools-win-$($lock.commandLineToolsVersion).zip"

    Set-InitializationStage 'prepare-java-runtime'
    $bundledJre = Join-Path $repoRoot 'toolchain/jre'
    if (Test-Path -LiteralPath (Join-Path $bundledJre 'bin/java.exe')) {
        Write-LoggedHost 'INFO' 'Using the Temurin JRE included in the application directory.'
        Copy-Item -LiteralPath $bundledJre -Destination $jreExtract -Recurse
    } else {
        Write-ToolchainLog 'INFO' 'Bundled JRE was not found; downloading the locked Temurin JRE.'
        Set-InitializationStage 'download-java-runtime'
        $javaArchive = Get-VerifiedArchive `
            $lock.javaRuntimeUrl `
            $lock.javaRuntimeSha256 `
            "temurin-jre-$($lock.javaRuntimeVersion)-windows-x64.zip"
        Set-InitializationStage 'extract-java-runtime'
        $extract = Expand-WorkspaceArchive $javaArchive 'jre-extract' 'Temurin JRE'
        Require-Directory $extract 'Temurin JRE extraction directory'
        $javaHome = Get-ChildItem -LiteralPath $extract -Directory | Select-Object -First 1
        if (-not $javaHome) { throw "Temurin JRE archive has an unexpected layout: $javaArchive" }
        Move-Item -LiteralPath $javaHome.FullName -Destination $jreExtract -Force
    }
    Require-File (Join-Path $jreExtract 'bin/java.exe') 'Temurin Java runtime'
    Write-ToolchainLog 'INFO' "Java runtime ready: $jreExtract"

    Set-InitializationStage 'extract-command-line-tools'
    $commandLineExtract = Expand-WorkspaceArchive $commandLineArchive 'cmdline-tools-extract' 'Android command-line tools'
    Require-Directory $commandLineExtract 'Android command-line tools extraction directory'
    $extractedCommandLineTools = Join-Path $commandLineExtract 'cmdline-tools'
    Require-Directory $extractedCommandLineTools 'Android command-line tools directory'
    Require-File (Join-Path $extractedCommandLineTools 'bin/sdkmanager.bat') 'Android sdkmanager after extraction'
    $commandLineHome = Join-Path $sdk "cmdline-tools/$($lock.commandLineToolsVersion)"
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $commandLineHome) | Out-Null
    Move-Item -LiteralPath $extractedCommandLineTools -Destination $commandLineHome -Force

    $sdkManager = Join-Path $commandLineHome 'bin/sdkmanager.bat'
    Require-File $sdkManager 'Android sdkmanager after staging'
    Write-ToolchainLog 'INFO' "Android command-line tools ready; home=$commandLineHome; sdkmanager=$sdkManager"

    Set-InitializationStage 'install-android-sdk-components'
    $previousJavaHome = $env:JAVA_HOME
    $env:JAVA_HOME = $jreExtract
    try {
        Write-LoggedHost 'INFO' 'Installing the locked Android Platform and Build Tools from the official repository ...'
        Write-ToolchainLog 'INFO' "Installing Android SDK components; sdkRoot=$sdk; platform=android-$($lock.platformApi); buildTools=$($lock.buildToolsVersion)"
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

    Set-InitializationStage 'prepare-runtime'
    $packagedRuntime = Join-Path $repoRoot 'toolchain/runtime'
    $developmentRuntime = Join-Path $repoRoot 'build/runtime-dist/runtime-v6'
    if (Test-Path -LiteralPath (Join-Path $packagedRuntime 'classes.dex')) {
        $runtimeDirectory = Join-Path $ioRoot 'runtime'
        Copy-Item -LiteralPath $packagedRuntime -Destination $runtimeDirectory -Recurse
        Write-ToolchainLog 'INFO' "Using Runtime Bundle from the application directory: $packagedRuntime"
    } elseif (Test-Path -LiteralPath (Join-Path $developmentRuntime 'classes.dex')) {
        $runtimeDirectory = $developmentRuntime
        Write-ToolchainLog 'INFO' "Using development Runtime Bundle: $developmentRuntime"
    } else {
        throw 'Runtime Bundle was not found. Use the release package or build it with tools/package-runtime.ps1 first.'
    }

    Set-InitializationStage 'assemble-minimal-toolchain'
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

    Set-InitializationStage 'complete'
    $initializationSucceeded = $true
    Write-LoggedHost 'INFO' 'Minimal toolchain is ready.'
    Write-LoggedHost 'INFO' $destinationPath
    Write-ToolchainLog 'INFO' "Initialization completed successfully; destination=$destinationPath"
} catch {
    $pathLengthFailure = Test-PathLengthFailure $_
    Write-LoggedHost 'ERROR' "Toolchain initialization failed during stage '$currentStage': $($_.Exception.Message)"
    Write-ToolchainLog 'ERROR' "Initialization failed; stage=$currentStage; physicalWorkspace=$working; ioRoot=$ioRoot; workspaceDrive=$workspaceDrive; workspaceLength=$(Get-PathLength $working); destination=$destinationPath; destinationLength=$(Get-PathLength $destinationPath); pathLengthFailure=$pathLengthFailure"
    Write-ToolchainLog 'ERROR' $_.Exception.ToString()
    Write-Host ''
    Write-Host "Initialization stage: $currentStage" -ForegroundColor Yellow
    Write-Host "Initialization log:   $logFile" -ForegroundColor Yellow
    if ($working) { Write-Host "Temporary workspace:  $working" -ForegroundColor Yellow }
    if ($workspaceDrive) { Write-Host "Temporary mapping:    $workspaceDrive" -ForegroundColor Yellow }
    if ($pathLengthFailure) {
        Write-Host ''
        Write-Host 'The failure is related to Windows path length handling. The initializer attempted a short-path fallback when possible; see the initialization log for details.' -ForegroundColor Yellow
    }
    throw
} finally {
    if ($workspaceDrive) {
        Remove-TemporaryDriveMapping $workspaceDrive
    }
    if ($working -and (Test-Path -LiteralPath $working)) {
        if (-not $initializationSucceeded -and $KeepWorkDirectoryOnFailure) {
            Write-LoggedHost 'WARN' "Initialization failed; temporary workspace was preserved for diagnostics: $working"
            Write-ToolchainLog 'WARN' "Temporary workspace preserved because KeepWorkDirectoryOnFailure was specified: $working"
        } else {
            Remove-WorkspaceSafely $working
        }
    }
    Write-ToolchainLog 'INFO' "Initialization process finished; success=$initializationSucceeded; finalStage=$currentStage"
}
