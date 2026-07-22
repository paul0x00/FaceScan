param(
    [int]$BackendPort = 8080,
    [string]$FrontendHost = "127.0.0.1",
    [string]$VcpkgRoot = "",
    [switch]$InstallTools,
    [switch]$SkipBuild,
    [switch]$CleanBuild,
    [switch]$NoBackend,
    [switch]$NoFrontend
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$RootDir = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$LogDir = Join-Path $RootDir "Log"
$DepsDir = Join-Path $RootDir ".deps"
$BackendBuildDir = Join-Path $RootDir "backend\build"
$BackendExe = Join-Path $BackendBuildDir "Release\FaceScanBackend.exe"
$BackendPidFile = Join-Path $LogDir "facescan-backend.pid"
$FrontendPidFile = Join-Path $LogDir "facescan-frontend.pid"
$BackendOutLog = Join-Path $LogDir "facescan-backend.log"
$BackendErrLog = Join-Path $LogDir "facescan-backend.err.log"
$FrontendOutLog = Join-Path $LogDir "facescan-frontend.log"
$FrontendErrLog = Join-Path $LogDir "facescan-frontend.err.log"

function Write-Step {
    param([string]$Message)
    Write-Host ""
    Write-Host "==> $Message" -ForegroundColor Cyan
}

function Test-Command {
    param([string]$Name)
    return $null -ne (Get-Command $Name -ErrorAction SilentlyContinue)
}

function Update-PathFromEnvironment {
    $machinePath = [Environment]::GetEnvironmentVariable("Path", "Machine")
    $userPath = [Environment]::GetEnvironmentVariable("Path", "User")
    $env:Path = "$machinePath;$userPath"
}

function Install-WingetPackage {
    param(
        [string]$Id,
        [string]$Name,
        [string]$Override = ""
    )

    if (-not (Test-Command "winget")) {
        throw "winget is not available. Install $Name manually, then run this script again."
    }

    Write-Step "Installing $Name with winget"
    $args = @("install", "--id", $Id, "-e", "--source", "winget", "--accept-package-agreements", "--accept-source-agreements")
    if ($Override -ne "") {
        $args += @("--override", $Override)
    }
    & winget @args
    Update-PathFromEnvironment
}

function Ensure-Command {
    param(
        [string]$Command,
        [string]$PackageId,
        [string]$PackageName,
        [string]$Override = ""
    )

    if (Test-Command $Command) {
        return
    }

    if ($InstallTools) {
        Install-WingetPackage -Id $PackageId -Name $PackageName -Override $Override
        if (Test-Command $Command) {
            return
        }
    }

    throw "$PackageName is required. Install it or rerun with -InstallTools."
}

function Test-VsBuildTools {
    $vswherePaths = @(
        "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe",
        "${env:ProgramFiles}\Microsoft Visual Studio\Installer\vswhere.exe"
    )

    foreach ($path in $vswherePaths) {
        if (Test-Path $path) {
            $installPath = & $path -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
            if ($LASTEXITCODE -eq 0 -and $installPath) {
                return $true
            }
        }
    }

    return $false
}

function Ensure-VsBuildTools {
    if (Test-VsBuildTools) {
        return
    }

    if ($InstallTools) {
        Install-WingetPackage `
            -Id "Microsoft.VisualStudio.2022.BuildTools" `
            -Name "Visual Studio 2022 Build Tools" `
            -Override "--wait --quiet --norestart --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"
        if (Test-VsBuildTools) {
            return
        }
    }

    throw "Visual Studio 2022 Build Tools with C++ workload is required. Install it or rerun with -InstallTools."
}

function Ensure-Vcpkg {
    if ($VcpkgRoot -ne "") {
        $root = $VcpkgRoot
    } elseif ($env:VCPKG_ROOT -and (Test-Path $env:VCPKG_ROOT)) {
        $root = $env:VCPKG_ROOT
    } else {
        $root = Join-Path $DepsDir "vcpkg"
    }

    if (-not (Test-Path $root)) {
        Write-Step "Cloning vcpkg into $root"
        New-Item -ItemType Directory -Force -Path (Split-Path $root -Parent) | Out-Null
        & git clone https://github.com/microsoft/vcpkg.git $root
    }

    $vcpkgExe = Join-Path $root "vcpkg.exe"
    if (-not (Test-Path $vcpkgExe)) {
        Write-Step "Bootstrapping vcpkg"
        & (Join-Path $root "bootstrap-vcpkg.bat") -disableMetrics
    }

    if (-not (Test-Path $vcpkgExe)) {
        throw "Failed to bootstrap vcpkg at $root."
    }

    $env:VCPKG_ROOT = (Resolve-Path $root).Path
    Write-Host "VCPKG_ROOT=$env:VCPKG_ROOT"
}

function Get-PidFromFile {
    param([string]$Path)
    if (-not (Test-Path $Path)) {
        return $null
    }

    $text = (Get-Content -Raw $Path).Trim()
    if ($text -match "^\d+$") {
        return [int]$text
    }

    Remove-Item -Force $Path -ErrorAction SilentlyContinue
    return $null
}

function Test-ProcessId {
    param([Nullable[int]]$ProcessId)
    if ($null -eq $ProcessId) {
        return $false
    }

    try {
        Get-Process -Id $ProcessId -ErrorAction Stop | Out-Null
        return $true
    } catch {
        return $false
    }
}

function Start-LoggedProcess {
    param(
        [string]$Name,
        [string]$FilePath,
        [string[]]$ArgumentList,
        [string]$WorkingDirectory,
        [string]$PidFile,
        [string]$StdoutPath,
        [string]$StderrPath
    )

    $existingPid = Get-PidFromFile -Path $PidFile
    if (Test-ProcessId -ProcessId $existingPid) {
        Write-Host "$Name is already running with PID $existingPid."
        return
    }

    Remove-Item -Force $StdoutPath, $StderrPath -ErrorAction SilentlyContinue
    $process = Start-Process `
        -FilePath $FilePath `
        -ArgumentList $ArgumentList `
        -WorkingDirectory $WorkingDirectory `
        -RedirectStandardOutput $StdoutPath `
        -RedirectStandardError $StderrPath `
        -WindowStyle Hidden `
        -PassThru

    Set-Content -Path $PidFile -Value $process.Id -Encoding ascii
    Start-Sleep -Seconds 1

    if (-not (Test-ProcessId -ProcessId $process.Id)) {
        throw "$Name failed to start. Check $StdoutPath and $StderrPath."
    }

    Write-Host "$Name started with PID $($process.Id)."
}

function Ensure-FrontendDependencies {
    $nodeModules = Join-Path $RootDir "frontend\node_modules"
    if (Test-Path $nodeModules) {
        return
    }

    Write-Step "Installing frontend npm packages"
    $frontendDir = Join-Path $RootDir "frontend"
    $packageLock = Join-Path $frontendDir "package-lock.json"
    Push-Location $frontendDir
    try {
        if (Test-Path $packageLock) {
            & npm.cmd ci
        } else {
            & npm.cmd install
        }
    } finally {
        Pop-Location
    }
}

function Build-Backend {
    if ($CleanBuild -and (Test-Path $BackendBuildDir)) {
        Write-Step "Cleaning backend build directory"
        Remove-Item -Recurse -Force $BackendBuildDir
    }

    Write-Step "Configuring backend"
    & cmake -S (Join-Path $RootDir "backend") --preset windows

    Write-Step "Building backend"
    & cmake --build $BackendBuildDir --config Release

    if (-not (Test-Path $BackendExe)) {
        throw "Backend executable was not produced: $BackendExe"
    }
}

New-Item -ItemType Directory -Force -Path $LogDir | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $RootDir "backend\data\images") | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $RootDir "backend\data\db") | Out-Null

