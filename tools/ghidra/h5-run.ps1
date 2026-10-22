# H5: rebuilds the Ghidra project of default.xbe and the coverage reports.
# Usage: powershell -ExecutionPolicy Bypass -File %NGB_DATA%\ghidra\scripts\h5-run.ps1 [-Fresh]
# NGB_DATA defaults to %USERPROFILE%\ngb-data; GHIDRA_HOME is the Ghidra install folder.
# -Fresh deletes the ngb project and imports it again (about 5 min of analysis).
param([switch]$Fresh)

$ErrorActionPreference = "Stop"
$data = if ($env:NGB_DATA) { $env:NGB_DATA } else { "$env:USERPROFILE\ngb-data" }
if (-not $env:GHIDRA_HOME) { throw "set GHIDRA_HOME to the Ghidra install folder" }
$ghidra = "$env:GHIDRA_HOME\support\analyzeHeadless.bat"
$root = "$data\ghidra"
$scripts = "$root\scripts"
$reports = "$root\reports"
$logs = "$root\logs"
$xbe = "$data\game\default.xbe"
$env:GHIDRA_HEADLESS_MAXMEM = "6G"
New-Item -ItemType Directory -Force $reports, $logs | Out-Null

if ($Fresh -and (Test-Path "$root\ngb.gpr")) {
    Remove-Item -Recurse -Force "$root\ngb.rep", "$root\ngb.gpr"
}

if (-not (Test-Path "$root\ngb.gpr")) {
    # 1) Import with the ghidra-xbe loader and default analysis (includes XbSymbolDatabase and .XTLID).
    #    H5Fixup creates functions at the signatures that were left as labels and re-analyzes.
    $t = Measure-Command {
        & $ghidra $root ngb -import $xbe -loader XbeLoader -analysisTimeoutPerFile 3600 `
            -scriptPath $scripts -postScript H5Fixup.java `
            -log "$logs\h5-import.log" -scriptlog "$logs\h5-import-script.log" *> "$logs\h5-import-stdout.txt"
    }
    "import+analysis: $($t.TotalSeconds) s"
}

# 2) Reports (read-only, does not modify the project).
& $ghidra $root ngb -process default.xbe -noanalysis -readOnly -scriptPath $scripts `
    -postScript H5Report.java $reports "$scripts\nv097_methods.txt" *> "$logs\h5-report-stdout.txt"
& $ghidra $root ngb -process default.xbe -noanalysis -readOnly -scriptPath $scripts `
    -postScript H5Xtlid.java "$reports\h5-xtlid.csv" *> "$logs\h5-xtlid-stdout.txt"

# 3) Supporting decompilations (they contain code derived from the game: NOT for the repo).
& $ghidra $root ngb -process default.xbe -noanalysis -readOnly -scriptPath $scripts `
    -postScript H5Decomp.java "$reports\h5-decomp-d3d-unnamed.txt" 0x003C87C0 0x003C89C0 0x003CB5D0 0x003CC830 `
    0x003CC8A0 0x003D0350 0x003D1170 0x003D13B0 0x003CE5B0 0x003CE7E0 0x003CE820 0x003D1715 0x0030F400 `
    0x0030F2A0 0x001A1772 0x003CD240 0x003CD680 *> "$logs\h5-decomp-stdout.txt"
& $ghidra $root ngb -process default.xbe -noanalysis -readOnly -scriptPath $scripts `
    -postScript H5Decomp.java "$reports\h5-decomp-push-callers.txt" callers:0x003CD240 callers:0x003CD680 `
    *> "$logs\h5-decomp2-stdout.txt"
& $ghidra $root ngb -process default.xbe -noanalysis -readOnly -scriptPath $scripts `
    -postScript H5Decomp.java "$reports\h5-decomp-d3d-vblank.txt" refsin:D3D:KeSetEvent refsin:D3D:KeInsertQueueDpc `
    refsin:D3D:KeConnectInterrupt refsin:D3D:KeInitializeDpc 0x003D15B5 *> "$logs\h5-decomp4-stdout.txt"
& $ghidra $root ngb -process default.xbe -noanalysis -readOnly -scriptPath $scripts `
    -postScript H5Decomp.java "$reports\h5-decomp-samples.txt" --asm 0x003200C0 0x000C66D0 0x001A1920 `
    0x00328F10 0x0030E760 0x001D2B90 *> "$logs\h5-decomp3-stdout.txt"
"reports in $reports"
