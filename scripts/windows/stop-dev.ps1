param(
    [switch]$Force
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$RootDir = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$LogDir = Join-Path $RootDir "Log"
$BackendPidFile = Join-Path $LogDir "facescan-backend.pid"
$FrontendPidFile = Join-Path $LogDir "facescan-frontend.pid"

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

function Get-ChildProcessIds {
    param([int]$ProcessId)
    Get-CimInstance Win32_Process -Filter "ParentProcessId=$ProcessId" -ErrorAction SilentlyContinue |
        ForEach-Object { [int]$_.ProcessId }
}

function Stop-ProcessTree {
    param([int]$ProcessId)

    foreach ($childPid in Get-ChildProcessIds -ProcessId $ProcessId) {
        Stop-ProcessTree -ProcessId $childPid
    }

    if (Test-ProcessId -ProcessId $ProcessId) {
        Stop-Process -Id $ProcessId -Force:$Force -ErrorAction SilentlyContinue
    }
}

function Stop-TrackedProcess {
    param(
        [string]$Name,
        [string]$PidFile
    )

    $pidValue = Get-PidFromFile -Path $PidFile
    if (-not (Test-ProcessId -ProcessId $pidValue)) {
        Write-Host "$Name is not running."
        Remove-Item -Force $PidFile -ErrorAction SilentlyContinue
        return
    }

    Stop-ProcessTree -ProcessId $pidValue
    Start-Sleep -Milliseconds 500

    if (Test-ProcessId -ProcessId $pidValue) {
        Stop-Process -Id $pidValue -Force -ErrorAction SilentlyContinue
    }

    Remove-Item -Force $PidFile -ErrorAction SilentlyContinue
    Write-Host "$Name stopped."
}

Stop-TrackedProcess -Name "Frontend" -PidFile $FrontendPidFile
Stop-TrackedProcess -Name "Backend" -PidFile $BackendPidFile
