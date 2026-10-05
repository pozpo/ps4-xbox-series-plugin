<#
 build.ps1 - Windows-native build for the Xbox Series X|S GoldHEN plugin.

 No WSL, no Linux, no Make. It runs the same compiler/linker commands as the
 original Linux Makefile, using:
   * LLVM for Windows       (clang.exe, ld.lld.exe, llvm-ar.exe)
   * OpenOrbis PS4 Toolchain (headers, link.x, create-fself.exe)
   * GoldHEN Plugins SDK    (headers + libGoldHEN_Hook.a + crtprx.o)

 Usage (normally started by double-clicking build.bat):
   build.ps1                 build the plugin  ->  dist\xbox_series.prx
   build.ps1 -Check          only check that everything needed is installed
   build.ps1 -Clean          delete build output
   build.ps1 -RebuildSdk     force a rebuild of the GoldHEN SDK library
   build.ps1 -Upload -Ps4Ip 192.168.1.50   build, then FTP the .prx to the PS4

 Optional: -Toolchain "C:\path\PS4Toolchain"  -Sdk "C:\path\GoldHEN_Plugins_SDK"
#>
param(
    [string]$Toolchain = "",
    [string]$Sdk = "",
    [string]$Ps4Ip = "",
    [switch]$Check,
    [switch]$Clean,
    [switch]$RebuildSdk,
    [switch]$Upload
)

$ErrorActionPreference = 'Continue'
$PluginName = 'xbox_series'
$Proj = $PSScriptRoot
if (-not $Proj) { $Proj = (Get-Location).Path }

function Say([string]$m)  { Write-Host $m }
function Good([string]$m) { Write-Host "  [ OK ] $m" -ForegroundColor Green }
function Warn([string]$m) { Write-Host "  [WARN] $m" -ForegroundColor Yellow }
function Bad([string]$m)  { Write-Host "  [FAIL] $m" -ForegroundColor Red }
function Stop-Build([string]$m) {
    Write-Host ""
    Write-Host "BUILD STOPPED: $m" -ForegroundColor Red
    exit 1
}

# ---------------------------------------------------------------- clean
$ObjDir   = Join-Path $Proj 'obj'
$BinDir   = Join-Path $Proj 'bin'
$DistDir  = Join-Path $Proj 'dist'
$BuildDir = Join-Path $Proj 'build'

if ($Clean) {
    foreach ($d in @($ObjDir, $BinDir, $DistDir, $BuildDir)) {
        if (Test-Path $d) { Remove-Item -Recurse -Force $d }
    }
    Say "Cleaned obj, bin, dist and build folders."
    exit 0
}

Say ""
Say "=== Xbox Series X|S plugin - Windows build ==="
Say "Project folder: $Proj"
Say ""
Say "Step 1: looking for the tools"

# ---------------------------------------------------------------- LLVM
function Find-Tool([string]$name) {
    $c = Get-Command $name -ErrorAction SilentlyContinue
    if ($c) { return $c.Source }
    foreach ($dir in @("$env:ProgramFiles\LLVM\bin", "${env:ProgramFiles(x86)}\LLVM\bin", "C:\LLVM\bin")) {
        $p = Join-Path $dir $name
        if (Test-Path $p) { return $p }
    }
    return $null
}

$Clang = Find-Tool 'clang.exe'
$Lld   = Find-Tool 'ld.lld.exe'
$Ar    = Find-Tool 'llvm-ar.exe'
$problems = 0

if ($Clang) { Good "clang    : $Clang" } else { Bad "clang.exe not found (install LLVM for Windows, see GUIDE.md step 2)"; $problems++ }
if ($Lld)   { Good "ld.lld   : $Lld" }   else { Bad "ld.lld.exe not found (comes with LLVM for Windows)"; $problems++ }
if ($Ar)    { Good "llvm-ar  : $Ar" }    else { Bad "llvm-ar.exe not found (comes with LLVM for Windows)"; $problems++ }

if ($Clang) {
    $ver = (& $Clang --version 2>&1 | Select-Object -First 1)
    Say "         $ver"
    if ("$ver" -notmatch 'version 18\.') {
        Warn "The OpenOrbis release recommended in GUIDE.md was built with LLVM 18. Other versions often work; if you get strange compiler errors, install LLVM 18.1.8."
    }
    # make the LLVM folder visible to the SDK's build_static.bat as well
    $env:PATH = (Split-Path $Clang) + ';' + $env:PATH
}

