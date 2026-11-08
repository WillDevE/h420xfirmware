# WIP firmware source and build (1015 Hz)

This directory is the source for the optional **WIP** image
`rhythm1015-wip.bin`. It is a snapshot of the clean-room source after the
round-51 report-cadence work. It is **not** the stable release image; the
stable source is in `../firmware/`.

No firmware binary is stored in the repository: the image is built by
`.github/workflows/build.yml` and attached to the GitHub release.

## Layout

```text
firmware-wip/
  src/                    clean-room application (GPL-3.0-or-later)
  third_party/tinyusb/    TinyUSB device stack (MIT)
  ld/h420x_app.ld         linker script, application at 0x08004000
  tools/verify_image.py   independent 96-check image verifier
  build.ps1               compile + link + pad + hash
```

## Requirements

- Windows with PowerShell 5.1 or PowerShell 7
- Arm GNU Toolchain **12.2.MPACBTI-Rel1** (`arm-none-eabi-gcc` on `PATH`)

## Build

From the repository root:

```powershell
powershell -ExecutionPolicy Bypass -File firmware-wip/build.ps1 `
  -Clean -Profile rhythm900 -Robust -EdgeExt `
  -Scan1kModes -AxisGapModes -LoopMode 1 -LoopArm -Release `
  -CadReset 256 -FrameFloorUs 985 -ReadGap 50 `
  -StockIdentity -SerialNumber 20210233 `
  -LateConnect -QuietHeartbeat
```

Outputs in `firmware-wip/build/`; the `_app.bin` is the 32,000-byte DFU
payload.

## Verify

```powershell
python firmware-wip/tools/verify_image.py `
  --elf firmware-wip/build/h420x_rhythm900_robust_edge_gap50_scan1k_scan1kmode1_arm_release_stockid_axisgapmodes_lateconnect_quiet_sn20210233_cadreset256_ff985.elf `
  --bin firmware-wip/build/h420x_rhythm900_robust_edge_gap50_scan1k_scan1kmode1_arm_release_stockid_axisgapmodes_lateconnect_quiet_sn20210233_cadreset256_ff985_app.bin
```

Expected: `96 passed, 0 failed`, 32,000-byte payload, SHA-256
`12cdd4689595dc94d68546fe74d279be9c84b1028b8e45afcaaa9fd8bead11a3`.

## Status

**WIP.** Bench result on hardware: 994.3-994.4 Hz across a 9-point steady test
plus a CCW trial, 0.56-0.57 % of host polls at 2 ms (scattered, no long runs),
zero gaps over 5 ms. Compared with the stable image, X scatter is lower and Y
scatter is slightly higher. The flasher labels this image WIP; the stable
`rhythm1000.bin` remains the recommended default.
