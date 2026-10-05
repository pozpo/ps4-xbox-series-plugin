# Xbox Series X|S plugin for PS4 (GoldHEN) — complete beginner guide

Everything here runs **natively on Windows 11**. No WSL, no Ubuntu, no Docker, no virtual machine.


---

## 1. What is in this folder

| File / folder | What it is |
|---|---|
| `src\*.c`, `include\*.h` | the plugin source code (you never have to edit it) |
| `build.bat` | **double-click to build** `dist\xbox_series.prx` |
| `check.bat` | double-click to check that all tools are installed |
| `setup.bat` | double-click to download the OpenOrbis toolchain and GoldHEN SDK |
| `build.ps1`, `setup.ps1` | the PowerShell scripts those `.bat` files run |
| `plugins.ini.example` | example GoldHEN plugin list |
| `xbox_series.ini.example` | example plugin settings |
| `host_tests\` | developer tests (Linux only; you can ignore this folder) |

---

## 2. Setup on Windows 11 (one time, about 30 minutes)

### Step 1 — Put the project in a short folder

Unzip the project so you get **`C:\xbox_series_plugin`** (so `C:\xbox_series_plugin\build.bat` exists).
Avoid folders with spaces in the name (for example not `C:\Users\John Smith\Downloads`).

### Step 2 — Install LLVM (the compiler)

**Open PowerShell:** press the Windows key, type `PowerShell`, press Enter. A blue/black text window opens.

**Paste this and press Enter:**

```powershell
winget install LLVM.LLVM --version 18.1.8
```

*What it does:* downloads and installs the LLVM compiler (`clang`, `ld.lld`, `llvm-ar`).
*Expected result:* progress text, then `Successfully installed`.
*If `winget` is not recognised:* download `LLVM-18.1.8-win64.exe` from
<https://github.com/llvm/llvm-project/releases/tag/llvmorg-18.1.8>, run it, and on the page
"Install Options" choose **"Add LLVM to the system PATH for all users"** (or "for current user").

**Close the PowerShell window and open a new one**, then check:

```powershell
clang --version
```

*Expected:* first line `clang version 18.1.8`. (Another version usually still works, but 18 is what the
OpenOrbis release below was built with.)

### Step 3 — OpenOrbis toolchain (headers, linker script, `create-fself.exe`)

**Easy way:** double-click **`setup.bat`**. It downloads the toolchain release `v0.5.3` (several hundred MB) and
extracts it to `C:\OpenOrbis\PS4Toolchain`, and it also downloads the GoldHEN SDK (step 4).
*Expected end of output:* `[ OK ] Toolchain installed: C:\OpenOrbis\PS4Toolchain`.

**By hand (if setup.bat fails):**
1. Open <https://github.com/OpenOrbis/OpenOrbis-PS4-Toolchain/releases/tag/v0.5.3> and download `toolchain-llvm-18.2.zip`.
2. Right-click it → *Extract All* → open the extracted folder; inside is `toolchain-llvm-18.tar.gz`.
3. In PowerShell run (change the path if yours differs):
   ```powershell
   tar -xzf "C:\Users\YOU\Downloads\toolchain-llvm-18.2\toolchain-llvm-18.tar.gz" -C C:\
   ```
   Warnings about symbolic links are normal.
4. Check that `C:\OpenOrbis\PS4Toolchain\link.x` and `C:\OpenOrbis\PS4Toolchain\bin\windows\create-fself.exe` exist.

*Why v0.5.3:* it is the version remotePad's own build uses, and the original plugin's code compiles with it
(inference from the uploaded sources; newer "master" headers rename some structure fields).

### Step 4 — GoldHEN Plugins SDK

`setup.bat` (step 3) already downloaded it to `C:\GoldHEN_Plugins_SDK`.
By hand: open <https://github.com/GoldHEN/GoldHEN_Plugins_SDK>, click **Code → Download ZIP**, extract it, and rename/move the
folder so that `C:\GoldHEN_Plugins_SDK\include\GoldHEN.h` exists.

### Step 5 — Check everything

Double-click **`check.bat`**. Expected: all lines `[ OK ]` and
`Everything needed is installed.` Any `[FAIL]` line tells you what is missing; fix it and run `check.bat` again.

---

## 3. Building the plugin

Double-click **`build.bat`** (if Windows SmartScreen warns about the file: *More info → Run anyway*).

What happens, in order:

1. looks for clang / lld / OpenOrbis / SDK,
2. builds the GoldHEN SDK library with the SDK's own `build_static.bat` (first time only),
3. **tests your OpenOrbis/GoldHEN headers** with tiny sample programs and switches optional parts on/off
   (lines like `[WARN] XBS_HAVE_FN_scePadSetMotionSensorState = 0` are fine: that optional hook is just skipped),
4. compiles every file in `src\`, links, and creates the PRX.

*Expected end:*

```
BUILD SUCCESSFUL
  Plugin file : C:\xbox_series_plugin\dist\xbox_series.prx   (… bytes)
