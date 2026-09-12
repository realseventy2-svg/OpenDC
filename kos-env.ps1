<#
.SYNOPSIS
    OpenDC / DreamSDK PowerShell Environment & CLI Helper
.DESCRIPTION
    Provides direct access to the OpenDC project and Flycast emulator from PowerShell.
    Adapted from the original KallistiOS kos-env.ps1 for the DreamSDK toolchain.

    Source this file in your PowerShell session:
        . C:\DreamSDK\home\PC\OpenDC\kos-env.ps1
#>

# -- Paths --------------------------------------------------------------------
$Script:ProjectRoot  = if ($PSScriptRoot) { $PSScriptRoot } else { (Get-Location).Path }
$Script:DreamSDKRoot = if ($env:DREAMSDK_ROOT) { $env:DREAMSDK_ROOT } else {
    @("C:\DreamSDK", "D:\DreamSDK", "E:\DreamSDK") | Where-Object { Test-Path $_ } | Select-Object -First 1
}

# Auto-discover Flycast Directory & Executable
$Script:FlycastDir = if ($env:FLYCAST_DIR) { $env:FLYCAST_DIR } else {
    @(
        "$Script:ProjectRoot\tools\flycast",
        "D:\Github\Personal\KallistiOS\tools\flycast",
        "C:\DreamSDK\tools\flycast",
        "$env:LOCALAPPDATA\Programs\Flycast",
        "$env:ProgramFiles\Flycast"
    ) | Where-Object { Test-Path $_ } | Select-Object -First 1
}
$Script:FlycastExe = if ($Script:FlycastDir) {
    "$Script:FlycastDir\flycast.exe"
} else {
    $cmd = Get-Command flycast -ErrorAction SilentlyContinue
    if ($cmd) { $cmd.Source } else { $null }
}

# Retail / Devkit / Flash BIOS paths (Auto-discovered across local folders)
$Script:FlashBin = @(
    "$Script:ProjectRoot\dc_flash.bin",
    "$Script:ProjectRoot\bios\res\dc_flash.bin",
    "$Script:ProjectRoot\bootloader\res\dc_flash.bin",
    "D:\Github\Personal\KallistiOS\bios\dc_flash.bin"
) | Where-Object { Test-Path $_ } | Select-Object -First 1

$Script:BiosRetail = @(
    "$Script:ProjectRoot\bios\res\boot_loader_retail.bios",
    "D:\Github\Personal\KallistiOS\bios\jc-bootROM-retail-v1.032.bin"
) | Where-Object { Test-Path $_ } | Select-Object -First 1

$Script:BiosDevkit = @(
    "$Script:ProjectRoot\bios\res\boot_loader_devkit.bios",
    "D:\Github\Personal\KallistiOS\bios\jc-bootROM-devkit-v1.032.bin"
) | Where-Object { Test-Path $_ } | Select-Object -First 1

# OpenDC custom BIOS output (built by `make` in the project root)
$Script:CustomBios = "$Script:ProjectRoot\boot_loader_custom.bios"

# -- Internal helpers ----------------------------------------------------------