# ---------------------------------------------------------------- OpenOrbis
$tcCandidates = @($Toolchain, $env:OO_PS4_TOOLCHAIN, 'C:\OpenOrbis\PS4Toolchain',
                  (Join-Path $env:USERPROFILE 'OpenOrbis\PS4Toolchain'), 'C:\PS4Toolchain')
$Tc = $null
foreach ($c in $tcCandidates) {
    if ($c -and (Test-Path (Join-Path $c 'link.x'))) { $Tc = $c; break }
}
if ($Tc) {
    Good "OpenOrbis toolchain : $Tc"
    $Fself = Join-Path $Tc 'bin\windows\create-fself.exe'
    if (Test-Path $Fself) { Good "create-fself.exe     : found" }
    else { Bad "create-fself.exe missing at $Fself (re-extract the OpenOrbis toolchain)"; $problems++ }
    if (-not (Test-Path (Join-Path $Tc 'include\orbis\Pad.h'))) { Bad "include\orbis\Pad.h missing: wrong toolchain folder?"; $problems++ }
    $env:OO_PS4_TOOLCHAIN = $Tc
} else {
    Bad "OpenOrbis toolchain not found (looked for link.x in: C:\OpenOrbis\PS4Toolchain ...). Run setup.bat or see GUIDE.md step 3."
    $problems++
}

# ---------------------------------------------------------------- GoldHEN SDK
$sdkCandidates = @($Sdk, $env:GOLDHEN_SDK, 'C:\GoldHEN_Plugins_SDK', 'C:\GoldHEN_Plugins_SDK-main',
                   'C:\GoldHEN_Plugins_SDK\GoldHEN_Plugins_SDK-main',
                   (Join-Path $Proj 'GoldHEN_Plugins_SDK'), (Join-Path $Proj 'GoldHEN_Plugins_SDK-main'),
                   (Join-Path $env:USERPROFILE 'GoldHEN_Plugins_SDK'), (Join-Path $env:USERPROFILE 'GoldHEN_Plugins_SDK-main'))
$SdkDir = $null
foreach ($c in $sdkCandidates) {
    if ($c -and (Test-Path (Join-Path $c 'include\GoldHEN.h'))) { $SdkDir = $c; break }
}
if ($SdkDir) {
    Good "GoldHEN SDK        : $SdkDir"
    if (-not (Test-Path (Join-Path $SdkDir 'include\Detour.h'))) { Bad "include\Detour.h missing in the SDK"; $problems++ }
    else {
        # The hook macros (HOOK_INIT, HOOK32 ...) are defined in one of the SDK's
        # headers (Utilities.h in current versions), so look in all of them.
        $allHdr = ''
        foreach ($hf in (Get-ChildItem -Path (Join-Path $SdkDir 'include') -Filter '*.h')) {
            $allHdr += [string](Get-Content $hf.FullName -Raw)
        }
        foreach ($m in @('HOOK_INIT', 'HOOK32', 'HOOK_CONTINUE', 'UNHOOK')) {
            if ($allHdr -notmatch $m) { Warn "No SDK header mentions $m - this SDK version may be incompatible." }
        }
    }
} else {
    Bad "GoldHEN Plugins SDK not found (looked for include\GoldHEN.h in C:\GoldHEN_Plugins_SDK ...). Run setup.bat or see GUIDE.md step 4."
    $problems++
}

if ($problems -gt 0) {
    Stop-Build "$problems required item(s) missing - see the [FAIL] lines above and GUIDE.md."
}
if ($Check) {
    Say ""
    Say "Everything needed is installed. Run build.bat to build the plugin."
    exit 0
}

