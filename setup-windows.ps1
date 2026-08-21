[CmdletBinding()]
param(
    [string]$Msys2Root,
    [switch]$SkipUpdate,
    [switch]$SkipPathUpdate,
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

function Write-Step {
    param([string]$Message)
    Write-Host "`n==> $Message" -ForegroundColor Cyan
}

function Invoke-Native {
    param(
        [Parameter(Mandatory = $true)][string]$FilePath,
        [Parameter(Mandatory = $true)][AllowEmptyCollection()][string[]]$ArgumentList
    )

    & $FilePath @ArgumentList
    if ($LASTEXITCODE -ne 0) {
        throw "Command failed with exit code $LASTEXITCODE`: $FilePath $($ArgumentList -join ' ')"
    }
}

function Test-Msys2Root {
    param([AllowNull()][AllowEmptyString()][string]$Path)
    return -not [string]::IsNullOrWhiteSpace($Path) -and
        (Test-Path -LiteralPath (Join-Path $Path "usr\bin\bash.exe") -PathType Leaf)
}

function Find-Msys2Root {
    param([AllowNull()][AllowEmptyString()][string]$PreferredRoot)

    $candidates = [System.Collections.Generic.List[string]]::new()
    if (-not [string]::IsNullOrWhiteSpace($PreferredRoot)) {
        [void]$candidates.Add($PreferredRoot)
    }
    $environmentRoot = [Environment]::GetEnvironmentVariable("MSYS2_ROOT")
    if (-not [string]::IsNullOrWhiteSpace($environmentRoot)) {
        [void]$candidates.Add($environmentRoot)
    }
    [void]$candidates.Add((Join-Path $env:SystemDrive "msys64"))
    [void]$candidates.Add((Join-Path $env:LOCALAPPDATA "Programs\msys64"))
    [void]$candidates.Add((Join-Path $env:LOCALAPPDATA "msys64"))

    foreach ($candidate in $candidates) {
        if (Test-Msys2Root $candidate) {
            return (Resolve-Path -LiteralPath $candidate).Path
        }
    }

    $uninstallRoots = @(
        "HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\*",
        "HKLM:\Software\Microsoft\Windows\CurrentVersion\Uninstall\*",
        "HKLM:\Software\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\*"
    )
    foreach ($uninstallRoot in $uninstallRoots) {
        $entries = Get-ItemProperty -Path $uninstallRoot -ErrorAction SilentlyContinue |
            Where-Object {
                $displayName = $_.PSObject.Properties["DisplayName"]
                $null -ne $displayName -and $displayName.Value -like "MSYS2*"
            }
        foreach ($entry in $entries) {
            $installLocation = $entry.PSObject.Properties["InstallLocation"]
            if ($null -ne $installLocation -and (Test-Msys2Root $installLocation.Value)) {
                return (Resolve-Path -LiteralPath $installLocation.Value).Path
            }
        }
    }

    return $null
}

function Invoke-Msys2 {
    param(
        [Parameter(Mandatory = $true)][string]$BashPath,
        [Parameter(Mandatory = $true)][string]$Command
    )
    Invoke-Native -FilePath $BashPath -ArgumentList @("-lc", $Command)
}

function Add-UserPathEntries {
    param([Parameter(Mandatory = $true)][string[]]$Entries)

    $userPath = [Environment]::GetEnvironmentVariable("Path", "User")
    $parts = [System.Collections.Generic.List[string]]::new()
    foreach ($entry in $Entries) {
        $normalizedEntry = $entry.TrimEnd("\")
        $alreadyPresent = $false
        foreach ($part in ($userPath -split ";")) {
            if ($part.Trim().TrimEnd("\").Equals($normalizedEntry, [StringComparison]::OrdinalIgnoreCase)) {
                $alreadyPresent = $true
                break
            }
        }
        if (-not $alreadyPresent) {
            [void]$parts.Add($entry)
        }
    }

    if (-not [string]::IsNullOrWhiteSpace($userPath)) {
        [void]$parts.Add($userPath.Trim(";"))
    }

    if ($parts.Count -gt 0 -and ($parts -join ";") -ne $userPath) {
        [Environment]::SetEnvironmentVariable("Path", ($parts -join ";"), "User")
        return $true
    }
    return $false
}

function Publish-EnvironmentChange {
    if ($null -eq ("MusializerSetup.EnvironmentNotifier" -as [type])) {
        Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;

namespace MusializerSetup
{
    public static class EnvironmentNotifier
    {
        [DllImport("user32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        private static extern IntPtr SendMessageTimeout(
            IntPtr window,
            uint message,
            IntPtr wordParameter,
            string longParameter,
            uint flags,
            uint timeout,
            out IntPtr result);

        public static void Broadcast()
        {
            IntPtr result;
            SendMessageTimeout(
                new IntPtr(0xffff),
                0x001a,
                IntPtr.Zero,
                "Environment",
                0x0002,
                5000,
                out result);
        }
    }
}
"@
    }

    [MusializerSetup.EnvironmentNotifier]::Broadcast()
}

function Set-MingwBuildTarget {
    param([Parameter(Mandatory = $true)][string]$ConfigPath)

    if (-not (Test-Path -LiteralPath $ConfigPath -PathType Leaf)) {
        return
    }

    $content = [IO.File]::ReadAllText($ConfigPath)
    $activeTargetPattern = "(?m)^\s*#define\s+(MUSIALIZER_TARGET_(?:LINUX|WIN64_MINGW|WIN64_MSVC|MACOS|OPENBSD))\s*$"
    $activeTargets = [regex]::Matches($content, $activeTargetPattern)
    if ($activeTargets.Count -eq 1 -and
        $activeTargets[0].Groups[1].Value -eq "MUSIALIZER_TARGET_WIN64_MINGW") {
        return
    }

    if ($content -notmatch "MUSIALIZER_TARGET_WIN64_MINGW" -or
        $content -match "(?m)^\s*#define\s+MUSIALIZER_TARGET\s*$") {
        throw "The existing build\config.h uses an unsupported schema. Move the build directory aside and run this script again."
    }

    $backupPath = "$ConfigPath.before-windows-setup.bak"
    if (-not (Test-Path -LiteralPath $backupPath)) {
        Copy-Item -LiteralPath $ConfigPath -Destination $backupPath
    }

    $targets = @(
        "MUSIALIZER_TARGET_LINUX",
        "MUSIALIZER_TARGET_WIN64_MINGW",
        "MUSIALIZER_TARGET_WIN64_MSVC",
        "MUSIALIZER_TARGET_MACOS",
        "MUSIALIZER_TARGET_OPENBSD"
    )
    foreach ($target in $targets) {
        $pattern = "(?m)^\s*(?://\s*)?#define\s+$target\s*$"
        $replacement = if ($target -eq "MUSIALIZER_TARGET_WIN64_MINGW") {
            "#define $target"
        } else {
            "// #define $target"
        }
        $content = [regex]::Replace($content, $pattern, $replacement)
    }

    $utf8WithoutBom = [Text.UTF8Encoding]::new($false)
    [IO.File]::WriteAllText($ConfigPath, $content, $utf8WithoutBom)
    Write-Host "Configured build\config.h for MinGW-w64 (backup: $backupPath)."
}

if ([Environment]::OSVersion.Platform -ne [PlatformID]::Win32NT) {
    throw "This setup script must be run on Windows."
}
if (-not [Environment]::Is64BitOperatingSystem) {
    throw "Musializer's Windows build requires 64-bit Windows."
}

$repoRoot = $PSScriptRoot
if (-not (Test-Path -LiteralPath (Join-Path $repoRoot "nob.c") -PathType Leaf)) {
    throw "Could not find nob.c beside this script. Run the script from a Musializer source checkout."
}

Write-Step "Locating MSYS2"
$resolvedMsys2Root = Find-Msys2Root $Msys2Root
if ($null -eq $resolvedMsys2Root) {
    $winget = Get-Command winget.exe -ErrorAction SilentlyContinue
    if ($null -eq $winget) {
        throw "MSYS2 is not installed and winget.exe is unavailable. Install App Installer from Microsoft, then run this script again."
    }

    $installRoot = if ([string]::IsNullOrWhiteSpace($Msys2Root)) {
        Join-Path $env:SystemDrive "msys64"
    } else {
        $Msys2Root
    }
    Write-Step "Installing MSYS2 in $installRoot"
    Invoke-Native -FilePath $winget.Source -ArgumentList @(
        "install",
        "--id", "MSYS2.MSYS2",
        "--exact",
        "--architecture", "x64",
        "--scope", "user",
        "--source", "winget",
        "--location", $installRoot,
        "--accept-source-agreements",
        "--accept-package-agreements",
        "--disable-interactivity"
    )
    $resolvedMsys2Root = Find-Msys2Root $installRoot
    if ($null -eq $resolvedMsys2Root) {
        throw "MSYS2 was installed, but its usr\bin\bash.exe could not be found. Re-run with -Msys2Root pointing to the installation directory."
    }
} else {
    Write-Host "Using MSYS2 at $resolvedMsys2Root"
}

$bash = Join-Path $resolvedMsys2Root "usr\bin\bash.exe"
if (-not $SkipUpdate) {
    Write-Step "Updating MSYS2 packages"
    try {
        Invoke-Msys2 -BashPath $bash -Command "pacman -Syu --noconfirm"
    } catch {
        # A core runtime update can terminate its own shell. The fresh second pass
        # distinguishes that expected case from a persistent update failure.
        Write-Warning "The first update pass ended early; retrying in a fresh MSYS2 shell."
    }
    Invoke-Msys2 -BashPath $bash -Command "pacman -Syu --noconfirm"
}

Write-Step "Installing the UCRT64 MinGW-w64 toolchain, FFmpeg, and zip"
Invoke-Msys2 -BashPath $bash -Command (
    "pacman -S --needed --noconfirm " +
    "mingw-w64-ucrt-x86_64-toolchain " +
    "mingw-w64-ucrt-x86_64-ffmpeg zip"
)

$ucrtBin = Join-Path $resolvedMsys2Root "ucrt64\bin"
$msysBin = Join-Path $resolvedMsys2Root "usr\bin"
$env:Path = "$ucrtBin;$msysBin;$env:Path"

if (-not $SkipPathUpdate) {
    Write-Step "Adding MSYS2 tools to your user PATH"
    $pathChanged = Add-UserPathEntries @($ucrtBin, $msysBin)
    if ($pathChanged) {
        Publish-EnvironmentChange
        Write-Host "PATH updated. Open a new terminal after this script finishes."
    } else {
        Write-Host "The required PATH entries are already present."
    }
}

$requiredTools = @(
    (Join-Path $ucrtBin "gcc.exe"),
    (Join-Path $ucrtBin "windres.exe"),
    (Join-Path $ucrtBin "ar.exe"),
    (Join-Path $ucrtBin "ffmpeg.exe"),
    (Join-Path $msysBin "zip.exe")
)
foreach ($tool in $requiredTools) {
    if (-not (Test-Path -LiteralPath $tool -PathType Leaf)) {
        throw "A required tool was not installed: $tool"
    }
}

Write-Step "Verifying the compiler and FFmpeg"
Invoke-Native -FilePath (Join-Path $ucrtBin "gcc.exe") -ArgumentList @("--version")
Invoke-Native -FilePath (Join-Path $ucrtBin "ffmpeg.exe") -ArgumentList @("-version")

if (-not $SkipBuild) {
    Write-Step "Building Musializer"
    Push-Location $repoRoot
    try {
        Set-MingwBuildTarget (Join-Path $repoRoot "build\config.h")
        Invoke-Native -FilePath (Join-Path $ucrtBin "gcc.exe") -ArgumentList @("-o", "nob.exe", "nob.c")
        Invoke-Native -FilePath (Join-Path $repoRoot "nob.exe") -ArgumentList @()
    } finally {
        Pop-Location
    }
}

Write-Host "`nWindows setup completed successfully." -ForegroundColor Green
if (-not $SkipBuild) {
    Write-Host "Run Musializer from the repository root with: .\build\musializer.exe"
}

# powershell -NoProfile -ExecutionPolicy Bypass -File .\setup-windows.ps1