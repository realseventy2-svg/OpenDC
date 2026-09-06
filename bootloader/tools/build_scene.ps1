<#
.SYNOPSIS
    Compiles a Blender project scene into the Dreamcast bootloader and custom BIOS ROM.
.DESCRIPTION
    1. Auto-detects the .blend file in tools/dcbs-tool/res/scenes/ (or accepts a path).
    2. Locates Blender executable on the system.
    3. Runs background export with tools/dcbs-tool/export_dcbs.py to produce boot_scene.bin.
    4. Compiles the bootloader (dc_boot.bin) via WSL.
    5. Deploys the bootloader payload to DreamDash and OpenDC.
    6. Rebuilds the custom BIOS (dreamdash.bios).
#>

[CmdletBinding()]
param(
    [Parameter(Position=0)]
    [string]$BlendPath,

    [switch]$SkipBiosBuild
)

$ErrorActionPreference = "Stop"

$workspaceRoot = "D:\Github\Personal\KallistiOS"
$bootloaderDir = "$workspaceRoot\projects\OpenDC\bootloader"
$dcbsToolDir = "$bootloaderDir\tools\dcbs-tool"
$dcbsScenesDir = "$dcbsToolDir\res\scenes"
$blenderSceneDir = "$bootloaderDir\res\blender_scene"
$exportScript = "$dcbsToolDir\export_dcbs.py"

Write-Host "=======================================================" -ForegroundColor Cyan
Write-Host "  OpenDC 3D Boot Scene & Custom BIOS Compiler" -ForegroundColor Yellow
Write-Host "=======================================================" -ForegroundColor Cyan

# 1. Locate Blender executable
$blenderExe = $null
if (Get-Command blender -ErrorAction SilentlyContinue) {
    $blenderExe = (Get-Command blender).Source
} else {
    $standardPaths = @(
        (Get-ChildItem "C:\Program Files\Blender Foundation\Blender *\blender.exe" -ErrorAction SilentlyContinue | Sort-Object FullName -Descending | Select-Object -First 1 -ExpandProperty FullName),
        (Get-ChildItem "$env:LOCALAPPDATA\Programs\Blender Foundation\Blender *\blender.exe" -ErrorAction SilentlyContinue | Sort-Object FullName -Descending | Select-Object -First 1 -ExpandProperty FullName),
        "C:\Program Files\Blender Foundation\Blender 4.5\blender.exe",
        "C:\Program Files\Blender Foundation\Blender 4.4\blender.exe",
        "C:\Program Files\Blender Foundation\Blender 4.3\blender.exe",
        "C:\Program Files\Blender Foundation\Blender 4.2\blender.exe",
        "C:\Program Files\Blender Foundation\Blender\blender.exe"
    )
    foreach ($p in $standardPaths) {
        if ($p -and (Test-Path -LiteralPath $p)) {
            $blenderExe = $p
            break
        }
    }
}

if (-not $blenderExe -or -not (Test-Path -LiteralPath $blenderExe)) {
    Write-Error "Blender executable not found! Please ensure Blender is installed or added to PATH."
    exit 1
}

Write-Host "[1/4] Blender executable: $blenderExe" -ForegroundColor Gray

# 2. Detect .blend scene file
$targetBlend = $null
if (-not [string]::IsNullOrWhiteSpace($BlendPath)) {
    # 1. Exact path provided
    if (Test-Path -LiteralPath $BlendPath) {
        $targetBlend = (Resolve-Path -LiteralPath $BlendPath).Path
    }
    # 2. Auto-append .blend extension if missing / misspelled extension
    elseif (Test-Path -LiteralPath "$BlendPath.blend") {
        $targetBlend = (Resolve-Path -LiteralPath "$BlendPath.blend").Path
    }
    # 3. Check inside dcbs-tool/res/scenes/
    elseif (Test-Path -LiteralPath "$dcbsScenesDir\$BlendPath") {
        $targetBlend = (Resolve-Path -LiteralPath "$dcbsScenesDir\$BlendPath").Path
    }
    elseif (Test-Path -LiteralPath "$dcbsScenesDir\$BlendPath.blend") {
        $targetBlend = (Resolve-Path -LiteralPath "$dcbsScenesDir\$BlendPath.blend").Path
    }
    # 4. Check inside legacy res/blender_scene/
    elseif (Test-Path -LiteralPath "$blenderSceneDir\$BlendPath") {
        $targetBlend = (Resolve-Path -LiteralPath "$blenderSceneDir\$BlendPath").Path
    }
    elseif (Test-Path -LiteralPath "$blenderSceneDir\$BlendPath.blend") {
        $targetBlend = (Resolve-Path -LiteralPath "$blenderSceneDir\$BlendPath.blend").Path
    }
    else {
        Write-Error "Specified .blend scene file not found: '$BlendPath'"
        exit 1
    }
} else {
    # Auto-detect first .blend in dcbs-tool/res/scenes/ or fallback to res/blender_scene/
    $found = Get-ChildItem -LiteralPath $dcbsScenesDir -Filter "*.blend" -File -ErrorAction SilentlyContinue | Select-Object -First 1
    if (-not $found) {
        $found = Get-ChildItem -LiteralPath $blenderSceneDir -Filter "*.blend" -File -ErrorAction SilentlyContinue | Select-Object -First 1
    }
    if ($found) {
        $targetBlend = $found.FullName
    } else {
        Write-Error "No .blend project file found in '$dcbsScenesDir' or '$blenderSceneDir'!"
        exit 1
    }
}

