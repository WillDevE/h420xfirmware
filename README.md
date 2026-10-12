# H420X (T223) custom firmware

Faster Huion H420X tablet firmware for rhythm games, plus a `.bat` flasher
that installs it safely. This folder holds the firmwares, the flasher, and
this guide. Two small USB files are NOT included — grab them once below.

## 1. Download this first (one time)

The flasher talks to the tablet through GigaDevice's USB library. Create a
`gigadevice` folder next to `flash.bat` and put both files in it — no install
step, just the two files side by side:

- `GD_DFU_DLL.dll` (32-bit, tested v5.4.0.42035) and `vcruntime140.dll` —
  GigaDevice's free "GD32 All-In-One Programmer":
  https://www.gd32mcu.com/en/download?kw=GD32+All-In-One+Programmer
- `vcruntime140.dll` alone, if you'd rather skip the GigaDevice download —
  Microsoft's free Visual C++ Redistributable (x86):
  https://aka.ms/vs/17/release/vc_redist.x86.exe

Shortcut: run `flash.bat` without the files and it offers `D` to open the
GigaDevice page or `B` to point at a file you already downloaded (even still
zipped) — it copies the two files into `gigadevice\` itself.

Check they match:

```powershell
certutil -hashfile gigadevice\GD_DFU_DLL.dll SHA256
certutil -hashfile gigadevice\vcruntime140.dll SHA256
```

against the table under Contents (`SHA256SUMS.txt` has them too). The flasher
re-checks the DLL hash itself and asks before using any other version.

Driver, only if asked: if the flasher says Windows has no driver for update
mode, install "GD32 Dfu Drivers" once:
https://www.gd32mcu.com/en/download?kw=GD32+Dfu+Drivers
(Signed by Microsoft; PCs that ran Huion's updater usually have it already.)

## 2. Flash

1. Double-click `flash.bat`. First time, choose `S` to back up the tablet's
   current firmware (changes nothing), then run it again.
2. Type a firmware number, Enter.
3. Unplug the USB cable and plug it back in when told.
4. Wait for DONE, unplug/plug once more. Move the pen around a few seconds so
   the firmware settles in.
5. To undo: run `flash.bat`, choose `R` to restore a backup, or install
   `stock.bin`. The bootloader is never touched, so just run it again if
   anything goes wrong.

Every install saves a backup first
(`backups\backup_<date>_<name>_<hash>.bin`). Before writing, the flasher
verifies the tablet is a real H420X/T223 (exact bootloader match) and reports
what it currently runs.

## 3. Which firmware

| File | What it is | Tested |
|---|---|---|
| `rhythm900.bin` | ~910/s, cursor only — no pressure/tip/buttons, use keys | 910–915/s, 0 drops, no crossing jumps |
| `rhythm750.bin` | ~765/s, cursor only, the cleanest fast option | 766/s, 0 drops, no crossing jumps |
| `pen500.bin` | Exactly 500/s, pressure + tip + buttons, even spacing | 500.29/s, no crossing jumps |
| `pen333.bin` | Exactly 333/s, same with more frequent pen checks | shares pen500's tested tracker |
| `stock.bin` | Huion factory firmware — back to exactly as it came | byte-identical original dump |

Cursor-only builds are for rhythm games. Pen builds read pressure fresh every
scan (no averaging or curve); tip threshold and buttons are Huion's. The
previous set (`rhythm960`, `pen600`/`pen300`) is superseded — notches and
uneven delivery — and not shipped.

## Contents

```text
h420xfirmware/
  firmware/       the 5 files above
  gigadevice/     YOU add this: GD_DFU_DLL.dll + vcruntime140.dll (see §1)
  flash.bat       launcher (runs flash.ps1 in 32-bit PowerShell)
  flash.ps1       the installer — plain text, read it in Notepad
  SHA256SUMS.txt  hashes of everything, including the 2 downloads
  README.md       this file
```

`flash.ps1` only talks to update mode (USB `28E9:0189`), backs up first,
writes only the 32,000-byte app at `0x08004000`, verifies every byte. No
admin rights, nothing installed. Run files next to `flash.bat`: `backups\`
(your firmware), `gigadevice_dll_location.txt` (which DLL copy was used).

Full hashes (`SHA256SUMS.txt` has them too):

| File | SHA-256 |
|---|---|
| `firmware\rhythm900.bin` | `860b3645154c4b963a54356875bf5d9ec53b60ab3f3ad96aa636e4ac41929c7f` |
| `firmware\rhythm750.bin` | `842283679e7969b4a10ad1573a29dda661fa23cf751c5b32fba91d3c9d343d76` |
| `firmware\pen500.bin` | `a1821663297217b1ba4bb7f20ed7f1d738988a8aedd58a3c9ef589f6d32c4e02` |
| `firmware\pen333.bin` | `ef57e32b05f1eea2234a457b61c55595e69df73623dc255bc05248797fa405ad` |
| `firmware\stock.bin` | `aef9a6df0806fa9ce9a182c6c2974e26d36a4707b9cd752062d76c638cd8b630` |
| `gigadevice\GD_DFU_DLL.dll` | `c97d2a6a94747d2c7532bb98ed447008319c3d74e87d96cdf018b54602a5ad14` |
| `gigadevice\vcruntime140.dll` | `9faeaa45e8cc986af56f28350b38238b03c01c355e9564b849604b8d690919c5` |

---

## Details — how things came to be

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
2. **Provide the USB library (one-time download, not included).** USB access
   uses GigaDevice's own 32-bit `GD_DFU_DLL.dll` (tested v5.4.0.42035), which
   you download once into `gigadevice\` (links above). The script checks local
   copies
   first — next to the script, then `gigadevice\` — and only then falls back
   to the remembered path, the usual download folders, a still-packed
   All-In-One download, or asking you to browse (links in §1 above). The chosen DLL's hash is
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
