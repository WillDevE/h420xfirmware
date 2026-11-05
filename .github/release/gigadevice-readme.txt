Put these two files in this folder:

  GD_DFU_DLL.dll
  vcruntime140.dll

They come from GigaDevice's free "GD32 All-In-One Programmer":
https://www.gd32mcu.com/en/download?kw=GD32+All-In-One+Programmer

Both files ship in that download. Copy them side by side here. The flasher
checks GD_DFU_DLL.dll against the tested version before using it.

If the flasher says the DLL cannot load (missing runtime), either make sure
vcruntime140.dll sits next to GD_DFU_DLL.dll, or install Microsoft's free
Visual C++ Redistributable (x86):
https://aka.ms/vs/17/release/vc_redist.x86.exe

If Windows asks for a driver while the tablet is in update mode, install
GigaDevice's GD32 Dfu Drivers once:
https://www.gd32mcu.com/en/download?kw=GD32+Dfu+Drivers

Shortcut: run flash.bat without the files and it offers D to open the
GigaDevice page or B to point at a downloaded zip.