Write-Host "[2/4] Target Scene: $targetBlend" -ForegroundColor Green

# 3. Export .blend -> boot_scene.bin using Python exporter
Write-Host "      Exporting 3D geometry, keyframes, sprites & audio to boot_scene.bin..." -ForegroundColor Cyan
& "$blenderExe" -b "$targetBlend" -P "$exportScript"
if ($LASTEXITCODE -ne 0) {
    Write-Error "Blender export failed with exit code $LASTEXITCODE"
    exit $LASTEXITCODE
}

$bootSceneBin = "$bootloaderDir\boot_scene.bin"
if (-not (Test-Path -LiteralPath $bootSceneBin)) {
    Write-Error "Export completed but '$bootSceneBin' was not generated."
    exit 1
}

$sceneSize = (Get-Item $bootSceneBin).Length
Write-Host "      boot_scene.bin generated successfully ($("{0:N0}" -f $sceneSize) bytes)." -ForegroundColor Green

# 4. Compile OpenDC Bootloader (dc_boot.bin)
Write-Host "[3/3] Compiling OpenDC Bootloader (dc_boot.bin)..." -ForegroundColor Cyan
wsl -d Ubuntu-26.04 -e /mnt/d/Github/Personal/KallistiOS/scripts/kos-exec.sh "/mnt/d/Github/Personal/KallistiOS/projects/OpenDC/bootloader" make
if ($LASTEXITCODE -ne 0) {
    Write-Error "OpenDC bootloader build failed with exit code $LASTEXITCODE"
    exit $LASTEXITCODE
}

$dcBootBin = "$bootloaderDir\dc_boot.bin"
if (Test-Path -LiteralPath $dcBootBin) {
    $bootSize = (Get-Item $dcBootBin).Length
    Write-Host "      dc_boot.bin built ($("{0:N0}" -f $bootSize) bytes)." -ForegroundColor Green

    # Deploy OpenDC Custom BIOS
    $customBiosDest = "$workspaceRoot\bios\boot_loader_custom.bios"
    $openDcBiosDest = "$workspaceRoot\projects\OpenDC\boot_loader_custom.bios"
    Copy-Item -LiteralPath $dcBootBin -Destination $customBiosDest -Force
    Copy-Item -LiteralPath $dcBootBin -Destination $openDcBiosDest -Force
    
    # Sync into Flycast directories
    $flycastData = "$workspaceRoot\tools\flycast\data"
    $appDataFlycast = "$env:APPDATA\Flycast"
    $appDataFlycastData = "$appDataFlycast\data"
    @(
        "$flycastData\dc_boot.bin", "$flycastData\bios.bin",
        "$workspaceRoot\tools\flycast\dc_boot.bin",
        "$appDataFlycast\dc_boot.bin", "$appDataFlycastData\dc_boot.bin"
    ) | ForEach-Object {
        if (Test-Path (Split-Path $_ -Parent)) {
            Copy-Item -LiteralPath $dcBootBin -Destination $_ -Force -ErrorAction SilentlyContinue
        }
    }
    Write-Host "      Deployed OpenDC BIOS (dc_boot.bin) to bios/ and Flycast." -ForegroundColor Gray
}

# 5. Analyze Flash ROM memory budget and remaining headroom
$romCheckScript = "$dcbsToolDir\check_rom_size.py"
if (Test-Path -LiteralPath $romCheckScript) {
    Write-Host ""
    python "$romCheckScript"
}

Write-Host "=======================================================" -ForegroundColor Cyan
Write-Host "  OpenDC Boot Scene & Custom BIOS Build Complete!" -ForegroundColor Green
Write-Host "  Test with: kos-bootcustom" -ForegroundColor Yellow
Write-Host "=======================================================" -ForegroundColor Cyan