```

The result is **`C:\xbox_series_plugin\dist\xbox_series.prx`**.

### Common build errors

| You see | Meaning / fix |
|---|---|
| `[FAIL] clang.exe not found` | LLVM not installed or window opened before installing. Do step 2, open a **new** window. |
| `[FAIL] OpenOrbis toolchain not found` | Run `setup.bat` or do step 3 by hand; the folder must contain `link.x`. |
| `[FAIL] GoldHEN Plugins SDK not found` | Do step 4; `include\GoldHEN.h` must exist. |
| `The SDK build did not produce libGoldHEN_Hook.a` | The SDK's own script failed; read the red text above it. Make sure LLVM is installed and the SDK path has no spaces. |
| `Your OpenOrbis or GoldHEN headers do not match …` | Wrong OpenOrbis version. Use v0.5.3 (step 3). The lines under it show the exact compiler message — send them to me. |
| `Compiling xxx.c failed` | Copy the red compiler message to me. |
| `Linking failed … undefined symbol: X` | Copy the line to me; it names the missing function/library. |
| `running scripts is disabled` | Use `build.bat` (it bypasses the policy) instead of running `build.ps1` by hand. |

To start fresh: `powershell -ExecutionPolicy Bypass -File build.ps1 -Clean`.

---

## 4. Installing on the PS4

Requirements: PS4 with **GoldHEN loaded** (your usual jailbreak routine), PS4 and PC on the same network.

1. **Find the PS4's IP address:** PS4 *Settings → Network → View Connection Status*. Example: `192.168.1.50`.
2. **Get an FTP program for Windows**, e.g. FileZilla Client (free). *Host* = the PS4 IP, *Port* = `2121`,
   user/password = anything (the original plugin's Makefile used `ps4` / `ps4`). Click *Quickconnect*.
   GoldHEN has a built-in FTP server on port 2121; if the connection is refused, enable the FTP option in GoldHEN's
   settings menu (menu layout varies by GoldHEN version — **unverified for your version**).
3. **Remove the old plugin** if installed: delete `xbox_controller.prx` and remove its line from `plugins.ini`
   (two plugins hooking the same functions will fight).
4. **Upload** `dist\xbox_series.prx` to `/data/GoldHEN/plugins/` on the PS4.
   (Shortcut with no FTP program: `powershell -ExecutionPolicy Bypass -File build.ps1 -Upload -Ps4Ip 192.168.1.50`.)
5. **Edit `/data/GoldHEN/plugins.ini`** (download it, edit it in Notepad, upload it). Add:
   ```
   [default]
   /data/GoldHEN/plugins/xbox_series.prx
   ```
   Better: load it only for the game you test, using that game's title ID (shown in the game's *Options → Information*):
   ```
   [CUSA00000]
   /data/GoldHEN/plugins/xbox_series.prx
   ```
6. *(Optional)* upload `xbox_series.ini.example` as `/data/GoldHEN/xbox_series.ini` and edit it (settings in section 6).
7. **Log in the extra users.** The second/third player must be a **logged-in PS4 user**
   (*PS4 home → press the PS button on a controller → choose a profile*, or *Add user*).
   This is a PS4 rule, not a plugin rule: a game only opens controllers for logged-in users.
8. **Start the game.** You should see a pop-up `Xbox Series plugin 1.0.0 loaded`.

**Removing the plugin if something goes wrong:** delete its line from `plugins.ini` (or delete the `.prx`) and restart
the game. Nothing else on the PS4 is modified.

---

## 5. How it works (and what changed)

```
Xbox Series X|S #1 --USB--> reader thread #1 --\                             /-- real DualShock 4 (Player 1) --- untouched
Xbox Series X|S #2 --USB--> reader thread #2 ---+--> newest state per pad    |
                         (scanner thread: plug / unplug)  |                   |
                                                           v                   |
                      Series parser -> translator -> virtual pad manager ----> scePad hooks ---> Game
                                                    (pad <-> user assignment)  (Open/Close/GetHandle/Read/ReadState/Info)
