# H420X (T223) clean-room firmware

A from-scratch replacement firmware for the Huion H420X pen tablet, built for
low-latency rhythm-game (ozu) aim. It's not a patched Huion image and doesn't contain
any Huion application code. The stock 16 KB bootloader and its DFU recovery path
are left alone, so you can always recover the tablet (pls dont flash over it manually or its joever).

[Flash it](#flash-it) · [What it is](#what-it-is) · [How it is made](#how-it-is-made) ·
[How it was tested](#how-it-was-tested-the-boring-part) · [Limits and TODO](#limits-and-todo) ·
[Source and license](#source-and-license)

## Flash it

### 1. Get the flasher package

The normal download is a single zip with the flasher and the firmware already
inside. It's built by GitHub Actions from the source in `firmware/` and attached
to the [latest release](https://github.com/WillDevE/h420xfirmware/releases/latest).

(why did I do it this way? tl;dr so you dont download a file you can't easily read/verify the source of, basically I can't hide evil stuff in it)

- **Download:** [`H420X-Flasher.zip`](https://github.com/WillDevE/h420xfirmware/releases/latest/download/H420X-Flasher.zip)
  — extract it anywhere.
- **Prefer the raw image?** It's attached to the release as
  [`rhythm1000.bin`](https://github.com/WillDevE/h420xfirmware/releases/latest/download/rhythm1000.bin).

The zip contains `flash.bat`, `flash.ps1`, `firmware\rhythm1000.bin`, the
GigaDevice instructions, and a `SHA256SUMS.txt`. The firmware's SHA-256 is:

```
00d11f9e682c7fdb7e52896db66b5170ae881a1fafe4e79638d10282ce262171
```

The flasher checks it again before writing.

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
- **Seven reads per frame** (four X, three Y), 32 excitation pulses, a long ADC
  sample window, and a fixed cadence with a 1002 µs frame floor. The device
  frame is 998 Hz; OpenTabletDriver sees about 1 kHz. The bench host reports a
  bit lower because of a host-side 2 ms burst, not because the tablet is slow.
- **Stock USB identity.** VID/PID `256C:0064`, manufacturer `HUION`, product
  `Huion Tablet_H420X`, report ID 8. OpenTabletDriver and WebHID just see a
  normal H420X.
- **Late connect and quiet idle.** USB comes up after sensing/ADC init, and the
  old 10 ms heartbeat is gone, so there's no ~78 Hz idle stream.
- **Application only.** The stock bootloader and recovery path are untouched.
- **Cursor only.** No pressure, tip switch or pen buttons in this image.

## How it is made

The firmware is clean-room C — about 5,700 lines in `firmware/src/` — plus the
vendored TinyUSB device stack (MIT) in `firmware/third_party/tinyusb/`. It builds
with `arm-none-eabi-gcc` and a linker script that puts everything in the
32,000-byte application slot at `0x08004000`. No make/cmake needed:
`firmware/build.ps1` compiles and links it directly.

Here's the exact build for the release image:

```powershell
powershell -ExecutionPolicy Bypass -File firmware/build.ps1 `
  -Profile rhythm900 -Robust -EdgeExt `
  -SamptModes -LoopMode 3 -LoopArm -Release -CadReset 256 `
  -StockIdentity -SerialNumber 20210233 `
  -LateConnect -QuietHeartbeat
```

The compiler is pinned to **Arm GNU Toolchain 12.2.MPACBTI-Rel1**. Different
compiler versions can generate different code, so that pin is part of the
reproducibility promise.

### Built by GitHub Actions, not uploaded from a desktop
(this is the part that prevents me from hiding evil stuff in here)

There's no firmware binary in the repo. The workflow in
`.github/workflows/build.yml` runs on every push:

1. downloads the pinned Arm toolchain from Arm and checks its SHA-256,
2. builds the image from the source here,
3. runs `firmware/tools/verify_image.py` — 96 independent checks of the ELF,
   USB descriptors, HID report descriptor, vector table and DFU padding,
4. asserts the finished image's SHA-256 is exactly
   `00d11f9e682c7fdb7e52896db66b5170ae881a1fafe4e79638d10282ce262171`,
   the image that was tested on hardware,
5. assembles the user-facing `H420X-Flasher.zip` (flasher + built image +
   README/hashes),
6. uploads everything as workflow artifacts and, on a version tag, attaches the
   zip, the standalone image and their SHA-256 files to the GitHub release.

If any step fails, there's no release. The binary you download is the build the
public workflow produced from the public source, and you can reproduce it
yourself with the command above. That's the whole shabang.

### Verify it yourself

If you want to check the file yourself:

```powershell
certutil -hashfile firmware\rhythm1000.bin SHA256
```

The hash must match `SHA256SUMS.txt` and the release page. The flasher checks it
before writing.

## How it was tested (the boring part)

It took **38 firmware rounds**, **117
candidate build folders**, and **228 distinct images built** over about **five
weeks** (not really it was on and off). Along the way the bench recorded **113 sessions**, **290 trials**, and
**4.75 million pen reports**. About **115 images were flashed** to the tablet;
**97** distinct images actually ran on hardware. The clean-room app is 5,700
lines of C.

I had a personal test bench thing help me with trying to get it stable and collecting data then fed it back into the token burning AI loop and repeated

The last rounds were mostly about shaving off the last few units: ADC sample
window, cadence reset, stock USB identity, late connect, quiet idle, and a pile
of rejected ideas. The current image is the one that survived.

## Limits and TODO

What's not done yet:

- **Cursor only.** No pressure, tip switch or pen buttons. The next build is a
  lower-rate pen image (target maybe 800 or 500/333 Hz) using the clean-room pen path with
  the same ADC window and cadence reset.
- **Host rate.** The device frame is 1002 µs (998 Hz). The host-side 2 ms burst
  is a host reader artifact; OpenTabletDriver itself reaches ~1 kHz. (I don't understand what this means yet I need to read more, had a version that was pinned to 998hz but was way too unstable)
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
