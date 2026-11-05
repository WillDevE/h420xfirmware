# Source and provenance

The full source for the release image is in `firmware/`. It is clean-room C
written from public USB/HID material, the GD32F350 documentation, and measured
behavior. It contains no Huion application code and no Huion firmware dump.

## Release image

`rhythm1000.bin` is built by GitHub Actions from this repository —
`.github/workflows/build.yml` — and attached to the GitHub release. It is not
stored in the repository.

```text
SHA-256 00d11f9e682c7fdb7e52896db66b5170ae881a1fafe4e79638d10282ce262171
```

The workflow pins Arm GNU Toolchain 12.2.MPACBTI-Rel1, runs the 96-check
verifier, and fails unless the finished image has that exact hash. The same
build can be reproduced locally with the command in `firmware/README.md`.

## What is in the image

- Clean-room three-coil log-domain fit, same-frame only, no output filter or
  temporal averaging.
- Seven reads per frame, 32 excitation pulses, 41.5-cycle ADC sample window,
  1002 µs frame floor, 256-frame cadence reset.
- Stock USB identity, late connect, quiet idle.
- 32,000-byte application image at `0x08004000`; the stock 16 KB bootloader at
  `0x08000000` and the settings page at `0x0800FC00` are never written by the
  flasher.
- Cursor only: no pressure, tip switch or pen buttons in this image.

## License

Clean-room firmware: GPL-3.0-or-later (`LICENSE`). TinyUSB: MIT
(`firmware/third_party/tinyusb/LICENSE`). Huion and H420X are trademarks of
their respective owner; this is not a Huion product.
