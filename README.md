# H420X (T223) clean-room firmware

A from-scratch replacement firmware for the Huion H420X pen tablet, built for
low-latency rhythm-game (ozu) aim. It's not a patched Huion image and doesn't contain
any Huion application code. The stock 16 KB bootloader and its DFU recovery path
are left alone, so you can always recover the tablet (pls dont flash over it manually or its joever).

This is the **final 1000 Hz build**: one cursor-only image, one download. Older
images are kept as [legacy](#legacy-images).

[Flash it](#flash-it) · [What it is](#what-is-all-of-this) · [How it is made](#how-it-is-made) ·
[How it was tested](#how-it-was-tested-the-boring-part) · [Legacy images](#legacy-images) ·
[Limits and TODO](#limits-and-todo) · [Source and license](#source-and-license)

## Flash it

### 1. Get the flasher package

The download is a single zip with the flasher and the firmware image already
inside. It's built by GitHub Actions from the source in `firmware/` and attached
to the [latest release](https://github.com/WillDevE/h420xfirmware/releases/latest).

(why did I do it this way? tl;dr so you dont download a file you can't easily read/verify the source of, basically I can't hide evil stuff in it)

- **Download:** [`H420X-Flasher.zip`](https://github.com/WillDevE/h420xfirmware/releases/latest/download/H420X-Flasher.zip)
  — extract it anywhere.

The zip contains `flash.bat`, `flash.ps1`, `firmware\rhythm1000.bin`, the
GigaDevice instructions, and a `SHA256SUMS.txt`. The image hash is:

```
08bbedc446596fbbc65d4f9fe7b89f9d5ab80a8d843d6eea8044f655a10044e1  rhythm1000.bin
```

The flasher checks it again before writing.

### What you get

| | `rhythm1000.bin` |
|---|---|
| Bench report rate | 999.9 Hz |
| Dropped reports | none in about 214,000 reports |
| Report gaps over 1.5 ms | none in about 214,000 reports |
| Steady jitter, 9-point RMS | X 1.82 / Y 2.28 units (1 unit is about 0.005 mm) |
| Pressure / tip / buttons | none |

The image is raw and same-frame, with no smoothing or added delay. Jitter
numbers are from the 9-point bench sessions and vary a little with pen position
and session.

### 2. Add GigaDevice's USB library (one time)

The flasher talks to the tablet through GigaDevice's 32-bit USB library, which
isn't included. Download GigaDevice's free **GD32 All-In-One Programmer**:

https://www.gd32mcu.com/en/download?kw=GD32+All-In-One+Programmer

Inside the extracted `H420X-Flasher` folder, copy these two files next to each
other into `gigadevice\`:

- `GD_DFU_DLL.dll` (tested v5.4.0.42035)
- `vcruntime140.dll`

If Windows asks for a driver while the tablet is in update mode, install
GigaDevice's **GD32 Dfu Drivers** once:

https://www.gd32mcu.com/en/download?kw=GD32+Dfu+Drivers

`flash.bat` checks `GD_DFU_DLL.dll` against the tested hash before using it.

### 3. Back up first, then flash

From the extracted `H420X-Flasher` folder, run `flash.bat` and choose `S`. It
reads the firmware that's on the tablet now (most likely the stock Huion
firmware) twice and saves it in `backups\`. It doesn't write anything. Keep that
file — it's your way back to stock.

Then:

1. Run `flash.bat` again and choose `1` for `rhythm1000.bin`.
2. Unplug the USB cable and plug it back in when told.
3. Wait for `DONE`, then unplug and plug once more.
4. To undo, run `flash.bat`, choose `R`, and pick a backup.

Every install also saves a backup first, so you get a fresh restore point each
time. The flasher then erases and writes **only** the 32,000-byte application
area at `0x08004000`, and reads it back byte for byte. It never writes the
bootloader (`0x08000000`) or the settings page (`0x0800FC00`).

## What is all of this

This is firmware for the h420x focused on rhythm games. The short version:

- **Raw, same-frame position.** Every report is the fit from that frame's own
  coil readings. No output filter, no smoothing, no averaging across frames, no
  prediction, no added delay. What the tablet saw is what you get.
- **True 1000 Hz, locked to USB.** Each frame starts when the host picks up the
  previous report, so the tablet stays in step with the 1 ms poll instead of
  drifting against it. The next report goes straight to the USB hardware, which
  is what got rid of the dropped reports.
- **Seven reads per frame**: one Y prep read that's thrown away but keeps the
  pen charged up, then three Y and three X, each with 28 excitation pulses and
  a long ADC sample window. Every read lands on an exact CPU cycle.
- **Stock USB identity.** VID/PID `256C:0064`, manufacturer `HUION`, product
  `Huion Tablet_H420X`, report ID 8. OpenTabletDriver and WebHID just see a
  normal H420X.
- **Late connect and quiet idle.** USB comes up after sensing/ADC init, and the
  old 10 ms heartbeat is gone, so there's no ~78 Hz idle stream.
- **Application only.** The stock bootloader and recovery path are untouched.
- **Cursor only.** No pressure, tip switch or pen buttons.

## How it is made

The firmware is clean-room C — about 7,000 lines in `firmware/src/` — plus the
vendored TinyUSB device stack (MIT) in `firmware/third_party/tinyusb/`. It builds
with `arm-none-eabi-gcc` and a linker script that puts everything in the
32,000-byte application slot at `0x08004000`. No make/cmake needed:
`firmware/build.ps1` compiles and links the tree directly.

Here's the exact build:

```powershell
powershell -ExecutionPolicy Bypass -File firmware/build.ps1 `
  -Clean -Profile rhythm900 -Robust -EdgeExt `
  -Scan1kModes -AxisGapModes -LoopMode 3 -LoopArm -Release `
  -CadReset 256 -ReadGap 50 `
  -StockIdentity -SerialNumber 20210233 `
  -LateConnect -QuietHeartbeat -FastEp1 -R59Modes -NoR55Aux
```

The compiler is pinned to **Arm GNU Toolchain 12.2.MPACBTI-Rel1**. Different
compiler versions can generate different code, so that pin is part of the
reproducibility promise.

### Built by GitHub Actions, not uploaded from a desktop
(this is the part that prevents me from hiding evil stuff in here)

There's no firmware binary in the repo. The workflow in
`.github/workflows/build.yml` runs on every push:

1. downloads the pinned Arm toolchain from Arm and checks its SHA-256,
2. builds the image from `firmware/`,
3. runs the 96-check verifier (ELF, USB descriptors, HID report descriptor,
   vector table and DFU padding),
4. asserts the image's SHA-256 is exactly
   `08bbedc446596fbbc65d4f9fe7b89f9d5ab80a8d843d6eea8044f655a10044e1`,
   the one tested on hardware,
5. assembles the user-facing `H420X-Flasher.zip` (flasher + image +
   README/hashes),
6. uploads it as a workflow artifact and, on a version tag, attaches the zip to
   the GitHub release.

If any step fails, there's no release. The binary you download is the build the
public workflow produced from the public source, and you can reproduce it
yourself with the command above. That's the whole shabang.

### Verify it yourself

If you want to check the file yourself:

```powershell
certutil -hashfile firmware\rhythm1000.bin SHA256
```

The hash must match `SHA256SUMS.txt` and the release page. The flasher checks it
again before writing.

## How it was tested (the boring part)

It took **59 firmware rounds** and about **180 candidate build folders** over
about **six weeks** (not really it was on and off). Along the way the bench
recorded **174 sessions**, **634 trials**, and **12.9 million pen reports**. The
clean-room app is about 7,000 lines of C.

I had a personal test bench thing help me with trying to get it stable and collecting data then fed it back into the token burning AI loop and repeated

The last rounds were about getting a true 1000 Hz with no dropped reports, then
shaving the jitter down at the source: frames locked to the USB poll,
cycle-exact read timing, keeping the Y prep read steady, and a sweep of the
excitation pulse count that landed on 28.

The final check ran the image on a 9-point steady test and circles: 999.9 Hz, no
dropped reports and no report gaps over 1.5 ms in about 214,000 reports.

## Legacy images

These are older builds. They're kept for reference and aren't recommended. Each
one is still on its [release page](https://github.com/WillDevE/h420xfirmware/releases),
marked Legacy, and its source is in that release's tag.

| | Hash | Bench report rate | 2 ms report gaps |
|---|---|---|---|
| `rhythm1000.bin` (v1.0.x stable) | `00d11f9e…` | ~979 Hz | ~2.1 %, in short runs |
| `rhythm1015-wip.bin` (v1.0.2/v1.0.3 WIP) | `12cdd468…` | ~994 Hz | ~0.56 %, scattered |

## Limits and TODO

What's not done yet:

- **Cursor only.** No pressure, tip switch or pen buttons. The next build is a
  lower-rate pen image (target maybe 800 or 500/333 Hz) using the clean-room pen path.
- **Jitter isn't even across the tablet.** The bottom row has about twice the Y
  jitter of the top row, and the left column has a bit more X jitter than the
  right. The cause isn't known yet.
- **Coil boundaries.** While the pen moves fast, Y has a small extra wobble as it
  crosses a coil boundary, on the order of 0.02 mm.
- **Bootloader.** Only the stock H420X/T223 bootloader is supported. The flasher
  refuses anything else.
- **Not a Huion product.** Huion and H420X are trademarks of their respective
  owner. The VID/PID is retained only so existing host configurations recognize
  the tablet.

## Source and license

- Clean-room firmware: **GPL-3.0-or-later**, see `LICENSE`.
- Source: `firmware/`.
- TinyUSB: **MIT**, see `firmware/third_party/tinyusb/LICENSE`.
- No Huion application code or firmware dump is included.
- Detailed build notes: `firmware/README.md`. Provenance: `SOURCE.md`.
