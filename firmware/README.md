# Firmware source and build

This directory is the complete source for the `rhythm1000.bin` image. No firmware
binary is stored in the repository: the image is built by
`.github/workflows/build.yml` and packaged in `H420X-Flasher.zip` on the GitHub
release.

## Layout

```text
firmware/
  src/                    clean-room application (GPL-3.0-or-later)
  third_party/tinyusb/    TinyUSB device stack (MIT)
  ld/h420x_app.ld         linker script, application at 0x08004000
  tools/verify_image.py   independent 96-check image verifier
  build.ps1               compile + link + pad + hash
```

## Requirements

- Windows with PowerShell 5.1 or PowerShell 7
- Arm GNU Toolchain **12.2.MPACBTI-Rel1** (`arm-none-eabi-gcc` on `PATH`)

The version matters. The release image is byte-for-byte reproducible only with
that compiler release. Newer toolchains may generate different code.

## Build

From the repository root:

```powershell
powershell -ExecutionPolicy Bypass -File firmware/build.ps1 `
  -Clean -Profile rhythm900 -Robust -EdgeExt `
  -Scan1kModes -AxisGapModes -LoopMode 1 -LoopArm -Release `
  -CadReset 256 -ReadGap 50 `
  -StockIdentity -SerialNumber 20210233 `
  -LateConnect -QuietHeartbeat -FastEp1 -R63Modes -NoR55Aux
```

`-Clean` removes the object directory first; CI always uses it.

Outputs in `firmware/build/`:

```text
h420x_rhythm900_robust_edge_gap50_scan1k_scan1kmode1_arm_release_stockid_axisgapmodes_lateconnect_quiet_sn20210233_cadreset256_fastep1_r63modes_noaux_app.bin
h420x_rhythm900_robust_edge_gap50_scan1k_scan1kmode1_arm_release_stockid_axisgapmodes_lateconnect_quiet_sn20210233_cadreset256_fastep1_r63modes_noaux.elf
h420x_rhythm900_robust_edge_gap50_scan1k_scan1kmode1_arm_release_stockid_axisgapmodes_lateconnect_quiet_sn20210233_cadreset256_fastep1_r63modes_noaux.map
h420x_rhythm900_robust_edge_gap50_scan1k_scan1kmode1_arm_release_stockid_axisgapmodes_lateconnect_quiet_sn20210233_cadreset256_fastep1_r63modes_noaux.lst
```

The `_app.bin` is the 32,000-byte DFU payload (0xFF padded).

## Verify

```powershell
python firmware/tools/verify_image.py `
  --elf firmware/build/h420x_rhythm900_robust_edge_gap50_scan1k_scan1kmode1_arm_release_stockid_axisgapmodes_lateconnect_quiet_sn20210233_cadreset256_fastep1_r63modes_noaux.elf `
  --bin firmware/build/h420x_rhythm900_robust_edge_gap50_scan1k_scan1kmode1_arm_release_stockid_axisgapmodes_lateconnect_quiet_sn20210233_cadreset256_fastep1_r63modes_noaux_app.bin
```

Expected: `96 passed, 0 failed`, 32,000-byte payload, SHA-256
`7543ef7d1eb68658e2959ca1c966f1ad8d11402f9e4d5985d55c9740bebd51a5`.

The verifier does not use the firmware sources or linker map. It parses the ELF,
reconstructs the loadable image, and checks the boot contract, USB descriptors,
HID report descriptors and DFU padding directly.

## What the workflow does

`.github/workflows/build.yml`:

1. downloads Arm GNU Toolchain 12.2.MPACBTI-Rel1 and checks its SHA-256,
2. runs the build above,
3. runs the verifier,
4. fails unless the finished image hash is
   `7543ef7d1eb68658e2959ca1c966f1ad8d11402f9e4d5985d55c9740bebd51a5`,
5. assembles `H420X-Flasher.zip` (flasher + built image + README/hashes),
6. uploads the zip as an artifact and, on a version tag, attaches it to the
   GitHub release.

The release asset is therefore the workflow's output, not a binary built on a
developer machine.