# ---------------------------------------------------------------- SDK library
Say ""
Say "Step 2: GoldHEN SDK library"
$SdkLib = Join-Path $SdkDir 'libGoldHEN_Hook.a'
$SdkCrt = Join-Path $SdkDir 'build\crtprx.o'
if ($RebuildSdk -or -not (Test-Path $SdkLib) -or -not (Test-Path $SdkCrt)) {
    $bat = Join-Path $SdkDir 'build_static.bat'
    if (-not (Test-Path $bat)) {
        Stop-Build "build_static.bat not found in $SdkDir. Download the SDK again from https://github.com/GoldHEN/GoldHEN_Plugins_SDK (Code > Download ZIP)."
    }
    Say "  Building the SDK library with its own build_static.bat (about a minute)..."
    Push-Location $SdkDir
    # 'echo.' answers any "press a key" prompt inside the SDK script
    cmd.exe /c "echo. | build_static.bat"
    Pop-Location
}
if (-not (Test-Path $SdkLib)) { Stop-Build "The SDK build did not produce libGoldHEN_Hook.a. Scroll up for the SDK's error message." }
if (-not (Test-Path $SdkCrt)) { Stop-Build "The SDK build did not produce build\crtprx.o. Scroll up for the SDK's error message." }
Good "libGoldHEN_Hook.a and crtprx.o are present"

# ---------------------------------------------------------------- flags (same as the original Makefile)
$cflags = @(
    '--target=x86_64-pc-freebsd12-elf', '-fPIC', '-funwind-tables', '-Wall',
    '-isysroot', $Tc,
    '-isystem', (Join-Path $Tc 'include'),
    '-isystem', (Join-Path $Tc 'include\orbis\_types'),
    ('-I' + (Join-Path $SdkDir 'include')),
    ('-I' + (Join-Path $Proj 'include')),
    ('-I' + $BuildDir),
    '-D__PS4__', '-D__ORBIS__'
)

New-Item -ItemType Directory -Force -Path $BuildDir, $ObjDir, $BinDir, $DistDir | Out-Null
$ProbeDir = Join-Path $BuildDir 'probe'
New-Item -ItemType Directory -Force -Path $ProbeDir | Out-Null

# ---------------------------------------------------------------- feature probes
# The OpenOrbis headers differ a little between versions. Instead of guessing,
# compile tiny test programs against YOUR headers and switch optional parts
# on or off. The build never fails just because an optional part is missing.
Say ""
Say "Step 3: checking your OpenOrbis / GoldHEN headers"

$Prelude = @'
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <orbis/libkernel.h>
#include <orbis/_types/pthread.h>
#include <orbis/Pad.h>
#include <orbis/Usbd.h>
#include <orbis/UserService.h>
#include <orbis/_types/errors.h>
#include <GoldHEN.h>
#include <Detour.h>
#include <Patcher.h>
#include <Utilities.h>
'@

function Test-Probe([string]$name, [string]$body, [bool]$showErrors) {
    $file = Join-Path $ProbeDir ($name + '.c')
    Set-Content -Path $file -Value ($Prelude + "`r`n" + $body) -Encoding ASCII
    $ccArgs = $cflags + @('-fsyntax-only', $file)
    $out = & $Clang @ccArgs 2>&1
    $ok = ($LASTEXITCODE -eq 0)
    if (-not $ok -and $showErrors) { $out | ForEach-Object { Write-Host "      $_" } }
    return $ok
}

