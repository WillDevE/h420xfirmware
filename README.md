# H420X (T223) clean-room firmware

A from-scratch replacement firmware for the Huion H420X pen tablet, made for
low-latency rhythm-game aim. It is **not** a patched Huion image and contains no
Huion application code. The stock 16 KB bootloader and its DFU recovery path are
kept, so the tablet can always be recovered.

[Flash it](#flash-it) · [What it is](#what-it-is) · [How it is made](#how-it-is-made) ·
[How it was tested](#how-it-was-tested-the-boring-part) · [Limits and TODO](#limits-and-todo) ·
[Source and license](#source-and-license)

## Flash it

### 1. Get the image (one file)

The firmware image is **not stored in this repository**. Every release image is
built by GitHub Actions from the source in `firmware/` and attached to the
[latest release](https://github.com/WillDevE/h420xfirmware/releases/latest).

- **Direct download:** [`rhythm1000.bin`](https://github.com/WillDevE/h420xfirmware/releases/latest/download/rhythm1000.bin)
  — put it in `firmware\`.
- **Or double-click `get-firmware.bat`**, which downloads it and checks its
  SHA-256 against `SHA256SUMS.txt`.

The release image's SHA-256 is:

```
00d11f9e682c7fdb7e52896db66b5170ae881a1fafe4e79638d10282ce262171
```

The flasher checks it again before writing.

### 2. Add GigaDevice's USB library (one time)

The flasher talks to the tablet through GigaDevice's 32-bit USB library. It is
not included. Download GigaDevice's free **GD32 All-In-One Programmer**:

https://www.gd32mcu.com/en/download?kw=GD32+All-In-One+Programmer

Copy these two files next to each other into `gigadevice\`:

- `GD_DFU_DLL.dll` (tested v5.4.0.42035)
- `vcruntime140.dll`

If Windows asks for a driver while the tablet is in update mode, install
GigaDevice's **GD32 Dfu Drivers** once:

https://www.gd32mcu.com/en/download?kw=GD32+Dfu+Drivers

`flash.bat` checks `GD_DFU_DLL.dll` against the tested hash before using it.

### 3. Flash

1. Double-click `flash.bat`.
2. Choose `1` for `rhythm1000.bin`.
3. Unplug the USB cable and plug it back in when told.
4. Wait for `DONE`, then unplug and plug once more.
5. To undo, run `flash.bat`, choose `R`, and pick a backup.

Every install first reads the current application twice and saves it in
`backups\`. The flasher then erases and writes **only** the 32,000-byte
application area at `0x08004000`, and reads it back byte for byte. It never
writes the bootloader (`0x08000000`) or the settings page (`0x0800FC00`).

## What it is

A cursor-only 1 kHz-class build for rhythm games:

- **Raw, same-frame position.** Every report is the fit from that frame's own
  coil readings. There is no output filter, smoothing, averaging across frames,
  prediction or added delay.
- **Seven reads per frame** (four X, three Y), 32 excitation pulses, a long ADC
  sample window, and a fixed cadence with a 1002 µs frame floor. The device
  frame is 998 Hz; OpenTabletDriver sees about 1 kHz. The bench host reports
  ~979 Hz because the host accepts one report every 2 ms for 11 frames every
  ~525 reports, not because the tablet is slow.
- **Stock USB identity.** VID/PID `256C:0064`, manufacturer `HUION`, product
  `Huion Tablet_H420X`, report ID 8. OpenTabletDriver and WebHID treat it as a
  normal H420X.
- **Late connect and quiet idle.** USB connects after sensing/ADC init, and the
  old 10 ms heartbeat is gone, so there is no ~78 Hz idle stream.
- **Application only.** The stock bootloader and recovery path are untouched.
- **Cursor only.** No pressure, tip switch or pen buttons in this image. A
  lower-rate pressure build is the next TODO.

## How it is made

The firmware is clean-room C: about 5,700 lines in `firmware/src/`, plus the
vendored TinyUSB device stack (MIT) in `firmware/third_party/tinyusb/`. It is
built with `arm-none-eabi-gcc` and a linker script that places everything in the
32,000-byte application slot at `0x08004000`. There is no make/cmake dependency:
`firmware/build.ps1` compiles and links it directly.

The exact build for the release image:

```powershell
powershell -ExecutionPolicy Bypass -File firmware/build.ps1 `
  -Profile rhythm900 -Robust -EdgeExt `
  -SamptModes -LoopMode 3 -LoopArm -Release -CadReset 256 `
  -StockIdentity -SerialNumber 20210233 `
  -LateConnect -QuietHeartbeat
```

The compiler is pinned to **Arm GNU Toolchain 12.2.MPACBTI-Rel1**. Different
compiler versions can generate different code, so the pin is part of the
reproducibility promise.

### Built by GitHub Actions, not uploaded from a desktop

There is no firmware binary in the repository. The workflow in
`.github/workflows/build.yml` runs on every push:

1. downloads the pinned Arm toolchain from Arm and checks its SHA-256,
2. builds the image from the source in this repository,
3. runs `firmware/tools/verify_image.py` — 96 independent checks of the ELF,
   USB descriptors, HID report descriptor, vector table and DFU padding,
4. asserts the finished image's SHA-256 is exactly
   `00d11f9e682c7fdb7e52896db66b5170ae881a1fafe4e79638d10282ce262171`,
   the image that was tested on hardware,
5. uploads the image as a workflow artifact, and, on a version tag, attaches it
   to the GitHub release.

If any step fails, there is no release. The binary you download is the build the
public workflow produced from the public source, and you can reproduce it
yourself with the command above. That is the point of the exercise.

### Verify it yourself

```powershell
certutil -hashfile firmware\rhythm1000.bin SHA256
```

The hash must match `SHA256SUMS.txt` and the release page. The flasher checks it
before writing.

## How it was tested (the boring part)

This did not come from one clever idea. It took **38 firmware rounds**, **117
candidate build folders**, and **228 distinct images built** over about **five
weeks**. In that time the bench recorded **113 sessions**, **290 trials**, and
**4.75 million pen reports**. About **115 images were flashed** to the tablet;
**97** distinct images actually ran on hardware. The clean-room application is
5,700 lines of C.

The bench measures the pen directly over USB: nine-point steady maps (center
plus the eight axis midpoints), slow circles in both directions, paced fast-aim
reversals, and lab hover captures. The numbers are raw position scatter in
tablet units and millimetres, with no output filtering, so every improvement had
to come from the sensing, estimator or timing. Early clean-room still-pen
captures were around 0.1 mm; the final nine-point median is **0.035 mm** X and
**0.010 mm** Y, with the center at 0.021 / 0.008 mm and the moving circle at
0.013 / 0.007 mm.

The last rounds were mostly subtracting the last few units: ADC sample window,
cadence reset, stock USB identity, late connect, quiet idle, and a lot of
rejected ideas. The current image is the one that survived.

## Limits and TODO

- **Cursor only.** No pressure, tip switch or pen buttons. The next build is a
  lower-rate pen image (target 500/333 Hz) using the clean-room pen path with
  the same ADC window and cadence reset.
- **Host rate.** The device frame is 1002 µs (998 Hz). The host-side 2 ms burst
  is a host reader artifact; OpenTabletDriver itself reaches ~1 kHz.
- **Bootloader.** Only the stock H420X/T223 bootloader is supported. The flasher
  refuses anything else.
- **Not a Huion product.** Huion and H420X are trademarks of their respective
  owner. The VID/PID is retained only so existing host configurations recognize
  the tablet.

## Source and license

- Clean-room firmware: **GPL-3.0-or-later**, see `LICENSE`.
- TinyUSB: **MIT**, see `firmware/third_party/tinyusb/LICENSE`.
- No Huion application code or firmware dump is included.
- Detailed build notes: `firmware/README.md`. Provenance: `SOURCE.md`.
