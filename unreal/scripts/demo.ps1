<#
.SYNOPSIS
  One command per step, in the vocabulary scripts/demo.sh already uses.

    .\scripts\demo.ps1 check                 preflight -- engine, toolchain, data, vrgrid
    .\scripts\demo.ps1 bake foveation        export one scene (or 'all')
    .\scripts\demo.ps1 bake ghosts-on -Rrd   export the scene AND the .rrd, in one pass
    .\scripts\demo.ps1 build                 compile the Unreal module
    .\scripts\demo.ps1 materials             generate the per-instance colour materials (once)
    .\scripts\demo.ps1 play foveation        open the scene in the Unreal MAP VIEWER
    .\scripts\demo.ps1 sim seq00-full        the DRIVING SIMULATION on that route
    .\scripts\demo.ps1 both ghosts-on        Rerun and Unreal side by side, same scene
    .\scripts\demo.ps1 list

  The scene names are the ones the Rerun demo uses, so 'ghosts-on' means the
  same sequence, frame range and colour layer in both windows.
#>
[CmdletBinding()]
param(
    [Parameter(Position = 0)][string]$Command = "help",
    [Parameter(Position = 1)][string]$Scene = "",
    [switch]$Rrd,
    [switch]$Diag,
    [int]$Shot = -1,
    [int]$Frames = 0,
    [string]$VrgridRepo = "$env:USERPROFILE\vrgrid",
    [string]$UnrealRoot = "C:\Program Files\Epic Games\UE_5.8"
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
$Project = Join-Path $Root "VRgridViewer\VRgridViewer.uproject"
$ScenesDir = Join-Path $Root "scenes"
$Scenes = @("foveation", "ghosts-off", "ghosts-on", "traffic", "reflectivity", "features")

function Say($m) { Write-Host $m -ForegroundColor Cyan }
function Warn($m) { Write-Host $m -ForegroundColor Yellow }
function Die($m) { Write-Host $m -ForegroundColor Red; exit 1 }

# The loader wants the directory HOLDING poses/ and sequences/. In the working
# clone that is data\dataset, NOT data -- the single most likely way to lose an
# export, so it is resolved here and never typed by hand.
function Resolve-DataRoot {
    if ($env:VRGRID_DATA_ROOT) { return $env:VRGRID_DATA_ROOT }
    $candidates = @(
        (Join-Path $VrgridRepo "data\dataset"),
        (Join-Path $VrgridRepo "data")
    )
    foreach ($c in $candidates) {
        if (Test-Path (Join-Path $c "poses")) { return $c }
    }
    return $null
}

switch ($Command) {

"check" {
    Say "== preflight =="
    $py = (Get-Command python -ErrorAction SilentlyContinue)
    if (-not $py) { Die "no python on PATH" }
    Write-Host ("python        " + (python --version 2>&1))

    $v = python -c "import vrgrid, sys; print(vrgrid.__file__)" 2>&1
    if ($LASTEXITCODE -ne 0) { Die "vrgrid not importable: $v" } else { Write-Host "vrgrid        $v" }

    $r = python -c "import rerun; print(rerun.__version__)" 2>&1
    if ($LASTEXITCODE -ne 0) { Warn "rerun        MISSING -- the -Rrd half will not run" }
    else { Write-Host "rerun         $r" }

    python -c "from vrgrid.perception import ground; print('patchwork++   ' + ('yes' if ground._HAVE_PATCHWORKPP else 'NO -- ground falls back to the semantic proxy'))"

    $dataRoot = Resolve-DataRoot
    if ($dataRoot) { Write-Host "data root     $dataRoot" } else { Warn "data root     NOT FOUND under $VrgridRepo" }

    if (Test-Path $UnrealRoot) { Write-Host "unreal        $UnrealRoot" } else { Warn "unreal        NOT FOUND at $UnrealRoot" }

    $exe = Join-Path $Root "VRgridViewer\Binaries\Win64\VRgridViewer.exe"
    if (Test-Path $exe) { Write-Host "viewer binary built" } else { Warn "viewer binary NOT built -- run: .\scripts\demo.ps1 build" }

    # The editor target is what cooks content and authors materials. Without
    # the .NET Framework SDK, UnrealBuildTool refuses it (SwarmInterface).
    $netfx = Test-Path "C:\Program Files (x86)\Reference Assemblies\Microsoft\Framework\.NETFramework"
    if ($netfx) { Write-Host "netfx sdk     present" }
    else {
        Warn "netfx sdk     MISSING -- the EDITOR target cannot build, so there are"
        Warn "              no colour materials and no cooked content. Install with:"
        Warn ""
        Warn '              & "C:\Program Files (x86)\Microsoft Visual Studio\Installer\vs_installer.exe" modify ^'
        Warn '                --installPath "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools" ^'
        Warn '                --add Microsoft.Net.Component.4.6.2.SDK ^'
        Warn '                --add Microsoft.Net.Component.4.6.2.TargetingPack --passive --norestart'
    }

    if (Test-Path $ScenesDir) {
        $n = (Get-ChildItem $ScenesDir -Directory -ErrorAction SilentlyContinue).Count
        Write-Host "baked scenes  $n in scenes\"
    } else { Warn "baked scenes  none -- run: .\scripts\demo.ps1 bake all" }

    Say "== format self-test =="
    python (Join-Path $Root "exporter\tests\test_format.py")
}

"bake" {
    if (-not $Scene) { Die "which scene? try: $($Scenes -join ', ') , or 'all'" }
    $dataRoot = Resolve-DataRoot
    if (-not $dataRoot) { Die "no KITTI data found under $VrgridRepo (expected data\dataset\poses)" }
    $env:VRGRID_DATA_ROOT = $dataRoot

    $targets = if ($Scene -eq "all") { $Scenes } else { @($Scene) }
    foreach ($s in $targets) {
        Say "baking $s ..."
        $args = @((Join-Path $Root "exporter\export_scene.py"), "--scene", $s,
                  "--out", (Join-Path $ScenesDir $s))
        if ($Rrd) { $args += "--rrd" }
        if ($Frames -gt 0) { $args += @("--frames", $Frames) }
        python @args
    }
}

"build" {
    $bat = Join-Path $UnrealRoot "Engine\Build\BatchFiles\Build.bat"
    if (-not (Test-Path $bat)) { Die "no Build.bat at $bat" }
    Say "== building VRgridViewer (Game) =="
    & $bat VRgridViewer Win64 Development -Project="$Project" -WaitMutex
    if ($LASTEXITCODE -ne 0) { Die "game build failed" }

    Say "== building VRgridViewerEditor =="
    & $bat VRgridViewerEditor Win64 Development -Project="$Project" -WaitMutex
    if ($LASTEXITCODE -ne 0) {
        Warn "editor build failed -- almost always the missing .NET Framework SDK."
        Warn "Run '.\scripts\demo.ps1 check' for the exact installer command."
    }
}

"materials" {
    $cmd = Join-Path $UnrealRoot "Engine\Binaries\Win64\UnrealEditor-Cmd.exe"
    if (-not (Test-Path $cmd)) { Die "no UnrealEditor-Cmd.exe at $cmd" }
    $script = Join-Path $Root "VRgridViewer\Content\Python\setup_assets.py"
    Say "generating /Game/VRgrid materials ..."
    & $cmd "$Project" -run=pythonscript -script="$script" -unattended -nosplash
}

"play" {
    if (-not $Scene) { Die "which scene? try: $($Scenes -join ', ')" }
    $dir = Join-Path $ScenesDir $Scene
    if (-not (Test-Path (Join-Path $dir "scene.json"))) {
        Die "no export at $dir -- run: .\scripts\demo.ps1 bake $Scene"
    }
    # Through the EDITOR binary with -game: it loads uncooked content, which a
    # standalone Development build does not. Cook the project if you want the
    # .exe on its own (see README).
    $editor = Join-Path $UnrealRoot "Engine\Binaries\Win64\UnrealEditor.exe"
    Say "playing $Scene ..."
    & $editor "$Project" -game -windowed -ResX=1600 -ResY=900 -VrgScene="$dir"
}

"sim" {
    # The driving simulation, not the map viewer: same binary, same export, one
    # flag. It reads the TRAJECTORY out of the export and drives that exact
    # route -- so it wants a FULL-sequence bake, which is what --light is for.
    if (-not $Scene) { Die "which scene? try: seq00-full" }
    $dir = Join-Path $ScenesDir $Scene
    if (-not (Test-Path (Join-Path $dir "scene.json"))) {
        Die "no export at $dir -- run: .\scripts\demo.ps1 bake $Scene"
    }
    $editor = Join-Path $UnrealRoot "Engine\Binaries\Win64\UnrealEditor.exe"
    $argv = @("$Project", "-game", "-windowed", "-ResX=1600", "-ResY=900",
              "-VrgMode=sim", "-VrgScene=$dir")
    if ($Diag) { $argv += "-VrgDiag" }
    if ($Shot -ge 0) { $argv += "-VrgShot=$Shot" }
    Say "simulating $Scene ...  (B = bands, N = pedestrian crossing)"
    & $editor @argv
}

"both" {
    if (-not $Scene) { Die "which scene? try: $($Scenes -join ', ')" }
    $dir = Join-Path $ScenesDir $Scene
    if (-not (Test-Path (Join-Path $dir "scene.json"))) {
        Die "no export at $dir -- run: .\scripts\demo.ps1 bake $Scene -Rrd"
    }
    $rrd = Get-ChildItem $dir -Filter *.rrd -ErrorAction SilentlyContinue | Select-Object -First 1
    if (-not $rrd) {
        Die "no .rrd in $dir -- re-bake with -Rrd so both windows come from one pass"
    }

    # Rerun first: it takes longer to come up, and whoever is presenting wants
    # the map window placed before the Unreal one appears over it.
    Say "starting Rerun on $($rrd.Name) ..."
    Start-Process -FilePath "python" -ArgumentList @("-m", "rerun", "`"$($rrd.FullName)`"")

    Start-Sleep -Seconds 3
    Say "starting Unreal on $Scene ..."
    $editor = Join-Path $UnrealRoot "Engine\Binaries\Win64\UnrealEditor.exe"
    & $editor "$Project" -game -windowed -ResX=1600 -ResY=900 -VrgScene="$dir"
}

"list" { $Scenes | ForEach-Object { Write-Host $_ } }

default {
    Get-Help $PSCommandPath -Detailed | Out-String | Write-Host
}

}