# 3a. things the plugin cannot work without (these names all come from the
#     original working plugin and remotePad)
$required = @'
extern int32_t scePadReadExt(int32_t, OrbisPadData*, int32_t);
extern int32_t scePadReadStateExt(int32_t, OrbisPadData*);
extern int sys_dynlib_load_prx(const char*, int*);
extern const char* sceKernelGetFsSandboxRandomWord(void);
typedef int32_t (*scePadOpen_t)(int32_t, int32_t, int32_t, void*);
typedef int32_t (*scePadClose_t)(int32_t);
typedef int32_t (*scePadGetControllerInformation_t)(int32_t, OrbisPadInformation*);
HOOK_INIT(scePadRead);
HOOK_INIT(scePadReadState);
HOOK_INIT(scePadOpen);
HOOK_INIT(scePadClose);
HOOK_INIT(scePadGetControllerInformation);
int32_t scePadRead_hook(int32_t h, OrbisPadData* d, int32_t n) { return scePadReadExt(h, d, n); }
int32_t scePadReadState_hook(int32_t h, OrbisPadData* d) { return scePadReadStateExt(h, d); }
int32_t scePadOpen_hook(int32_t u, int32_t t, int32_t i, void* p) { return HOOK_CONTINUE(scePadOpen, scePadOpen_t, u, t, i, p); }
int32_t scePadClose_hook(int32_t h) { return HOOK_CONTINUE(scePadClose, scePadClose_t, h); }
int32_t scePadGetControllerInformation_hook(int32_t h, OrbisPadInformation* i) { return HOOK_CONTINUE(scePadGetControllerInformation, scePadGetControllerInformation_t, h, i); }
void probe_install(void) { HOOK32(scePadRead); HOOK32(scePadReadState); HOOK32(scePadOpen); HOOK32(scePadClose); HOOK32(scePadGetControllerInformation); }
void probe_remove(void) { UNHOOK(scePadRead); UNHOOK(scePadReadState); UNHOOK(scePadOpen); UNHOOK(scePadClose); UNHOOK(scePadGetControllerInformation); }
void probe_pad(OrbisPadData* d, OrbisPadInformation* i) {
    d->buttons = ORBIS_PAD_BUTTON_L3 | ORBIS_PAD_BUTTON_R3 | ORBIS_PAD_BUTTON_OPTIONS | ORBIS_PAD_BUTTON_UP | ORBIS_PAD_BUTTON_RIGHT |
                 ORBIS_PAD_BUTTON_DOWN | ORBIS_PAD_BUTTON_LEFT | ORBIS_PAD_BUTTON_L2 | ORBIS_PAD_BUTTON_R2 | ORBIS_PAD_BUTTON_L1 |
                 ORBIS_PAD_BUTTON_R1 | ORBIS_PAD_BUTTON_TRIANGLE | ORBIS_PAD_BUTTON_CIRCLE | ORBIS_PAD_BUTTON_CROSS |
                 ORBIS_PAD_BUTTON_SQUARE | ORBIS_PAD_BUTTON_TOUCH_PAD;
    d->leftStick.x = 1; d->leftStick.y = 1; d->rightStick.x = 1; d->rightStick.y = 1;
    d->analogButtons.l2 = 1; d->analogButtons.r2 = 1; d->connected = 1; d->timestamp = 1;
    i->connected = 1; i->connectionType = ORBIS_PAD_CONNECTION_TYPE_STANDARD; i->deviceClass = ORBIS_PAD_DEVICE_CLASS_PAD;
    i->touchpadDensity = 1.0f; i->touchResolutionX = 1; i->touchResolutionY = 1;
}
int32_t probe_errors(void) { return ORBIS_PAD_ERROR_INVALID_ARG + ORBIS_PAD_ERROR_INVALID_HANDLE + ORBIS_PAD_ERROR_ALREADY_OPENED + ORBIS_PAD_ERROR_DEVICE_NO_HANDLE; }
void probe_usb(void) {
    libusb_device** list = 0; libusb_device_handle* h = 0; struct libusb_device_descriptor desc; int32_t n = 0;
    sceUsbdInit(); sceUsbdGetDeviceList(&list); sceUsbdGetDeviceDescriptor(list[0], &desc);
    sceUsbdOpen(list[0], &h); sceUsbdDetachKernelDriver(h, 0); sceUsbdClaimInterface(h, 0);
    sceUsbdSetInterfaceAltSetting(h, 0, 0); sceUsbdInterruptTransfer(h, 0x82, (uint8_t*)&n, 4, &n, 10);
    sceUsbdReleaseInterface(h, 0); sceUsbdClose(h); sceUsbdFreeDeviceList(list); sceUsbdExit();
    (void)desc.idVendor; (void)desc.idProduct;
}
void probe_kernel(void) {
    OrbisPthreadMutex m; OrbisPthread t; OrbisNotificationRequest r;
    scePthreadMutexInit(&m, 0, "x"); scePthreadMutexLock(&m); scePthreadMutexUnlock(&m);
    scePthreadCreate(&t, 0, (void*)0, 0, "x"); scePthreadJoin(t, 0);
    sceKernelUsleep(1); (void)sceKernelGetProcessTime();
    r.type = NotificationRequest; r.targetId = -1; (void)r.message;
    sceKernelSendNotificationRequest(0, &r, sizeof(r), 0);
}
void probe_user(void) {
    OrbisUserServiceLoginUserIdList l; int32_t fg = 0;
    l.userId[0] = ORBIS_USER_SERVICE_USER_ID_INVALID; (void)ORBIS_USER_SERVICE_MAX_LOGIN_USERS;
    sceUserServiceGetLoginUserIdList(&l); sceUserServiceGetForegroundUser(&fg);
}
'@
if (Test-Probe 'required' $required $true) {
    Good "required PS4/GoldHEN functions and macros: all present"
} else {
    Bad "Your OpenOrbis or GoldHEN headers do not match what the plugin needs (errors above)."
    Say  "      Fix: use the OpenOrbis release v0.5.3 (LLVM 18) and the GoldHEN SDK 'main' branch,"
    Say  "      exactly as described in GUIDE.md steps 3 and 4."
    Stop-Build "header check failed"
}