function Set-FlycastBios {
    param([string]$BiosPath)

    if (-not (Test-Path $BiosPath)) {
        Write-Error "BIOS file not found: $BiosPath"
        return $false
    }

    # Ensure local portable data dir exists
    $flycastData = "$Script:FlycastDir\data"
    New-Item -ItemType Directory -Path $flycastData -Force -ErrorAction SilentlyContinue | Out-Null

    $appDataFlycast     = "$env:APPDATA\Flycast"
    $appDataFlycastData = "$appDataFlycast\data"
    New-Item -ItemType Directory -Path $appDataFlycastData -Force -ErrorAction SilentlyContinue | Out-Null

    # Copy the BIOS under every filename Flycast might look for
    $targets = @(
        "$flycastData\dc_boot.bin",  "$flycastData\boot.bin",  "$flycastData\bios.bin",  "$flycastData\dc_bios.bin",
        "$Script:FlycastDir\dc_boot.bin", "$Script:FlycastDir\boot.bin", "$Script:FlycastDir\bios.bin", "$Script:FlycastDir\dc_bios.bin",
        "$appDataFlycast\dc_boot.bin",    "$appDataFlycast\boot.bin",    "$appDataFlycast\bios.bin",    "$appDataFlycast\dc_bios.bin",
        "$appDataFlycastData\dc_boot.bin","$appDataFlycastData\boot.bin","$appDataFlycastData\bios.bin","$appDataFlycastData\dc_bios.bin"
    )
    foreach ($t in $targets) { Copy-Item $BiosPath $t -Force -ErrorAction SilentlyContinue }

    # Flash file - only initialize if target does not exist so user FlashROM / NVRAM persists!
    if (Test-Path $Script:FlashBin) {
        $flashTargets = @(
            "$flycastData\dc_flash.bin",       "$flycastData\flash.bin",
            "$Script:FlycastDir\dc_flash.bin", "$Script:FlycastDir\flash.bin",
            "$appDataFlycast\dc_flash.bin",    "$appDataFlycast\flash.bin",
            "$appDataFlycastData\dc_flash.bin","$appDataFlycastData\flash.bin"
        )
        foreach ($ft in $flashTargets) {
            if (-not (Test-Path $ft)) {
                Copy-Item $Script:FlashBin $ft -Force -ErrorAction SilentlyContinue
            }
        }
    }


    # Update emu.cfg preserving existing settings
    foreach ($cfgPath in @("$Script:FlycastDir\emu.cfg", "$appDataFlycast\emu.cfg")) {
        if (Test-Path $cfgPath) {
            $lines = Get-Content $cfgPath
            $hasBios = $false
            $newLines = @()
            foreach ($line in $lines) {
                if ($line -match '^\s*bios\s*=') {
                    $newLines += "bios = $BiosPath"
                    $hasBios = $true
                } else {
                    $newLines += $line
                }
            }
            if (-not $hasBios) {
                $newLines += "bios = $BiosPath"
            }
            Set-Content -Path $cfgPath -Value $newLines -Encoding UTF8 -Force
        } else {
            $cfg = "[config]`nhle = no`nAutoHLE = 0`nFastBoot = yes`nbios = $BiosPath`n"
            Set-Content -Path $cfgPath -Value $cfg -Encoding UTF8 -Force
        }
    }


    return $true
}

function Invoke-DreamcastBoot {
    param(
        [Parameter(Mandatory=$true)][string]$BiosFile,
        [Parameter(Mandatory=$true)][string]$BiosName,
        [Parameter(Position=0)][string]$Target
    )

    if (-not (Test-Path $Script:FlycastExe)) {
        Write-Error "Flycast not found at: $Script:FlycastExe"
        return
    }
    if (-not (Set-FlycastBios $BiosFile)) { return }

    Write-Host "=======================================================" -ForegroundColor Cyan
    Write-Host "  Booting Dreamcast [$BiosName BIOS]"                   -ForegroundColor Yellow
    Write-Host "=======================================================" -ForegroundColor Cyan

    if ([string]::IsNullOrWhiteSpace($Target)) {
        Write-Host "No disc - booting into $BiosName dashboard..." -ForegroundColor Green
        Start-Process -FilePath $Script:FlycastExe -WorkingDirectory $Script:FlycastDir
        return
    }

    # Resolve the disc path
    $discPath = $Target
    if (-not (Test-Path -LiteralPath $discPath -ErrorAction SilentlyContinue)) {
        $candidate = Get-ChildItem -LiteralPath (Get-Location) -Filter $Target -ErrorAction SilentlyContinue |
                     Select-Object -First 1
        if ($candidate) { $discPath = $candidate.FullName }
    }

    if (Test-Path -LiteralPath $discPath -ErrorAction SilentlyContinue) {
        $discPath = (Resolve-Path -LiteralPath $discPath).Path
    }

    Write-Host "Booting $BiosName BIOS with disc: $discPath" -ForegroundColor Green
    Start-Process -FilePath $Script:FlycastExe -WorkingDirectory $Script:FlycastDir -ArgumentList "`"$discPath`""
}

# -- Public commands -----------------------------------------------------------

function kos-bootcustom {
    <#
    .SYNOPSIS
        Boot the freshly-built OpenDC Custom BIOS in Flycast.
        Optionally pass a disc image path.
    .EXAMPLE
        kos-bootcustom                              # boot into OpenDC dashboard
        kos-bootcustom "D:\Games\DC\game.gdi"      # boot with a GDI disc
    #>
    param([Parameter(Position=0)][string]$Target)

    $candidates = @(
        $Script:CustomBios,
        "$Script:ProjectRoot\bootloader\dc_boot.bin"
    )
    $customRom = $candidates | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1

    if (-not $customRom) {
        Write-Error "OpenDC BIOS not found! Please build it first."
        return
    }
    Invoke-DreamcastBoot -BiosFile $customRom -BiosName "OpenDC" -Target $Target
}

function kos-bootretail {
    <#
    .SYNOPSIS
        Boot with the retail JC BIOS ROM (original Dreamcast firmware) in Flycast.
    #>
    param([Parameter(Position=0)][string]$Target)
    Invoke-DreamcastBoot -BiosFile $Script:BiosRetail -BiosName "Retail" -Target $Target
}

function kos-bootdev {
    <#
    .SYNOPSIS
        Boot with the DevKit JC BIOS ROM in Flycast.
    #>
    param([Parameter(Position=0)][string]$Target)
    Invoke-DreamcastBoot -BiosFile $Script:BiosDevkit -BiosName "DevKit" -Target $Target
}

function kos-buildscene {
    <#
    .SYNOPSIS
        Compile the Blender boot scene into the OpenDC bootloader.
    #>
    param([Parameter(Position=0)][string]$BlendPath, [switch]$SkipBiosBuild)
    $buildScript = "$Script:ProjectRoot\bootloader\tools\build_scene.ps1"
    if (Test-Path -LiteralPath $buildScript) {
        & $buildScript -BlendPath $BlendPath -SkipBiosBuild:$SkipBiosBuild
    } else {
        Write-Error "Build scene script not found at: $buildScript"
    }
}

function kos-eject {
    <#
    .SYNOPSIS
        Close Flycast and reopen with no disc (BIOS dashboard).
    #>
    Stop-Process -Name flycast -Force -ErrorAction SilentlyContinue
    Start-Sleep -Milliseconds 150
    Write-Host "Booting into BIOS dashboard (no disc)..." -ForegroundColor Cyan
    Start-Process -FilePath $Script:FlycastExe -WorkingDirectory $Script:FlycastDir
}

function kos-insert {
    <#
    .SYNOPSIS
        Swap a disc into a running (or new) Flycast session.
    #>
    param([Parameter(Position=0)][string]$Target)
    if ([string]::IsNullOrWhiteSpace($Target)) {
        Write-Error "Please provide a disc image path."
        return
    }
    Stop-Process -Name flycast -Force -ErrorAction SilentlyContinue
    Start-Sleep -Milliseconds 150
    $resolvedPath = Resolve-Path -LiteralPath $Target -ErrorAction SilentlyContinue
    $disc = if ($resolvedPath) { $resolvedPath.Path } else { $Target }
    Write-Host "Inserting disc: $disc" -ForegroundColor Green
    Start-Process -FilePath $Script:FlycastExe -WorkingDirectory $Script:FlycastDir -ArgumentList "`"$disc`""
}

