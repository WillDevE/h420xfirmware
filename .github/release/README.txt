H420X clean-room firmware - flasher package
==========================================

This folder contains:
  flash.bat / flash.ps1       the installer
  get-firmware.bat / .ps1     re-downloads the release image if needed
  firmware\rhythm1000.bin      the firmware image
  gigadevice\README.txt       where to get GigaDevice's USB DLLs
  SHA256SUMS.txt              hashes of the files above
  SOURCE.txt                  source code location for this build

Quick start
-----------
1. Put GD_DFU_DLL.dll and vcruntime140.dll in gigadevice\.
   Download GigaDevice's free GD32 All-In-One Programmer:
   https://www.gd32mcu.com/en/download?kw=GD32+All-In-One+Programmer

   If Windows asks for a driver while the tablet is in update mode, install
   GigaDevice's GD32 Dfu Drivers once:
   https://www.gd32mcu.com/en/download?kw=GD32+Dfu+Drivers

2. Run flash.bat and choose S first. That reads the firmware currently on the
   tablet (most likely the stock Huion firmware) and saves it in backups\.
   Nothing is written. Keep that file - it is your way back to stock.

3. Run flash.bat again and choose 1 for rhythm1000.bin.
4. Unplug the USB cable and plug it back in when told.
5. Wait for DONE, then unplug and plug once more.
6. To undo, run flash.bat, choose R, and pick a backup.

The flasher checks firmware\rhythm1000.bin against SHA256SUMS.txt, checks the
tablet is a real H420X/T223 by its bootloader hash, backs up the current app
twice, erases and writes only the 32,000-byte application area at 0x08004000,
and reads it back byte for byte. It never writes the bootloader
(0x08000000) or the settings page (0x0800FC00).

The exact image SHA-256 is in SHA256SUMS.txt. The package was built by GitHub
Actions from the source in this project; SOURCE.txt has the exact source tag.

Project and source:
https://github.com/WillDevE/h420xfirmware