# 3b. optional parts
$features = [ordered]@{}
$features['XBS_HAVE_PADDATA_COUNT']    = (Test-Probe 'f_pd_count'  'void p(OrbisPadData* d){ d->count = 1; }' $false)
$features['XBS_HAVE_PADDATA_MOTION']   = (Test-Probe 'f_pd_motion' 'void p(OrbisPadData* d){ d->quat.x=0; d->quat.y=0; d->quat.z=0; d->quat.w=1; d->vel.x=0; d->vel.y=0; d->vel.z=0; d->acell.x=0; d->acell.y=0; d->acell.z=1; d->touch.fingers=0; }' $false)
$features['XBS_HAVE_PADINFO_COUNT']    = (Test-Probe 'f_pi_count'  'void p(OrbisPadInformation* i){ i->count = 1; }' $false)
$features['XBS_HAVE_PADINFO_DEADZONE'] = (Test-Probe 'f_pi_dz'     'void p(OrbisPadInformation* i){ i->stickDeadzoneL = 1; i->stickDeadzoneR = 1; }' $false)
$features['XBS_HAVE_KFILE']            = (Test-Probe 'f_kfile'     'void p(void){ int fd = sceKernelOpen("/x", 0, 0); char b[4]; sceKernelRead(fd, b, 4); sceKernelWrite(fd, b, 4); sceKernelClose(fd); }' $false)

$hookTemplate = @'
@EXTRA@
typedef int32_t (*@NAME@_t)(@TYPES@);
HOOK_INIT(@NAME@);
int32_t @NAME@_hook(@PARAMS@) { return HOOK_CONTINUE(@NAME@, @NAME@_t, @ARGS@); }
void probe_install(void) { HOOK32(@NAME@); }
void probe_remove(void) { UNHOOK(@NAME@); }
'@
$optionalHooks = @(
    @{ N='scePadGetHandle';                          T='int32_t, uint32_t, uint32_t';              P='int32_t a, uint32_t b, uint32_t c'; A='a, b, c' },
    @{ N='scePadSetLightBar';                        T='int32_t, OrbisPadColor*';                  P='int32_t a, OrbisPadColor* b';        A='a, b' },
    @{ N='scePadResetLightBar';                      T='int32_t';                                  P='int32_t a';                          A='a' },
    @{ N='scePadSetVibration';                       T='int32_t, const OrbisPadVibeParam*';        P='int32_t a, const OrbisPadVibeParam* b'; A='a, b' },
    @{ N='scePadResetOrientation';                   T='int32_t';                                  P='int32_t a';                          A='a' },
    @{ N='scePadSetMotionSensorState';               T='int32_t, bool';                            P='int32_t a, bool b';                  A='a, b' },
    @{ N='scePadSetTiltCorrectionState';             T='int32_t, bool';                            P='int32_t a, bool b';                  A='a, b' },
    @{ N='scePadSetAngularVelocityDeadbandState';    T='int32_t, bool';                            P='int32_t a, bool b';                  A='a, b' },
    @{ N='scePadDeviceClassParseData';               T='int32_t, const OrbisPadData*, void*';      P='int32_t a, const OrbisPadData* b, void* c'; A='a, b, c' },
    @{ N='scePadDeviceClassGetExtendedInformation';  T='int32_t, void*';                           P='int32_t a, void* b';                 A='a, b' }
)
foreach ($h in $optionalHooks) {
    $code = $hookTemplate.Replace('@NAME@', $h.N).Replace('@TYPES@', $h.T).Replace('@PARAMS@', $h.P).Replace('@ARGS@', $h.A).Replace('@EXTRA@', '')
    $features['XBS_HAVE_FN_' + $h.N] = (Test-Probe ('hook_' + $h.N) $code $false)
}