function kos-romsize {
    <#
    .SYNOPSIS
        Display the ROM memory budget and remaining free space.
    #>
    $romCheckScript = "$Script:ProjectRoot\bootloader\tools\dcbs-tool\check_rom_size.py"
    if (Test-Path -LiteralPath $romCheckScript) {
        python "$romCheckScript"
    } else {
        Write-Error "ROM check script not found at: $romCheckScript"
    }
}

# -- Aliases -------------------------------------------------------------------
Set-Alias -Name kos-opendc      -Value kos-bootcustom
Set-Alias -Name kos-bootopendc  -Value kos-bootcustom
Set-Alias -Name kos-bios        -Value kos-bootretail
Set-Alias -Name kos-scene       -Value kos-buildscene
Set-Alias -Name kos-rom         -Value kos-romsize
Set-Alias -Name kos-swap        -Value kos-insert
Set-Alias -Name kos-opendrive   -Value kos-eject

# -- Banner --------------------------------------------------------------------
Write-Host "=======================================================" -ForegroundColor DarkCyan
Write-Host "  OpenDC / DreamSDK Environment Loaded"                  -ForegroundColor Cyan
Write-Host "=======================================================" -ForegroundColor DarkCyan
Write-Host "Commands:" -ForegroundColor Yellow
Write-Host "  kos-bootcustom [disc]     - Run OpenDC BIOS in Flycast (+ optional disc)" -ForegroundColor Green
Write-Host "  kos-bootretail [disc]     - Run retail JC BIOS in Flycast" -ForegroundColor Green
Write-Host "  kos-bootdev    [disc]     - Run devkit JC BIOS in Flycast" -ForegroundColor Green
Write-Host "  kos-buildscene [blend]    - Recompile Blender boot scene" -ForegroundColor Green
Write-Host "  kos-eject                 - Boot into BIOS dashboard (no disc)" -ForegroundColor Green
Write-Host "  kos-insert <disc>         - Swap disc into Flycast" -ForegroundColor Green
Write-Host "  kos-romsize               - Show ROM memory budget" -ForegroundColor Green
Write-Host ""
Write-Host "Project : $Script:ProjectRoot" -ForegroundColor DarkGray
Write-Host "Flycast : $Script:FlycastExe"  -ForegroundColor DarkGray
Write-Host "BIOS    : $Script:CustomBios"  -ForegroundColor DarkGray
