# H420X (T223) clean-room firmware

A from-scratch replacement firmware for the Huion H420X pen tablet, built for
low-latency rhythm-game (ozu) aim. It's not a patched Huion image and doesn't contain
any Huion application code. The stock 16 KB bootloader and its DFU recovery path
are left alone, so you can always recover the tablet (pls dont flash over it manually or its joever).

The download contains two cursor-only images: the tested **stable** image and an
optional **WIP** image with a higher report rate and lower overall scatter. The
flasher labels both; the stable image is the default and the recommended one.

[Flash it](#flash-it) · [What it is](#what-it-is) · [How it is made](#how-it-is-made) ·
[How it was tested](#how-it-was-tested-the-boring-part) · [Limits and TODO](#limits-and-todo) ·
[Source and license](#source-and-license)

## Flash it

### 1. Get the flasher package

The normal download is a single zip with the flasher and both firmware images
already inside. It's built by GitHub Actions from the source in `firmware/`
(stable) and `firmware-wip/` (WIP), and attached to the
[latest release](https://github.com/WillDevE/h420xfirmware/releases/latest).

(why did I do it this way? tl;dr so you dont download a file you can't easily read/verify the source of, basically I can't hide evil stuff in it)

- **Download:** [`H420X-Flasher.zip`](https://github.com/WillDevE/h420xfirmware/releases/latest/download/H420X-Flasher.zip)
  — extract it anywhere.
- **Prefer the raw image?** Both are attached to the release:
  [`rhythm1000.bin`](https://github.com/WillDevE/h420xfirmware/releases/latest/download/rhythm1000.bin)
  (stable) and
  [`rhythm1015-wip.bin`](https://github.com/WillDevE/h420xfirmware/releases/latest/download/rhythm1015-wip.bin)
  (WIP).

The zip contains `flash.bat`, `flash.ps1`, `firmware\rhythm1000.bin`,
`firmware\rhythm1015-wip.bin`, the GigaDevice instructions, and a
`SHA256SUMS.txt`. The image hashes are:

```
00d11f9e682c7fdb7e52896db66b5170ae881a1fafe4e79638d10282ce262171  rhythm1000.bin
12cdd4689595dc94d68546fe74d279be9c84b1028b8e45afcaaa9fd8bead11a3  rhythm1015-wip.bin
```

The flasher checks each one again before writing.

### Which image should I use?

| | `rhythm1000.bin` | `rhythm1015-wip.bin` |
|---|---|---|
| Status | **Stable, recommended** | **WIP / experimental** |
| Bench report rate | ~979 Hz | ~994 Hz |
| Host 2 ms gaps | ~2.1 %, in short ~20 ms runs every ~0.5 s | ~0.56 %, scattered, no long runs |
| Residual scatter (9-point median) | X 3.2 / Y 1.6 units | X 1.6 / Y 2.1 units |
| Pressure / tip / buttons | none | none |

Both images are raw and same-frame, with no smoothing or added delay. The WIP
image runs the report cadence slightly ahead of the host poll (985 µs frame
floor) so a fresh report is always queued. That raises the rate and lowers X
scatter, but the remaining host-side 2 ms gaps show up as scattered drops, and
Y scatter is a little higher. It is not the default yet; switch back to image 1
if it feels worse.

Scatter numbers are from the 9-point bench sessions and vary a little with pen
position and session.

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

1. Run `flash.bat` again and choose `1` for the stable `rhythm1000.bin`, or
   `2` for the WIP `rhythm1015-wip.bin`.
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
- **Two cadence variants.** The stable image keeps the 1002 µs frame floor; the
  WIP image uses 985 µs. Both are raw and same-frame; the WIP just queues the
  next report earlier, so it reaches a higher host rate.
- **Late connect and quiet idle.** USB comes up after sensing/ADC init, and the
  old 10 ms heartbeat is gone, so there's no ~78 Hz idle stream.
- **Application only.** The stock bootloader and recovery path are untouched.
- **Cursor only.** No pressure, tip switch or pen buttons in either image.

## How it is made

The firmware is clean-room C — about 5,700 lines in `firmware/src/` — plus the
vendored TinyUSB device stack (MIT) in `firmware/third_party/tinyusb/`. It builds
with `arm-none-eabi-gcc` and a linker script that puts everything in the
32,000-byte application slot at `0x08004000`. No make/cmake needed:
`firmware/build.ps1` and `firmware-wip/build.ps1` compile and link each tree
directly.

Here's the exact build for the stable image (source in `firmware/`):

```powershell
powershell -ExecutionPolicy Bypass -File firmware/build.ps1 `
  -Profile rhythm900 -Robust -EdgeExt `
  -SamptModes -LoopMode 3 -LoopArm -Release -CadReset 256 `
  -StockIdentity -SerialNumber 20210233 `
  -LateConnect -QuietHeartbeat
```

And the WIP image (source in `firmware-wip/`):

```powershell
powershell -ExecutionPolicy Bypass -File firmware-wip/build.ps1 `
  -Clean -Profile rhythm900 -Robust -EdgeExt `
  -Scan1kModes -AxisGapModes -LoopMode 1 -LoopArm -Release `
  -CadReset 256 -FrameFloorUs 985 -ReadGap 50 `
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
2. builds both images from the matching source trees (`firmware/` and
   `firmware-wip/`),
3. runs the 96-check verifier on each image (ELF, USB descriptors, HID report
   descriptor, vector table and DFU padding),
4. asserts the stable image's SHA-256 is exactly
   `00d11f9e682c7fdb7e52896db66b5170ae881a1fafe4e79638d10282ce262171`
   and the WIP image's SHA-256 is exactly
   `12cdd4689595dc94d68546fe74d279be9c84b1028b8e45afcaaa9fd8bead11a3`,
   both tested on hardware,
5. assembles the user-facing `H420X-Flasher.zip` (flasher + both images +
   README/hashes),
6. uploads everything as workflow artifacts and, on a version tag, attaches the
   zip, both standalone images and their SHA-256 files to the GitHub release.

If any step fails, there's no release. The binary you download is the build the
public workflow produced from the public source, and you can reproduce it
yourself with the command above. That's the whole shabang.

### Verify it yourself

If you want to check the file yourself:

```powershell
certutil -hashfile firmware\rhythm1000.bin SHA256
certutil -hashfile firmware\rhythm1015-wip.bin SHA256
```

Each hash must match `SHA256SUMS.txt` and the release page. The flasher checks
them again before writing.

## How it was tested (the boring part)

It took **51 firmware rounds**, **117
candidate build folders**, and **228 distinct images built** over about **five
weeks** (not really it was on and off). Along the way the bench recorded **113 sessions**, **290 trials**, and
**4.75 million pen reports**. About **115 images were flashed** to the tablet;
**97** distinct images actually ran on hardware. The clean-room app is 5,700
lines of C.

I had a personal test bench thing help me with trying to get it stable and collecting data then fed it back into the token burning AI loop and repeated

The last rounds were mostly about shaving off the last few units: ADC sample
window, cadence reset, stock USB identity, late connect, quiet idle, and a pile
of rejected ideas. The current image is the one that survived.

The final round tested a faster report cadence: a 985 µs frame floor keeps a
fresh report queued ahead of the host poll. On the 9-point steady test it ran
994.3-994.4 Hz with 0.56-0.57 % 2 ms host gaps (scattered, no long runs) and
zero gaps over 5 ms; X scatter was lower than the stable image and Y slightly
higher. It ships as the optional **WIP** image, not the default.

## Limits and TODO

What's not done yet:

- **Cursor only.** No pressure, tip switch or pen buttons. The next build is a
  lower-rate pen image (target maybe 800 or 500/333 Hz) using the clean-room pen path with
  the same ADC window and cadence reset.
- **Host rate.** The stable image's device frame is 1002 µs (998 Hz); the WIP
  image's is 985 µs. The host-side 2 ms gaps are a Windows poll limiter, not
  the device frame - OpenTabletDriver itself reaches ~1 kHz. The WIP image
  cannot force the host to poll every 1 ms, which is why it still shows about
  0.6 % scattered 2 ms gaps.
- **Bootloader.** Only the stock H420X/T223 bootloader is supported. The flasher
  refuses anything else.
- **Not a Huion product.** Huion and H420X are trademarks of their respective
  owner. The VID/PID is retained only so existing host configurations recognize
  the tablet.

## Source and license

- Clean-room firmware: **GPL-3.0-or-later**, see `LICENSE`.
- Stable source: `firmware/`; WIP source: `firmware-wip/`.
- TinyUSB: **MIT**, see `firmware/third_party/tinyusb/LICENSE` (also
  `firmware-wip/third_party/tinyusb/LICENSE`).
- No Huion application code or firmware dump is included.
- Detailed build notes: `firmware/README.md` (stable) and
  `firmware-wip/README.md` (WIP). Provenance: `SOURCE.md`.
