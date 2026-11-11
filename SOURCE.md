# Source and provenance

The full source for the release image is in `firmware/`. It is clean-room C
written from public USB/HID material, the GD32F350 documentation, and measured
behavior. It contains no Huion application code and no Huion firmware dump.

## Release image

`rhythm1000.bin` is built by GitHub Actions from `firmware/`. The workflow is
`.github/workflows/build.yml`. The image is not stored in the repository. The
workflow packages it in `H420X-Flasher.zip` together with the flasher and the
quick-start README.

```text
SHA-256 7543ef7d1eb68658e2959ca1c966f1ad8d11402f9e4d5985d55c9740bebd51a5  rhythm1000.bin
```

The workflow pins Arm GNU Toolchain 12.2.MPACBTI-Rel1, runs the 96-check
verifier on the image, and fails unless the finished image has that exact hash.
The same build can be reproduced locally with the commands in
`firmware/README.md`.

## What is in the image

- Clean-room three-coil log-domain fit, same-frame only, no output filter or
  temporal averaging.
- Fixed estimator constants only (v2.0.1): Y odd cubic weight 12000, coil
  ownership hysteresis 1/256, no run-time learner or calibration of any kind.
- Frames locked to the USB IN transfer, about 1000 Hz, with cycle-exact read
  timing.
- Seven reads per frame (a Y prep read, three Y, three X), 28 excitation pulses,
  41.5-cycle ADC sample window, 256-frame cadence reset, stock USB identity,
  late connect, quiet idle.
- 32,000-byte application image at `0x08004000`; the stock 16 KB bootloader at
  `0x08000000` and the settings page at `0x0800FC00` are never written by the
  flasher.
- Cursor only: no pressure, tip switch or pen buttons.

The source also contains the test-round code paths that led to this image; the
build switches select the released configuration, and everything else is
compiled out.

## Legacy images

The older `00d11f9e` (v1.0.x stable), `12cdd468` (WIP) and `08bbedc4` (v2.0.0)
images were built from the source at their release tags (`firmware/` and
`firmware-wip/`).

## License

Clean-room firmware: GPL-3.0-or-later (`LICENSE`). TinyUSB: MIT
(`firmware/third_party/tinyusb/LICENSE`). Huion and H420X are trademarks of
their respective owner; this is not a Huion product.
