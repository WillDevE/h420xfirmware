# H420X (T223) custom firmware — install, how it was built, what changed vs stock, how flashing works

This folder is a self-contained release. It contains the final firmwares, the
`.bat` flasher (with its USB driver files bundled), and this writeup. Nothing
else is needed to install or verify it.

## Install (Windows 10/11)

1. Double-click `flash.bat`.
2. Recommended the first time: choose `S` to save a backup of the firmware your
   tablet has now (nothing on the tablet is changed). Then run `flash.bat` again.
3. Type the number of the firmware you want and press Enter.
4. When it says so, unplug the tablet's USB cable and plug it back in.
5. Wait for "DONE", then unplug and plug the tablet in once more.

Every install also saves the tablet's current firmware to the `backups` folder
first, named after what it is (for example `backup_<date>_stock_aef9a6df.bin`).
To go back: run `flash.bat` and choose `R` to restore a backup, or install
`stock.bin`. If anything goes wrong, just run `flash.bat` again: the part of
the tablet that makes updating possible (the bootloader) is never touched, so
it can always be fixed.

No download is needed: the two files the flasher uses to talk USB
(`gigadevice\GD_DFU_DLL.dll` from GigaDevice's free "GD32 All-In-One
Programmer" v5.4.0.42035, plus `vcruntime140.dll` beside it) are bundled and
checked locally first. Only if those local copies are missing would the script
fall back to searching the computer or asking you to download them.

### If the `gigadevice` files are missing

The flasher looks for `GD_DFU_DLL.dll` next to `flash.bat`, then in
`gigadevice\`, so putting the files back in `gigadevice\` is enough — no
install step, just the two files side by side. Get them from:

- `GD_DFU_DLL.dll` (32-bit, tested v5.4.0.42035) and `vcruntime140.dll` —
  free "GD32 All-In-One Programmer" from the chip maker, GigaDevice:
  https://www.gd32mcu.com/en/download?kw=GD32+All-In-One+Programmer
  (download it, then copy those two files out of it into `gigadevice\`;
  `flash.ps1` can also do this for you — press `D` to open that page or `B`
  to point it at the file you downloaded, including the still-zipped
  download).
- `vcruntime140.dll` alone (32-bit) — Microsoft's free Visual C++
  Redistributable (x86), if you would rather not unpack the GigaDevice
  download for it:
  https://aka.ms/vs/17/release/vc_redist.x86.exe
  (install it, or place its 32-bit `vcruntime140.dll` next to
  `GD_DFU_DLL.dll`).

After putting them back, check they match before running:

```powershell
certutil -hashfile gigadevice\GD_DFU_DLL.dll SHA256
certutil -hashfile gigadevice\vcruntime140.dll SHA256
```

against the hashes in the table below. `flash.ps1` re-checks the DLL hash
itself and asks for an explicit `YES` before using a different version.

One thing Windows may still ask for: the DFU driver. Only if `flash.bat` says
"Windows has no driver for the tablet's update mode", install "GD32 Dfu
Drivers" once (https://www.gd32mcu.com/en/download?kw=GD32+Dfu+Drivers).
The driver (`GDDFUDriver.sys`) is signed by Microsoft. PCs that already ran
Huion's own firmware updater may have it already.

After installing, move the pen around for a few seconds: the firmware quickly
learns tiny differences between its sensor reads and then stays consistent.

## Contents

```text
H420X-Final-Release/
  firmware/
    rhythm900.bin   ~910 reports/s, cursor only (no pressure/tip/buttons)
    rhythm750.bin   ~765 reports/s, cursor only (cleanest high rate)
    pen500.bin      exactly 500 reports/s, pressure + tip + buttons
    pen333.bin      exactly 333 reports/s, pressure + tip + buttons
    stock.bin       Huion factory firmware T223_220707 (untouched)
  gigadevice/
    GD_DFU_DLL.dll    GigaDevice USB/DFU library (32-bit, v5.4.0.42035)
    vcruntime140.dll  Microsoft C++ runtime the DLL needs beside it
  flash.bat         7-line launcher: runs flash.ps1 in 32-bit PowerShell
  flash.ps1         the actual installer (plain text, read it in Notepad)
  SHA256SUMS.txt    hashes of the firmwares, scripts, and bundled DLLs
  README.md         this file (install + full writeup)
```

Verify any file yourself:

```powershell
certutil -hashfile firmware\rhythm900.bin SHA256
certutil -hashfile gigadevice\GD_DFU_DLL.dll SHA256
```

Expected hashes (also in `SHA256SUMS.txt`):

| File | SHA-256 |
|---|---|
| `firmware\rhythm900.bin` | `860b3645154c4b963a54356875bf5d9ec53b60ab3f3ad96aa636e4ac41929c7f` |
| `firmware\rhythm750.bin` | `842283679e7969b4a10ad1573a29dda661fa23cf751c5b32fba91d3c9d343d76` |
| `firmware\pen500.bin` | `a1821663297217b1ba4bb7f20ed7f1d738988a8aedd58a3c9ef589f6d32c4e02` |
| `firmware\pen333.bin` | `ef57e32b05f1eea2234a457b61c55595e69df73623dc255bc05248797fa405ad` |
| `firmware\stock.bin` | `aef9a6df0806fa9ce9a182c6c2974e26d36a4707b9cd752062d76c638cd8b630` |
| `gigadevice\GD_DFU_DLL.dll` | `c97d2a6a94747d2c7532bb98ed447008319c3d74e87d96cdf018b54602a5ad14` |
| `gigadevice\vcruntime140.dll` | `9faeaa45e8cc986af56f28350b38238b03c01c355e9564b849604b8d690919c5` |

What you are running (so you do not have to trust us):

- `flash.bat` — 7 lines: starts `flash.ps1` in Windows' own 32-bit PowerShell
  (needed because GigaDevice's DLL is 32-bit).
- `flash.ps1` — plain text, commented, open it in Notepad. It only talks to the
  tablet's update mode (USB `28E9:0189`), uses the bundled `gigadevice\`
  copies first (hash-checked against the tested version), refuses any
  bootloader other than the exact H420X/T223 one, backs up the current
  firmware, writes ONLY the 32,000-byte application area at `0x08004000`, and
  verifies every byte. It never writes the bootloader or the settings page. No
  admin rights, nothing installed; the only internet use is opening
  GigaDevice's page if you press `D` in the download fallback.
- `SHA256SUMS.txt` — hashes of the firmwares, scripts, and DLLs. `flash.ps1`
  refuses a firmware whose hash does not match.
- Files created next to `flash.bat` when you run it: `backups\` (your
  firmware), `gigadevice_dll_location.txt` (which DLL copy was used).

## Firmware

All four custom firmwares apply zero cursor smoothing: every report comes only
from that scan's fresh coil reads, with no averaging and no added latency.

- `rhythm900.bin` — ~910 reports/s. Cursor only: pressure, tip and pen buttons
  are OFF (use keyboard keys for hits). For rhythm games. Hardware-tested:
  910–915 reports/s, zero dropped reports, steady jitter 2.7/3.4 units, no
  jumps at coil crossings in fast aim.
- `rhythm750.bin` — ~765 reports/s. Cursor only, like rhythm900, with slower
  and more careful coil timing. The cleanest high-rate option.
  Hardware-tested: 766 reports/s, zero dropped reports, no jumps at coil
  crossings in any test.
- `pen500.bin` — Exactly 500 reports/s, one every 2 ms. Pressure, tip and pen
  buttons ON. Reports are timed to the USB frames, so they arrive perfectly
  evenly. Hardware-tested (500.29/s, no jumps at coil crossings).
- `pen333.bin` — Exactly 333 reports/s, one every 3 ms. Same as pen500, with
  one report per 3 ms and more frequent pen-frequency checks.
- Pressure on the pen tiers is read fresh from the pen every scan, as a
  straight line (no averaging and no pressure curve); Huion's tip threshold
  and button detection are kept.
- `stock.bin` — Huion's original factory firmware (application T223_220707),
  dumped from a retail H420X. Installing it puts the tablet back exactly as it
  came.

Hardware evidence for the release set (2026-09-30 bench sessions):

| File | Measured result |
|---|---|
| `rhythm900.bin` | 910–915/s, 0 invalid, steady jitter 2.7/3.4, 0% fast-aim crossing leaps |
| `rhythm750.bin` | 766/s in every trial, 0 invalid, 0% crossing leaps, back-steps ≤ 0.54% |
| `pen500.bin` | Module-only rebuild of the hardware-clean pen500 (500.29/s, no crossing leaps). Changed bytes vs the tested image are only inside the tracker cave (learner guards, `LCL3` tag, u16 log table); same code as the tested rhythm tiers |
| `pen333.bin` | Module-only rebuild of pen333 (same cave-only difference) |
| `stock.bin` | Byte-identical to the first original application dump, region `[0x4000, 0xBD00)` |

The pen tiers were deliberately not re-flashed after the cave-only rebuild (user
decision); the changed code is the same tracker already hardware-tested in the
rhythm tiers. Emulator check (`verify_rhythm3.py`) is `PASS_offline_only` on all
four customs — that proves control flow/coordinates, never analog settling or
USB rate.

Previous release (`rhythm960` + `pen600`/`pen300`) is archived in the dev
workspace under `release/archive/2026-09-29_rhythm960_pen600_pen300/` and is
superseded: `rhythm960` had coil-boundary notches, `pen600`/`pen300` had uneven
delivery and the old estimator's seams.

## What changed from Huion's stock firmware (short version)

Common to all four custom firmwares:

- Built-in cursor smoothing is gone. Stock blends recent positions together,
  which adds delay. These report each scan as-is.
- It checks fewer wires per scan. The tablet finds the pen by checking a grid
  of wire loops under the surface. Stock checks about 7 in each direction every
  time; these check the 3 nearest the pen, plus 1 warm-up read.
- A new way to turn those 3 readings into a position (fitting the curve of the
  signal instead of a simple ratio). The old formula overshot and then snapped
  back whenever the pen crossed from one wire to the next, which showed up as
  small V/Z notches in fast movement.
- Crossing between wires never costs an extra scan: the firmware reports from
  the wires it just read and moves to the new wires for the next scan.
- A small self-calibration learns the tiny gain differences between the 3
  reads, only from steady pen movement, and starts fresh after every install.

`rhythm900` and `rhythm750` (cursor only):

- Pressure and pen buttons are off. The tablet no longer measures how hard you
  press or reads button clicks. Use keyboard keys for hits.
- Pen-tuning checks happen less often. The tablet regularly re-checks the pen's
  exact signal frequency; these do it every 32 scans instead of every few.
- The fixed waits around each wire read are shorter than stock (about 120
  microseconds per read in stock): about 41 in `rhythm900`, about 64 in
  `rhythm750` and the pen firmwares. `rhythm900` keeps the stock 29 pulses that
  energize the pen: cutting them to 25 (in the old `rhythm960`) made
  neighbouring reads disagree and caused the notches.
- `rhythm750` uses the longer waits: slower, and the cleanest.

`pen500` and `pen333` (pressure and pen buttons on):

- The same tracking as the rhythm firmwares, with the longer waits of
  `rhythm750`.
- Each report is sent right after a USB frame starts, exactly every 2 ms
  (`pen500`) or 3 ms (`pen333`), so reports arrive perfectly evenly instead of
  in an uneven pattern.
- Pressure is read fresh every scan as a straight line from the pen's
  frequency: Huion's 14-sample pressure average, its smoothing filter and its
  pressure curve are skipped. The tip threshold and button detection are
  Huion's.

Removed since the previous release: `rhythm960` (the notches), `pen600` and
`pen300` (uneven report spacing, and the old position formula's seams).

## 1. Stock baseline

- Tablet: Huion H420X, controller marked HV901, treated as GD32F350-class.
  The GD32 family identity is inferred from code behavior, not board probing.
- Stock app: `T223_220707`, exactly 32,000 bytes at `0x08004000..0x0800BD00`.
  Vector table: stack `0x20000EF0`, reset `0x08004165`.
- Kept regions (never written by our flasher): bootloader at `0x08000000`
  (16,384 bytes, SHA-256
  `7C1763281D039D8DB341B1E2E66D3B08F843CBAE4B53BA6B43FA7F3F6AAEB188`)
  and the settings page at `0x0800FC00`, plus the 768-byte tail at
  `0x0800BD00` which must read all-`0xFF`.
- Stock behavior we measured: ~388 USB packets/s but only ~187
  changed-coordinate updates/s (over half the packets repeat X/Y), because stock
  blends recent positions (temporal smoothing) and can emit a stale report
  around one sensor scan. USB endpoint `0x81` is full-speed interrupt with
  `bInterval=1` ms, so 1,000 packets/s is the practical ceiling either way.
- Stock sensing per position frame: ~7 wire-loop reads per axis, 29 excitation
  pulses per read (~53–64 µs of pulse time), ~120 µs of fixed waits per read,
  3 ADC conversions with median-of-three, periodic pen-frequency
  (resonance) fitting, 14-sample pressure averaging + pressure curve + IIR
  smoothing, and a ratio-based position estimator.

## 2. How the firmware was created

No source code from Huion was used. All customs are **patched stock images**,
not rewrites:

1. **Reverse engineering.** Ghidra disassembly + capstone emulation of the
   stock app and the `adcsmp` timing parent. Documented: sweep entry points
   (X `0x08006354`, Y `0x08006490`), envelope helpers (X `0x0800B69C`,
   Y `0x0800B728`, resonance `0x0800B610`), excitation routine `0x08004CF8`
   (13 bin-specific routines, 29 pulses, PA15 burst + PA3 driver enable),
   ADC paths, SysTick microsecond delays (`LOAD = us·72` at 72 MHz),
   coordinator `0x08004C28`, maintenance `0x08006780` (one detuned resonance
   probe per event, fit every 5 probes), finalizer `0x08006644`, report path
   `0x08004EFC`. Pin roles (PA15 burst, PA3 enable, PA2 integrator
   clear/sample/set, PA4 envelope, PA5–7 mux address, PB mux enables) are
   inferred from code, not probed.
2. **Module cave.** All new tracker code lives in the retired interval
   `0x080073F8..0x08007A18` (1,568 bytes). The coordinator's call to the old
   stock follow code is NOP'd and redirected to our module. Nothing outside
   the app interval is touched; protected/untouched intervals are asserted at
   build time.
3. **Builders.** `development/sensing/build_follow5.py` starts from the pinned
   parent `firmware/H420X_T223_220707_app_700_adcsmp_stage.bin`
   (`3d1a7efb…b4736d47c0`), compiles the C tracker (`rhythm3_tracker.c` /
   `follow5_tracker.c`, ARM `arm-none-eabi` toolchain) into the cave, and
   applies one mechanism per candidate. One-mechanism byte patches
   (`build_patchset.py`: `setup58`, `pulses25`, `gpiofast`, `tail1`, …) are
   hash-pinned: parent SHA, expected old/new bytes, decoded mnemonic, and an
   exact changed-byte-set check. Every candidate goes in a new
   `development/candidates/<name>/` folder with `build.json` (parent SHA,
   patch ranges, compiler flags), ELF/map/disassembly, and `validation.json`.
   Existing folders are never overwritten.
4. **Offline verification.** `verify_rhythm3.py` / `verify_follow5.py` execute
   the **compiled image** under Unicorn with mocked ADC/delays: exact physical
   read order, discarded preparation amplitude, six fresh used reads, recenter
   identity, weak/retry loss, edges, first re-entry, report modes,
   pressure/button clearing. Result is always labeled `PASS_offline_only`.
5. **Hardware trials.** Direct USB captures (`pen_capture.py`, MI_00 raw
   packets) with matched reference/candidate conditions: center + boundary
   holds, slow crossings both directions, circles, diagonals, fast
   stop/reversal, hover threshold, edges/corners, lift/re-entry. Metrics:
   whole-span and valid-span rate, invalid/release counts, gaps >5/>20 ms,
   interval percentiles, duplicate-XY %, stationary RMS after drift removal,
   boundary-jump renders, subjective game feel. Analysis scripts
   (`analyze_rhythm_sessions.py`, `analyze_chain_history.py`,
   `compare_motion.py`) score density RMS and fast-aim crossing leaps
   (>80 units past expected step). Rejected ideas are kept as evidence
   (`archive/firmware_rejected_2026-09-25/`, dated captures).
6. **Iteration path (abridged).** Raw baselines → `fidelity` (~400 Hz) →
   `stable_timing` (~500 Hz, smooth reference) → resonance-cadence cuts
   (~540–560 Hz) → setup/tail/ADC trims (~600–640 Hz `adcsmp`) → rejected
   sparse/B-path schedulers (fast but jittery) → fresh-five `follow5_selective`
   (~633 Hz, full-pen rollback) → cursor-only `rhythm3_prime` (~806 Hz) →
   res16/setup40/hysteresis/tail1/probe-quiet lineage (~880–985 Hz
   experiments, several rejected for boundary flapping, probe dip, or poll
   beating) → rc4 `rhythm960` (~960 Hz) → log-fit + deferred handoff + hardened
   learner + stock-29-pulses → final `rhythm900`/`rhythm750`/`pen500`/`pen333`.

Rejected on the way (kept as evidence, not shipped): 96 MHz clock (failed USB
enumeration + retimes all CPU-timed excitation/delays), single ADC conversion
(broke cursor), frozen resonance (broke tip/contact), 3-coil/naive
reduced-window scans (jumps/estimator mismatch), pulses25 on the fast chain
(neighbor-read mismatch → notches), SOF-unpaced uneven delivery.

## 3. What changed vs stock

Common to all four customs:

- **No cursor smoothing.** Stock's temporal XY blending is gone. Every report
  comes only from that scan's fresh coil reads: no averaging, no added
  latency. (`pen300`'s old oversample experiment was the only averaging build
  and is not shipped.)
- **Fewer wires per scan.** Stock checks ~7 loops per axis; these check the 3
  nearest the pen plus 1 warm-up (preparation) read per axis — 8 physical
  envelope reads per ordinary interior frame, 6 of which are used for position.
- **New position formula.** Log-domain fit with per-axis cubic linearization
  instead of the stock simple ratio. The old formula overshot then snapped back
  at wire crossings (V/Z notches in fast movement).
- **Deferred handoff.** Crossing wires never costs an extra scan: the firmware
  reports from the wires it just read and moves to the new wires next scan. No
  rescan inside the 25% band; accepted |jump| < p/4 is clamped to ±p/16.
- **Self-calibration (learner).** Learns tiny per-slot gain differences from
  steady motion only (steps in p/32..p/8 per frame), gain 1/16, limit ±8192.
  Fresh tag `LCL3` (`0x334C434C`): stale SRAM calibration from any earlier
  image is discarded after DFU (SRAM survives flashing — the bootloader jumps
  straight into the new image). Converges after a few seconds of moving the pen
  around post-install; starts at zero every power-up/install.
- **Pen-frequency checks less often.** Resonance probe every 32 scans
  (`res32`), placed mid-frame, plus 300 µs quiet delay after each probe so the
  next frame's X read isn't disturbed.
- **Shorter fixed waits.** Stock ~120 µs per read → ~41 µs (`rhythm900`),
  ~64 µs (`rhythm750`, both pen tiers). `rhythm900` keeps stock's 29
  energizing pulses; cutting to 25 was tested and rejected (neighbor reads
  disagreed → notches).

`rhythm900` / `rhythm750` (cursor only):

- Pressure, tip, and pen buttons are OFF (no pen-frequency pressure reads, no
  button logic). Use keyboard keys for hits.
- Free-running delivery (not USB-paced): beats against the 1 ms USB poll, so
  occasional 2 ms gaps appear. `rhythm750`'s longer waits make it the cleanest
  high-rate option.

`pen500` / `pen333` (pressure + buttons on):

- Same tracker and longer waits as `rhythm750`.
- Reports handed over right after a USB frame starts: exactly every 2 ms
  (`pen500`) or 3 ms (`pen333`) — perfectly even delivery.
- Pressure read fresh every scan as a straight line from pen frequency: Huion's
  14-sample average, smoothing filter, and pressure curve are skipped. Tip
  threshold and button detection are Huion's.

## 4. How flashing works (`flash.bat` → `flash.ps1`)

Design: plain-text scripts, no admin rights, nothing installed, bootloader and
settings never written, every byte verified.

1. **Pick and check the file.** `flash.ps1` hashes the chosen `.bin` against
   `SHA256SUMS.txt`, checks exactly 32,000 bytes and a valid H420X vector
   table (stack sane, reset vector inside the app area). Unknown files need an
   explicit `YES`; backups in `backups\` are restorable without the override.
2. **Use the bundled USB library.** USB access uses GigaDevice's own 32-bit
   `GD_DFU_DLL.dll` (tested v5.4.0.42035). The script checks local copies
   first — next to the script, then `gigadevice\` — and only then falls back
   to the remembered path, the usual download folders, a still-packed
   All-In-One download, or asking you to browse (re-download links are in the
   `gigadevice` section near the top of this file). The chosen DLL's hash is
   compared against the tested version; a different version needs an explicit
   `YES`. (`vcruntime140.dll` sits beside it because the DLL needs it, and the
   script loads the DLL from its exact folder. The DLL is 32-bit, which is why
   `flash.bat` starts the 32-bit PowerShell.)
3. **Catch the bootloader.** Replug the tablet when asked; the script waits up
   to 120 s for USB `28E9:0189`. Refuses: more than one tablet in update mode,
   device held by another program, bootloader never seen (replug advice), or
   seen-but-driverless (install the signed "GD32 Dfu Drivers" once —
   `GDDFUDriver.sys`, WHQL-signed; PCs that ran Huion's updater may have it).
4. **CHECK the tablet is the right one (read-only, nothing written).** Reads
   the 16,384-byte bootloader and requires the exact H420X/T223 hash; requires
   the 768-byte page tail to be all `0xFF`; reads the 1,024-byte settings page
   (its 12-hex-digit fingerprint is shown); reads the installed app and
   reports what it is (known firmware name or `unknown` with its hash).
   Anything else → stop, nothing changed. A wrong tablet (different
   bootloader) is blocked here, before any backup or write — the same gate
   the GUI flasher enforces.
5. **Backup.** Reads the current 32,000-byte app **twice**, requires both reads
   identical, saves to `backups\backup_<date>_<label>_<hash8>.bin` (label is
   the known firmware name or `unknown`). Verifies the saved file's hash.
   `S` in the menu does backup-only (changes nothing). If the installed app
   is damaged (bad vector table), the check says so explicitly: only a backup
   restore (`R`) or `stock.bin` may then be installed over it; other firmwares
   are blocked until the tablet is recovered.
6. **Re-verify, then erase → write → verify.** Immediately before erasing, the
   script re-reads the bootloader and the app and requires both to match what
   was just checked and backed up — proving the same tablet is still connected
   and undisturbed. Then it erases only the 32 pages of
   `[0x08004000, +32000)`, writes in 1 KB blocks (DFU set-address `0x21` /
   erase `0x41` / block-2 transfers), reads the whole app back and compares
   **every byte**, then re-hashes the bootloader to prove it didn't change.
   On failure mid-install: nothing is bricked (bootloader intact) — run
   `flash.bat` again and install anything, or `R` to restore a backup.
7. **Replug to boot.** Unplug/plug once more to leave DFU and run the new app;
   move the pen for a few seconds so the learner converges. To go back:
   `flash.bat` → install `stock.bin` or `R` to restore a backup.

Safety properties: only interval ever written is the app area; in-memory image
hash checked before erase; independent post-write readback; bootloader/config/
tail compared against pre-flash data; failure diagnosis via Windows'
`HKLM\...\Enum\USB\VID_28E9&PID_0189` record.

Note: the dev workspace also contains a GUI `KombiniH420xFlasher.exe`
(`distribution/`). It bundles an **older** profile set (400/600/800-class
lineage) and is not the installer for these finals — use `flash.bat` here.

## 5. Reproducing / auditing

- Final images are byte-identical to dev candidates (see
  `development/RELEASE_2026-09-30.md` for the candidate names and bench
  session IDs, `development/FINAL_TIERS_2026-09-30.md` for the tracker/analog
  matrix, `development/RE_AUDIT_2026-09-29.md` for the per-read acquisition
  audit, `development/HIGH_RATE_NO_READ_LOSS_PLAN_2026-09-28.md` for the full
  experiment log).
- Rebuild path: `python development/sensing/build_follow5.py --help`,
  one-mechanism patches via `build_patchset.py`, verify with
  `verify_rhythm3.py` / `verify_follow5.py`. Rebuilding into an existing
  candidate name overwrites provenance — always use a fresh `--output-name`.
- App image constraint for any rebuild: exactly 32,000 bytes at
  `0x08004000..0x0800BD00`, module cave `0x080073F8..0x08007A18`, SRAM
  calibration tag `LCL3`. Emulator `PASS` is offline-only by definition.