$hdr = @('/* Generated by build.ps1 from probes against YOUR OpenOrbis/GoldHEN headers. Do not edit. */')
foreach ($k in $features.Keys) {
    $v = 0
    if ($features[$k]) { $v = 1 }
    $hdr += "#define $k $v"
    if ($v -eq 1) { Good "$k = 1" } else { Warn "$k = 0 (not available in your headers; that optional part is skipped)" }
}
Set-Content -Path (Join-Path $BuildDir 'xbs_features.h') -Value $hdr -Encoding ASCII

# ---------------------------------------------------------------- compile
Say ""
Say "Step 4: compiling the plugin"
$objs = @()
$sources = Get-ChildItem -Path (Join-Path $Proj 'src') -Filter '*.c'
foreach ($c in $sources) {
    $o = Join-Path $ObjDir ($c.BaseName + '.o')
    Say "  compiling $($c.Name)"
    $ccArgs = $cflags + @('-c', $c.FullName, '-o', $o)
    & $Clang @ccArgs
    if ($LASTEXITCODE -ne 0) {
        Stop-Build "Compiling $($c.Name) failed. The red text above is the compiler's message (see GUIDE.md 'Common build errors')."
    }
    $objs += $o
}
Good "compiled $($objs.Count) source files"

# ---------------------------------------------------------------- link
Say ""
Say "Step 5: linking"
$Elf  = Join-Path $BinDir ($PluginName + '.elf')
$Oelf = Join-Path $BinDir ($PluginName + '.oelf')
$Prx  = Join-Path $BinDir ($PluginName + '.prx')
foreach ($f in @($Elf, $Oelf, $Prx)) { if (Test-Path $f) { Remove-Item -Force $f } }

$ldArgs = @($SdkCrt) + $objs + @(
    '-o', $Elf,
    '-m', 'elf_x86_64', '-pie',
    '--script', (Join-Path $Tc 'link.x'),
    '-e', '_init', '--eh-frame-hdr',
    ('-L' + (Join-Path $Tc 'lib')),
    ('-L' + $SdkDir),
    '-lSceLibcInternal', '-lGoldHEN_Hook', '-lkernel', '-lScePad', '-lSceUsbd', '-lSceSysmodule', '-lSceUserService'
)
& $Lld @ldArgs
if ($LASTEXITCODE -ne 0) {
    Stop-Build "Linking failed (red text above). 'undefined symbol' lines tell which function or library is missing."
}
Good "linked $Elf"

# ---------------------------------------------------------------- PRX
Say ""
Say "Step 6: creating the PRX"
& $Fself ("-in=" + $Elf) ("-out=" + $Oelf) ("--lib=" + $Prx) '--paid' '0x3800000000000011'
if ($LASTEXITCODE -ne 0 -or -not (Test-Path $Prx)) {
    Stop-Build "create-fself.exe failed (message above)."
}
$size = (Get-Item $Prx).Length
if ($size -lt 4096) { Stop-Build "The PRX is only $size bytes - something went wrong." }

Copy-Item -Force $Prx (Join-Path $DistDir ($PluginName + '.prx'))
foreach ($f in @('xbox_series.ini.example', 'plugins.ini.example')) {
    $src = Join-Path $Proj $f
    if (Test-Path $src) { Copy-Item -Force $src (Join-Path $DistDir $f) }
}

Say ""
Write-Host "BUILD SUCCESSFUL" -ForegroundColor Green
Say "  Plugin file : $(Join-Path $DistDir ($PluginName + '.prx'))   ($size bytes)"
Say "  Next step   : copy it to the PS4 (see GUIDE.md, 'Installing on the PS4')."

# ---------------------------------------------------------------- optional upload
if ($Upload) {
    if (-not $Ps4Ip) { Stop-Build "Use:  build.ps1 -Upload -Ps4Ip <your PS4's IP address>" }
    $curl = Get-Command curl.exe -ErrorAction SilentlyContinue
    if (-not $curl) { Stop-Build "curl.exe not found (it is included in Windows 10/11)." }
    Say ""
    Say "Uploading to ftp://$Ps4Ip`:2121/data/GoldHEN/plugins/ ..."
    & $curl.Source '--ftp-create-dirs' '-T' (Join-Path $DistDir ($PluginName + '.prx')) "ftp://${Ps4Ip}:2121/data/GoldHEN/plugins/"
    if ($LASTEXITCODE -eq 0) { Good "uploaded" } else { Bad "upload failed (is the PS4 on, GoldHEN loaded, and its FTP server enabled?)" }
}
exit 0
