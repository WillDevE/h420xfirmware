# Source and provenance

The full source for the stable image is in `firmware/`. The source for the
optional WIP image is in `firmware-wip/`. Both are clean-room C written from
public USB/HID material, the GD32F350 documentation, and measured behavior.
They contain no Huion application code and no Huion firmware dump.

## Release images

`rhythm1000.bin` (stable) is built by GitHub Actions from `firmware/`;
`rhythm1015-wip.bin` (WIP) is built from `firmware-wip/`. The workflow is
`.github/workflows/build.yml`. Neither image is stored in the repository. The
workflow also packages both in `H420X-Flasher.zip` together with the flasher
and the quick-start README.

```text
SHA-256 00d11f9e682c7fdb7e52896db66b5170ae881a1fafe4e79638d10282ce262171  rhythm1000.bin
SHA-256 12cdd4689595dc94d68546fe74d279be9c84b1028b8e45afcaaa9fd8bead11a3  rhythm1015-wip.bin
```

The workflow pins Arm GNU Toolchain 12.2.MPACBTI-Rel1, runs the 96-check
verifier on each image, and fails unless both finished images have those exact
hashes. The same builds can be reproduced locally with the commands in
`firmware/README.md` and `firmware-wip/README.md`.

## What is in the images

- Clean-room three-coil log-domain fit, same-frame only, no output filter or
  temporal averaging.
- Seven reads per frame, 32 excitation pulses, 41.5-cycle ADC sample window,
  256-frame cadence reset, stock USB identity, late connect, quiet idle.
- **Stable:** 1002 µs frame floor.
- **WIP:** 985 µs frame floor, which queues the next report earlier and raises
  the host-observed rate. It is labeled WIP in the flasher and README because
  the remaining 2 ms gaps (a deterministic ~180 ms beat) still need evaluation.
- 32,000-byte application image at `0x08004000`; the stock 16 KB bootloader at
  `0x08000000` and the settings page at `0x0800FC00` are never written by the
  flasher.
- Cursor only: no pressure, tip switch or pen buttons in either image.

## License

Clean-room firmware: GPL-3.0-or-later (`LICENSE`). TinyUSB: MIT
(`firmware/third_party/tinyusb/LICENSE` and
`firmware-wip/third_party/tinyusb/LICENSE`). Huion and H420X are trademarks of
their respective owner; this is not a Huion product.
