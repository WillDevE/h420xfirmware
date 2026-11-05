# Third-party notices

This repository contains or uses the following third-party components:

## TinyUSB

`firmware/third_party/tinyusb/` is the TinyUSB device stack, licensed under the
MIT License. The full license text is in
`firmware/third_party/tinyusb/LICENSE`.

## Arm GNU Toolchain

The release image is built with Arm GNU Toolchain 12.2.MPACBTI-Rel1. The
toolchain is downloaded by the GitHub Actions workflow and is not distributed
in this repository. See the Arm GNU Toolchain release notes and licenses for its
terms.

## GigaDevice USB library

The flasher uses GigaDevice's `GD_DFU_DLL.dll` from their free GD32 All-In-One
Programmer. It is not distributed in this repository; the user downloads it
from GigaDevice. See `README.md` for the link.

## Trademarks

Huion and H420X are trademarks of their respective owner. This project is not
affiliated with or endorsed by Huion. The Huion VID/PID is retained only so
existing host configurations recognize the tablet.