Write-Step "Checking required tools"
Ensure-Command -Command "git" -PackageId "Git.Git" -PackageName "Git"
Ensure-Command -Command "cmake" -PackageId "Kitware.CMake" -PackageName "CMake"
Ensure-Command -Command "node" -PackageId "OpenJS.NodeJS.LTS" -PackageName "Node.js LTS"
Ensure-Command -Command "npm.cmd" -PackageId "OpenJS.NodeJS.LTS" -PackageName "npm"
Ensure-VsBuildTools
Ensure-Vcpkg

if (-not $NoFrontend) {
    Ensure-FrontendDependencies
}

if (-not $SkipBuild -and -not $NoBackend) {
    Build-Backend
}

$backendStarted = $false
$frontendStarted = $false

if (-not $NoBackend) {
    Write-Step "Starting backend"
    Start-LoggedProcess `
        -Name "Backend" `
        -FilePath $BackendExe `
        -ArgumentList @("$BackendPort") `
        -WorkingDirectory $RootDir `
        -PidFile $BackendPidFile `
        -StdoutPath $BackendOutLog `
        -StderrPath $BackendErrLog
    Write-Host "Backend URL: http://127.0.0.1:$BackendPort"
    $backendStarted = $true
}

if (-not $NoFrontend) {
    Write-Step "Starting frontend"
    Start-LoggedProcess `
        -Name "Frontend" `
        -FilePath "npm.cmd" `
        -ArgumentList @("run", "dev", "--", "--host", $FrontendHost) `
        -WorkingDirectory (Join-Path $RootDir "frontend") `
        -PidFile $FrontendPidFile `
        -StdoutPath $FrontendOutLog `
        -StderrPath $FrontendErrLog
    Write-Host "Frontend URL: http://$FrontendHost`:5173"
    $frontendStarted = $true
}

Write-Host ""
if ($backendStarted -and $frontendStarted) {
    Write-Host "FaceScan dev environment is up." -ForegroundColor Green
} elseif ($backendStarted) {
    Write-Host "FaceScan backend is up." -ForegroundColor Green
} elseif ($frontendStarted) {
    Write-Host "FaceScan frontend is up." -ForegroundColor Green
} else {
    Write-Host "FaceScan setup checks completed." -ForegroundColor Green
}
Write-Host "Logs:"
Write-Host "  $BackendOutLog"
Write-Host "  $BackendErrLog"
Write-Host "  $FrontendOutLog"
Write-Host "  $FrontendErrLog"
Write-Host "Stop both with: powershell -ExecutionPolicy Bypass -File `"$PSScriptRoot\stop-dev.ps1`""
