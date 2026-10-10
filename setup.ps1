<#
 setup.ps1 - one-time download of the two things the build needs:
   1. OpenOrbis PS4 Toolchain v0.5.3 (LLVM 18 build)  ->  C:\OpenOrbis\PS4Toolchain
   2. GoldHEN Plugins SDK (main branch)               ->  C:\GoldHEN_Plugins_SDK

 It does NOT install LLVM (that needs an installer - see GUIDE.md step 2).
 If anything here fails, GUIDE.md explains how to do the same steps by hand.

 The download URLs are the ones used by remotePad's Dockerfile.
#>
param(
    [string]$ToolchainRoot = 'C:\',
    [string]$SdkDir = 'C:\GoldHEN_Plugins_SDK'
)

$ErrorActionPreference = 'Continue'
$ProgressPreference = 'SilentlyContinue'      # makes Invoke-WebRequest much faster
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

function Say([string]$m)  { Write-Host $m }
function Good([string]$m) { Write-Host "  [ OK ] $m" -ForegroundColor Green }
function Bad([string]$m)  { Write-Host "  [FAIL] $m" -ForegroundColor Red }

$TcUrl  = 'https://github.com/OpenOrbis/OpenOrbis-PS4-Toolchain/releases/download/v0.5.3/toolchain-llvm-18.2.zip'
$SdkUrl = 'https://codeload.github.com/GoldHEN/GoldHEN_Plugins_SDK/zip/refs/heads/main'
$Work   = Join-Path $env:TEMP 'xbs_setup'
New-Item -ItemType Directory -Force -Path $Work | Out-Null

Say ""
Say "=== setup: OpenOrbis toolchain + GoldHEN SDK ==="

# ------------------------------------------------------------ toolchain
$TcFinal = Join-Path $ToolchainRoot 'OpenOrbis\PS4Toolchain'
if (Test-Path (Join-Path $TcFinal 'link.x')) {
    Good "OpenOrbis toolchain already present: $TcFinal"
} else {
    Say ""
    Say "Downloading the OpenOrbis toolchain (several hundred MB, please wait)..."
    $zip = Join-Path $Work 'toolchain.zip'
    try { Invoke-WebRequest -Uri $TcUrl -OutFile $zip -UseBasicParsing }
    catch { Bad "Download failed: $($_.Exception.Message)"; Say "  Do it by hand: GUIDE.md step 3."; exit 1 }

    $ex = Join-Path $Work 'toolchain_zip'
    if (Test-Path $ex) { Remove-Item -Recurse -Force $ex }
    Expand-Archive -Path $zip -DestinationPath $ex -Force
    $tgz = Get-ChildItem -Path $ex -Recurse -Include '*.tar.gz', '*.tgz' | Select-Object -First 1
    if (-not $tgz) { Bad "No .tar.gz found inside the downloaded zip."; Say "  Do it by hand: GUIDE.md step 3."; exit 1 }

    Say "Extracting $($tgz.Name) into $ToolchainRoot (a few warnings about symbolic links are normal)..."
    & tar.exe -xzf $tgz.FullName -C $ToolchainRoot
    if (Test-Path (Join-Path $TcFinal 'link.x')) { Good "Toolchain installed: $TcFinal" }
    else { Bad "link.x not found in $TcFinal after extracting."; Say "  Do it by hand: GUIDE.md step 3."; exit 1 }
}

# ------------------------------------------------------------ SDK
$SdkFound = $null
foreach ($c in @($SdkDir, 'C:\GoldHEN_Plugins_SDK-main', (Join-Path $SdkDir 'GoldHEN_Plugins_SDK-main'))) {
    if (Test-Path (Join-Path $c 'include\GoldHEN.h')) { $SdkFound = $c; break }
}
if ($SdkFound) {
    Good "GoldHEN SDK already present: $SdkFound"
} else {
    Say ""
    Say "Downloading the GoldHEN Plugins SDK..."
    $zip2 = Join-Path $Work 'sdk.zip'
    try { Invoke-WebRequest -Uri $SdkUrl -OutFile $zip2 -UseBasicParsing }
    catch { Bad "Download failed: $($_.Exception.Message)"; Say "  Do it by hand: GUIDE.md step 4."; exit 1 }
    $ex2 = Join-Path $Work 'sdk_zip'
    if (Test-Path $ex2) { Remove-Item -Recurse -Force $ex2 }
    Expand-Archive -Path $zip2 -DestinationPath $ex2 -Force
    $inner = Get-ChildItem -Path $ex2 -Directory | Select-Object -First 1
    if (-not $inner) { Bad "Unexpected zip layout."; exit 1 }
    if (Test-Path $SdkDir) { Remove-Item -Recurse -Force $SdkDir }
    Move-Item -Path $inner.FullName -Destination $SdkDir
    if (Test-Path (Join-Path $SdkDir 'include\GoldHEN.h')) { Good "SDK installed: $SdkDir" }
    else { Bad "include\GoldHEN.h not found in $SdkDir."; exit 1 }
}

# ------------------------------------------------------------ LLVM check
Say ""
$clang = Get-Command clang.exe -ErrorAction SilentlyContinue
if (-not $clang -and -not (Test-Path "$env:ProgramFiles\LLVM\bin\clang.exe")) {
    Write-Host "  [TODO] LLVM for Windows is not installed yet." -ForegroundColor Yellow
    Say "         Install it (GUIDE.md step 2), for example by running this in PowerShell:"
    Say "             winget install LLVM.LLVM --version 18.1.8"
    Say "         then close and reopen this window."
} else {
    Good "LLVM (clang) found"
}
Say ""
Say "Setup finished. Next: double-click check.bat, then build.bat."
exit 0