```

* **Game-facing hooks never wait for USB.** They only copy the newest stored state. (Old plugin: the game thread itself
  did a blocking USB read with a 2 ms timeout inside `scePadRead`.)
* **Real DualShock 4s pass through byte-for-byte**, using the same mechanism as the original plugin.
* **An Xbox pad only steps in for a user whose DualShock 4 is not connected** (for at least 0.3 s), never for the user who
  launched the game (Player 1) unless you allow it, and it steps aside again the moment a DualShock 4 connects.

### Problems from the old code and what was done

Evidence labels: **[source]** seen in the uploaded code, **[external]** from outside documentation,
**[inference]** my reasoning, needs hardware testing.

| Problem | Cause | Fix in this version |
|---|---|---|
| Only one Xbox pad | [source] one global USB handle / user / handle `1001`; the 4-slot USB code was never called and only knew Xbox 360 | per-controller slots, each with its own USB handle, reader thread, cached state, owner user |
| Two identical pads | [source] dead code identified devices by VID/PID, which are identical | devices identified by their USB device object; never opened twice (tested) |
| Latency | [source] blocking 2 ms USB read in the game's own `scePadRead` path, one packet per call | background reader per pad; hooks copy cached state (tested: 20 000 reads in < 0.4 s, packet→game ≈ 4 ms on the test PC) |
| Buttons need repeated presses | [source] USB data written straight into the shared cache before validation, so non-input or short packets corrupted it; no locking. [inference] very short taps between polls were lost | parse into a private buffer and validate first; per-pad lock; short taps are *latched* so a tap shorter than one game poll is still seen once (tested) |
| Stick Fight loops "ready" | [inference] the game asks `scePadGetHandle` ("do I already have a pad for this user?"). The old plugin did not hook it, so the PS4 answered "no" for the fake handle, the game opened again and again and never read the pad (which also explains why "Controller input active" never appeared). remotePad hooks `scePadGetHandle` for exactly this reason | `scePadGetHandle` is hooked; repeated `scePadOpen` returns `ALREADY_OPENED` (what the PS4 returns) with **no** pop-up; setters (light bar, vibration, motion, device class) are answered for virtual handles; real handles are used whenever the PS4 provides one |
| "Xbox Player 2 ready!" spam | [source] pop-up on every open | removed; pop-ups only on events (loaded, connected, input OK, assigned, disconnected) and rate-limited |
| Timestamps | [source] translator overwrote the timestamp with a global counter starting at 0 (shared by all pads, jumps backwards from microseconds to a small number) | per-pad process-time microseconds, strictly increasing (tested). I use read time rather than USB arrival time because an idle Xbox pad sends no packets, which would make the stamp stale |
| Series shown as "Xbox One" | [source] PID `0x0B12` in an Xbox One list; fixed 18-byte struct cast | dedicated Series module `xbox_series.[ch]`: little-endian field parsing, packet length read from the header (Series firmware sends 36/44/48 bytes [external: SDL]), strict validation |
| Player assignment | [source] "any non-foreground user" first come first served | rule in `virtual_pad.c` (see above), optional `xbox_players=` override |

---

## 6. Settings file `/data/GoldHEN/xbox_series.ini`

All optional; see `xbox_series.ini.example` (it documents every line). Most useful:

| Key | Default | Meaning |
|---|---|---|
| `stick_deadzone` | 12 | % of stick travel ignored around the centre |
| `trigger_threshold` | 12 | % at which digital L2/R2 turn on (analog value is always sent) |
| `view_button` | share | `touchpad` makes the View button a touchpad click |
| `allow_player1` | 0 | 1 lets the launching user use an Xbox pad when no DS4 is on |
| `xbox_players` | empty | e.g. `2,3` = only the 2nd and 3rd logged-in users may get Xbox pads |
| `notifications` | 1 | pop-up messages |
| `log_to_file` / `log_verbose` | 1 / 1 | write the log, with the first calls of every pad function and raw USB packet dumps |

Mapping: A→Cross, B→Circle, X→Square, Y→Triangle, LB→L1, RB→R1, LT→L2, RT→R2, L3/R3, D-pad,
Menu→Options, View→Share (or Touchpad click). The Xbox/Guide button and the Series "Share" button are not mapped
(the PS4 reserves its own PS button). Rumble is **not** implemented (the plugin accepts and ignores vibration requests).

---

## 7. The log file

The plugin writes `/data/GoldHEN/xbox_series.log` (re-created each time the plugin loads). Download it with FTP after a test.
Notable lines:

| Line | Meaning |
|---|---|
| `USB initialised` / `hooks installed` | plugin started correctly |
| `first pad call: starting USB manager` | the game started using pads |
| `Xbox Series #1 connected` | USB device opened and claimed |
| `slot 0 raw INPUT len=NN: 20 00 xx 0E …` | first raw packets from the pad (**please send me these**) |
| `Xbox #1: input OK` | first valid input packet arrived |
| `scePadOpen user=0x… pos=2 fg=0 -> real handle 3` | game opened a pad; PS4 gave a real handle |
| `… -> virtual handle 1001` | PS4 refused, plugin invented a handle |
| `scePadReadState real 3 -> Xbox overlay` | the Xbox pad is now answering for that user |
| `Xbox #1 -> Player 2` | assignment made |
| `Xbox slot 0 released from user … (real pad connected)` | a DS4 turned on, Xbox stepped aside |
| `sceUsbdOpen failed: 0x…`, `sceUsbdClaimInterface failed: 0x…` | USB problems (send me the code) |
| `slot N: 100 instant USB errors in a row … -> dead` | controller unplugged or stuck; it is re-opened automatically |

---

## 8. Test plan (do them in this order; report back what I ask for)

Always: delete the old log by restarting the game, run the test, download `xbox_series.log`.
**Report for every test:** (a) pass/fail, (b) the pop-ups you saw, (c) the log file, (d) game name + PS4 firmware + GoldHEN version.

| # | Setup | Do | Expected | Failure means |
|---|---|---|---|---|
| 1 | **One Xbox** only (no DS4 needed if you can start the game) plugged before launch, one user | launch a game, press all buttons, move sticks, press triggers | `loaded`, `Xbox Series #1 connected`, `input OK`; game reacts. Set `allow_player1=1` for this test if the Xbox is the only pad | no `connected` → USB open problem (log has code); `connected` but no `input OK` → see raw packets in log |
| 2 | **DS4 = Player 1, Xbox = Player 2** (second user logged in) | launch, make Player 2 join (Start) | after ~0.3 s: `Xbox #1 -> Player 2`; DS4 keeps working | assignment missing → send log; P1 DS4 broken → stop and tell me immediately |
| 3 | **Two Xbox** + DS4 = P1, users 2 and 3 logged in (needs both USB ports) | join P2, P3 | `Xbox #1 -> Player 2`, `Xbox #2 -> Player 3`, no cross-talk | wrong order → set `xbox_players=2,3` and tell me |
| 4 | DS4 P1 + **two Xbox** | same as 3 | as 3 | |
| 5 | **DS4 = P1, DS4 = P3, Xbox = P2** | join all three | Xbox goes to P2, both DS4s untouched | Xbox on P3 → log |
| 6 | Xbox **plugged in before** launching | | as test 2 | |
| 7 | Xbox **plugged in after** the game started | plug in, wait ~2 s | `Xbox Series #1 connected`, then assignment | if the game never uses it, the game may have given up on that player: try test 8 order |
| 8 | **Unplug and replug** during play | unplug, wait 3 s, replug | `disconnected`, later `connected`; same player gets it back, no second assignment pop-up | |
| 9 | **Rapid button taps** | mash A as fast as you can | every tap registers | missed taps → send log + describe |
| 10 | **Hold buttons** | hold each button 5 s | stays pressed, no flicker | |
| 11 | **Sticks** | slowly circle both sticks, release | smooth movement, rests at centre without drift | drift → raise `stick_deadzone` |
| 12 | **Triggers** | slowly press LT/RT | smooth analog response | |
| 13 | **Stick Fight: The Game**, DS4 P1, Xbox P2 (and P3 with a second Xbox) | join with the Xbox | joins **once**; **no** repeated pop-ups | still looping → send the log: the `scePadOpen`/`scePadGetHandle` lines show exactly what the game does |
| 14 | **Diablo 3** (README says drop-in works) | Start on Xbox to join | joins as Player 2 | |
| 15 | Another local multiplayer game | | | |

## Acknowledgments & Attribution

* **AI Collaboration:** Developed with the assistance of **Claude AI** (by Anthropic), which helped design, structure, and write the code and documentation.
* **Original References:** This project builds upon concepts and code structures from:
  * [crucifix86/xbox_controller_plugin](https://github.com/crucifix86/xbox_controller_plugin)
  * [xfangfang/remotePad](https://github.com/xfangfang/remotePad)
    
