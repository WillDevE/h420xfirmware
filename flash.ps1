# H420X / T223 firmware installer - plain text, read it before running.
#
# What it does, in order (nothing else):
#   1. Checks the chosen .bin against SHA256SUMS.txt, and checks its size and vector table.
#   2. Waits for the tablet's bootloader (USB 28E9:0189) after you replug the tablet.
#   3. CHECKS the tablet is the right one: reads the bootloader and refuses to
#      continue unless it is the exact H420X/T223 one; checks the page tail is
#      empty; reads the settings page; reads the installed app and reports what
#      it is (or that it is damaged). Nothing is written during the check.
#   4. Reads the current firmware twice and saves it to .\backups\ (restorable with this script).
#   5. Re-verifies the same tablet is still connected, then erases and writes
#      ONLY the 32,000-byte application area at 0x08004000, then reads it back
#      and compares every byte.
# It never writes the bootloader (0x08000000) or the settings page (0x0800FC00).
#
# USB access goes through GigaDevice's own GD_DFU_DLL.dll from their free
# "GD32 All-In-One Programmer". It is NOT included: download it once into
# gigadevice\ (see README.md) — it is checked there first (then next to the
# script, then the remembered path, then the rest of the computer as a
# fallback). Its hash is checked against the tested version before use.
# That DLL is 32-bit, which is why flash.bat starts the 32-bit Windows
# PowerShell. The C# block below is only declarations for calling that DLL and
# Windows' setupapi.dll / kernel32.dll.

param(
    [string]$Firmware = "",       # optional: path to a .bin (skips the menu)
    [switch]$BackupOnly,          # optional: only save the tablet's current firmware
    [int]$TimeoutSeconds = 120    # how long to wait for the tablet's bootloader
)
$ErrorActionPreference = 'Stop'
$Here = Split-Path -Parent $MyInvocation.MyCommand.Path

$APP_START = 0x08004000; $APP_LEN = 32000; $PAGE = 0x400
$BOOT_START = 0x08000000; $BOOT_LEN = 16384
$CFG_START = 0x0800FC00; $CFG_LEN = 1024
$TAIL_START = 0x0800BD00; $TAIL_LEN = 768
$BOOT_SHA256 = '7C1763281D039D8DB341B1E2E66D3B08F843CBAE4B53BA6B43FA7F3F6AAEB188'
$DLL_SHA256  = 'C97D2A6A94747D2C7532BB98ED447008319C3D74E87D96CDF018B54602A5AD14'

function Sha256Hex([byte[]]$Data) {
    $h = [System.Security.Cryptography.SHA256]::Create()
    return ([BitConverter]::ToString($h.ComputeHash($Data))).Replace('-', '')
}
function Fail([string]$Message) { Write-Host ""; Write-Host "STOPPED: $Message" -ForegroundColor Red; exit 1 }

if ([IntPtr]::Size -ne 4) { Fail 'Start this through flash.bat (it needs 32-bit PowerShell for the GigaDevice DLL).' }

# ---------- 1. choose and check the firmware file ----------
$Order = 'rhythm900.bin', 'rhythm750.bin', 'pen500.bin', 'pen333.bin', 'stock.bin'
$Descriptions = @{
    'rhythm900.bin' = '~910 Hz, cursor only (no pressure/buttons) - rhythm games'
    'rhythm750.bin' = '~765 Hz, cursor only (no pressure/buttons) - cleanest high rate'
    'pen500.bin'    = '500 Hz exactly, pressure + pen buttons'
    'pen333.bin'    = '333 Hz exactly, pressure + pen buttons'
    'stock.bin'     = "Huion's original factory firmware"
}
$listed = @{}   # file name -> SHA-256, from SHA256SUMS.txt
foreach ($line in Get-Content (Join-Path $Here 'SHA256SUMS.txt')) {
    if ($line -match '^([0-9a-fA-F]{64})\s+\*?(\S+)$') { $listed[$Matches[2]] = $Matches[1].ToUpper() }
}
if ($Firmware -eq "" -and -not $BackupOnly) {
    $choices = @(Get-ChildItem (Join-Path $Here 'firmware') -Filter *.bin |
                 Sort-Object @{ Expression = { $i = [Array]::IndexOf($Order, $_.Name); if ($i -lt 0) { 99 } else { $i } } }, Name)
    $backups = @(Get-ChildItem (Join-Path $Here 'backups') -Filter *.bin -ErrorAction SilentlyContinue | Sort-Object Name)
    if ($choices.Count -eq 0) { Fail 'No .bin files in the firmware folder.' }
    Write-Host ""
    Write-Host "  H420X firmware installer" -ForegroundColor Cyan
    Write-Host ""
    Write-Host "  S) Save a backup of the tablet's current firmware only (changes nothing)"
    if ($backups.Count -eq 0) { Write-Host "     - recommended before your first install" -ForegroundColor DarkGray }
    for ($i = 0; $i -lt $choices.Count; $i++) {
        Write-Host ("  {0}) {1,-14} {2}" -f ($i + 1), $choices[$i].Name, $Descriptions[$choices[$i].Name])
    }
    if ($backups.Count -gt 0) { Write-Host "  R) Restore one of your backups" }
    Write-Host ""
    Write-Host "  (Every install also saves a backup first.)" -ForegroundColor DarkGray
    $pick = (Read-Host "Type your choice and press Enter").Trim()
    if ($pick -eq 'S' -or $pick -eq 's') { $BackupOnly = $true }
    elseif ($pick -eq 'R' -or $pick -eq 'r') {
        if ($backups.Count -eq 0) { Fail 'There are no backups yet.' }
        Write-Host ""
        for ($i = 0; $i -lt $backups.Count; $i++) { Write-Host ("  {0}) {1}" -f ($i + 1), $backups[$i].Name) }
        Write-Host "  (The oldest backup is the firmware the tablet had before you first used this tool.)"
        $pick = (Read-Host "Type the backup number and press Enter").Trim()
        $choices = $backups
    }
    if (-not $BackupOnly) {
        $n = 0
        if (-not [int]::TryParse($pick, [ref]$n) -or $n -lt 1 -or $n -gt $choices.Count) { Fail 'That is not one of the choices. Nothing was changed.' }
        $Firmware = $choices[$n - 1].FullName
    }
}
if ($BackupOnly) {
    Write-Host "Backup only: the tablet's firmware will be read and saved; nothing is written."
    $name = 'backup'
} else {
if (-not (Test-Path -LiteralPath $Firmware)) { Fail "File not found: $Firmware" }
$image = [System.IO.File]::ReadAllBytes((Resolve-Path -LiteralPath $Firmware))
$imageSha = Sha256Hex $image
$name = Split-Path -Leaf $Firmware
$isBackup = (Split-Path -Parent (Resolve-Path -LiteralPath $Firmware)) -eq (Join-Path $Here 'backups')
if ($listed.ContainsKey($name)) {
    if ($listed[$name] -ne $imageSha) { Fail "$name does not match SHA256SUMS.txt (file changed or corrupted)." }
    Write-Host "Checksum OK: $name  $imageSha"
} elseif ($isBackup) {
    Write-Host "Restoring backup $name (SHA-256 $imageSha)"
} else {
    Write-Host "$name is not one of the included firmwares. SHA-256: $imageSha" -ForegroundColor Yellow
    if ((Read-Host "Install it anyway? Type YES") -ne 'YES') { Fail 'Cancelled.' }
}
if ($image.Length -ne $APP_LEN) { Fail "Expected $APP_LEN bytes, file has $($image.Length)." }
$sp = [BitConverter]::ToUInt32($image, 0); $reset = [BitConverter]::ToUInt32($image, 4)
if ($sp -le 0x20000000 -or $sp -gt 0x20005000 -or ($sp % 4) -ne 0 -or ($reset -band 1) -ne 1 -or
    ($reset -band 0xFFFFFFFE) -lt $APP_START -or ($reset -band 0xFFFFFFFE) -ge ($APP_START + $APP_LEN)) {
    Fail 'This is not an H420X application image (bad vector table).'
}
}  # end of firmware checks (skipped for backup-only)

# ---------- find GigaDevice's GD_DFU_DLL.dll (download once into gigadevice\) ----------
# It comes with GigaDevice's free "GD32 All-In-One Programmer" and is NOT
# included here, so download it once (see README.md for the links). Order:
# local copies first (next to this script, then gigadevice\), then the
# remembered path, then the rest of the computer (download folders /
# still-packed download / browse).
$DLL_PAGE = 'https://www.gd32mcu.com/en/download?kw=GD32+All-In-One+Programmer'
$Remember = Join-Path $Here 'gigadevice_dll_location.txt'
$Unpacked = Join-Path $Here 'gigadevice'   # downloaded DLLs live here (also the unpack destination)
function Test-GdDll([string]$Path) {
    if (-not $Path -or -not (Test-Path -LiteralPath $Path -PathType Leaf)) { return $false }
    if ((Split-Path -Leaf $Path) -ne 'GD_DFU_DLL.dll') { return $false }
    $b = [System.IO.File]::ReadAllBytes($Path)
    if ($b.Length -lt 0x100 -or $b[0] -ne 0x4D -or $b[1] -ne 0x5A) { return $false }
    $pe = [BitConverter]::ToInt32($b, 0x3C)
    return ($pe -gt 0 -and $pe + 6 -lt $b.Length -and [BitConverter]::ToUInt16($b, $pe + 4) -eq 0x14C)  # 32-bit
}
function Expand-GdArchive([string]$Archive) {
    # Copy only GD_DFU_DLL.dll and vcruntime140.dll out of a downloaded archive.
    New-Item -ItemType Directory -Force $Unpacked | Out-Null
    if ($Archive -match '\.zip$') {
        Add-Type -AssemblyName System.IO.Compression.FileSystem
        $zip = [System.IO.Compression.ZipFile]::OpenRead($Archive)
        try {
            foreach ($e in $zip.Entries) {
                if ($e.Name -eq 'GD_DFU_DLL.dll' -or $e.Name -eq 'vcruntime140.dll') {
                    [System.IO.Compression.ZipFileExtensions]::ExtractToFile($e, (Join-Path $Unpacked $e.Name), $true)
                }
            }
        } finally { $zip.Dispose() }
    } else {
        # .rar / .7z: Windows' built-in tar.exe can read these on recent Windows 11.
        $tmp = Join-Path $env:TEMP ('gd32_' + [Guid]::NewGuid().ToString('N'))
        New-Item -ItemType Directory $tmp | Out-Null
        try {
            & "$env:SystemRoot\System32\tar.exe" -xf $Archive -C $tmp 2>$null
            foreach ($n in 'GD_DFU_DLL.dll', 'vcruntime140.dll') {
                $f = Get-ChildItem $tmp -Recurse -Filter $n -ErrorAction SilentlyContinue | Select-Object -First 1
                if ($f) { Copy-Item $f.FullName (Join-Path $Unpacked $n) -Force }
            }
        } finally { Remove-Item $tmp -Recurse -Force -ErrorAction SilentlyContinue }
    }
    $got = Join-Path $Unpacked 'GD_DFU_DLL.dll'
    if (Test-GdDll $got) { return $got } else { return $null }
}
function Find-GdDll {
    # Local copies first: 1) next to this script, 2) gigadevice\ subfolder,
    # then 3) the remembered path, and only then the rest of the computer.
    $prev = if (Test-Path $Remember) { (Get-Content $Remember -TotalCount 1) } else { $null }
    foreach ($p in @((Join-Path $Here 'GD_DFU_DLL.dll'), (Join-Path $Unpacked 'GD_DFU_DLL.dll'), $prev)) {
        if (Test-GdDll $p) { return $p }
    }
    # 4) an extracted download in the usual places
    $roots = @("$env:USERPROFILE\Downloads", "$env:USERPROFILE\Desktop", "$env:USERPROFILE\Documents",
               ${env:ProgramFiles(x86)}, $env:ProgramFiles) | Where-Object { $_ -and (Test-Path $_) }
    Write-Host "Looking for GigaDevice's GD_DFU_DLL.dll..."
    foreach ($r in $roots) {
        $hit = Get-ChildItem $r -Recurse -Depth 4 -Filter 'GD_DFU_DLL.dll' -ErrorAction SilentlyContinue |
               Where-Object { Test-GdDll $_.FullName } | Select-Object -First 1
        if ($hit) { return $hit.FullName }
    }
    # 5) the download itself, still packed
    foreach ($r in @($roots | Select-Object -First 2)) {   # Downloads, Desktop
        $arc = Get-ChildItem $r -File -ErrorAction SilentlyContinue |
               Where-Object { $_.Name -match 'All.?In.?One' -and $_.Extension -match '^\.(zip|rar|7z)$' } |
               Sort-Object LastWriteTime -Descending | Select-Object -First 1
        if ($arc) {
            Write-Host "Found the download $($arc.Name); taking the DLL out of it..."
            $got = Expand-GdArchive $arc.FullName
            if ($got) { return $got }
            Write-Host "Could not unpack $($arc.Name) automatically - please extract it (right-click > Extract All)." -ForegroundColor Yellow
        }
    }
    return $null
}
$dllPath = Find-GdDll
while (-not $dllPath) {
    Write-Host ""
    Write-Host "GigaDevice's GD_DFU_DLL.dll was not found locally (gigadevice\ was checked first)." -ForegroundColor Yellow
    Write-Host "It comes with GigaDevice's free 'GD32 All-In-One Programmer':"
    Write-Host "  $DLL_PAGE"
    Write-Host "  D) open that download page     B) browse to the file you downloaded     Q) quit"
    $answer = (Read-Host "Choose D, B or Q").Trim().ToUpper()
    if ($answer -eq 'D') {
        Start-Process $DLL_PAGE
        Read-Host "Download it, then press Enter here"
        $dllPath = Find-GdDll
    } elseif ($answer -eq 'B') {
        Add-Type -AssemblyName System.Windows.Forms
        $dlg = New-Object System.Windows.Forms.OpenFileDialog
        $dlg.Title = 'Select GD_DFU_DLL.dll or the downloaded All-In-One Programmer archive'
        $dlg.Filter = 'GD_DFU_DLL.dll or download (*.dll;*.zip;*.rar;*.7z)|*.dll;*.zip;*.rar;*.7z'
        $dlg.InitialDirectory = "$env:USERPROFILE\Downloads"
        if ($dlg.ShowDialog() -eq [System.Windows.Forms.DialogResult]::OK) {
            if ($dlg.FileName -match '\.(zip|rar|7z)$') { $dllPath = Expand-GdArchive $dlg.FileName }
            elseif (Test-GdDll $dlg.FileName) { $dllPath = $dlg.FileName }
            if (-not $dllPath) { Write-Host "That is not GigaDevice's 32-bit GD_DFU_DLL.dll (or an archive containing it)." -ForegroundColor Yellow }
        }
    } else { Fail 'GD_DFU_DLL.dll not provided. Nothing was changed.' }
}
$dllSha = Sha256Hex ([System.IO.File]::ReadAllBytes($dllPath))
if ($dllSha -ne $DLL_SHA256) {
    Write-Host "This GD_DFU_DLL.dll ($dllSha) is a different version than the tested v5.4.0.42035." -ForegroundColor Yellow
    if ((Read-Host "Continue anyway? Type YES") -ne 'YES') { Fail 'Cancelled.' }
}
Set-Content -Path $Remember -Value $dllPath
Write-Host "Using $dllPath"
$dllDir = Split-Path -Parent $dllPath
Add-Type -TypeDefinition @"
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
public static class H420xDfu {
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    public static extern bool SetDllDirectory(string path);
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    public static extern IntPtr LoadLibraryW(string path);

    // GigaDevice DFU DLL (cdecl). Request = {u16 0, u16 block, u16 value, u16 length, void* buffer}.
    [StructLayout(LayoutKind.Sequential, Pack = 1)]
    public struct Req { public ushort Zero; public ushort Block; public ushort Value; public ushort Length; public IntPtr Buffer; }
    [DllImport("GD_DFU_DLL.dll", CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
    public static extern IntPtr GDDFU_DeviceOpen(string devicePath);
    [DllImport("GD_DFU_DLL.dll", CallingConvention = CallingConvention.Cdecl)]
    public static extern int GDDFU_DeviceClose(IntPtr h);
    [DllImport("GD_DFU_DLL.dll", CallingConvention = CallingConvention.Cdecl)]
    public static extern int GDDFU_Upload(IntPtr h, ref Req r);
    [DllImport("GD_DFU_DLL.dll", CallingConvention = CallingConvention.Cdecl)]
    public static extern int GDDFU_Download(IntPtr h, ref Req r);
    [DllImport("GD_DFU_DLL.dll", CallingConvention = CallingConvention.Cdecl)]
    public static extern int GDDFU_GetStatus(IntPtr h, byte[] status6);
    [DllImport("GD_DFU_DLL.dll", CallingConvention = CallingConvention.Cdecl)]
    public static extern int GDDFU_ClearStatus(IntPtr h);
    [DllImport("GD_DFU_DLL.dll", CallingConvention = CallingConvention.Cdecl)]
    public static extern int GDDFU_WaitStatus(IntPtr h, int state);
    [DllImport("GD_DFU_DLL.dll", CallingConvention = CallingConvention.Cdecl)]
    public static extern int GDDFU_Abort(IntPtr h);

    // setupapi: list device paths of GigaDevice DFU interfaces.
    [StructLayout(LayoutKind.Sequential)]
    struct IfaceData { public int cbSize; public Guid ClassGuid; public int Flags; public IntPtr Reserved; }
    [DllImport("setupapi.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    static extern IntPtr SetupDiGetClassDevsW(ref Guid g, IntPtr enumerator, IntPtr hwnd, int flags);
    [DllImport("setupapi.dll", SetLastError = true)]
    static extern bool SetupDiEnumDeviceInterfaces(IntPtr set, IntPtr info, ref Guid g, int index, ref IfaceData d);
    [DllImport("setupapi.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    static extern bool SetupDiGetDeviceInterfaceDetailW(IntPtr set, ref IfaceData d, IntPtr detail, int size, out int needed, IntPtr info);
    [DllImport("setupapi.dll")]
    static extern bool SetupDiDestroyDeviceInfoList(IntPtr set);

    public static string[] DevicePaths() {
        Guid g = new Guid("573E8C73-0CB4-4471-A1BF-FAB26C31D384");
        List<string> paths = new List<string>();
        IntPtr set = SetupDiGetClassDevsW(ref g, IntPtr.Zero, IntPtr.Zero, 0x12); // PRESENT | DEVICEINTERFACE
        if (set == new IntPtr(-1)) return paths.ToArray();
        try {
            for (int i = 0; ; i++) {
                IfaceData d = new IfaceData(); d.cbSize = Marshal.SizeOf(d);
                if (!SetupDiEnumDeviceInterfaces(set, IntPtr.Zero, ref g, i, ref d)) break;
                int needed;
                SetupDiGetDeviceInterfaceDetailW(set, ref d, IntPtr.Zero, 0, out needed, IntPtr.Zero);
                IntPtr buf = Marshal.AllocHGlobal(needed);
                try {
                    Marshal.WriteInt32(buf, IntPtr.Size == 8 ? 8 : 6);
                    if (SetupDiGetDeviceInterfaceDetailW(set, ref d, buf, needed, out needed, IntPtr.Zero))
                        paths.Add(Marshal.PtrToStringUni(new IntPtr(buf.ToInt64() + 4)));
                } finally { Marshal.FreeHGlobal(buf); }
            }
        } finally { SetupDiDestroyDeviceInfoList(set); }
        return paths.ToArray();
    }
}
"@
# Load exactly the file found above; its folder is also searched for vcruntime140.dll.
[void][H420xDfu]::SetDllDirectory($dllDir)
if ([H420xDfu]::LoadLibraryW($dllPath) -eq [IntPtr]::Zero) {
    $err = [Runtime.InteropServices.Marshal]::GetLastWin32Error()
    if ($err -eq 126) {
        Fail ("GD_DFU_DLL.dll needs Microsoft's Visual C++ runtime (32-bit). Put vcruntime140.dll from the same GigaDevice " +
              "download next to it, or install https://aka.ms/vs/17/release/vc_redist.x86.exe, then try again.")
    }
    Fail "Windows could not load GD_DFU_DLL.dll (error $err)."
}

# ---------- DFU operations (same sequence as the Kombini flasher's helper) ----------
function Send-Command([IntPtr]$H, [byte]$Op, [uint32]$Address) {
    $cmd = [byte[]](@($Op) + [BitConverter]::GetBytes($Address))
    $mem = [Runtime.InteropServices.Marshal]::AllocHGlobal(5)
    try {
        [Runtime.InteropServices.Marshal]::Copy($cmd, 0, $mem, 5)
        $r = New-Object H420xDfu+Req; $r.Block = 0; $r.Length = 5; $r.Buffer = $mem
        return [H420xDfu]::GDDFU_Download($H, [ref]$r)
    } finally { [Runtime.InteropServices.Marshal]::FreeHGlobal($mem) }
}
function Read-Block([IntPtr]$H, [uint32]$Address, [int]$Size) {
    [void][H420xDfu]::GDDFU_WaitStatus($H, 2)
    $rc = Send-Command $H 0x21 $Address                      # set address pointer
    if ($rc -eq -1) { $rc = Send-Command $H 0x21 $Address }  # one retry, as the original helper does
    [void][H420xDfu]::GDDFU_WaitStatus($H, 2)
    [void][H420xDfu]::GDDFU_Abort($H)
    $mem = [Runtime.InteropServices.Marshal]::AllocHGlobal($Size + 16)
    try {
        $r = New-Object H420xDfu+Req; $r.Block = 2; $r.Length = $Size; $r.Buffer = $mem
        $rc = [H420xDfu]::GDDFU_Upload($H, [ref]$r)
        if ($rc -le 0) { throw ("read failed at 0x{0:X8} (rc={1})" -f $Address, $rc) }
        $out = New-Object byte[] $Size
        [Runtime.InteropServices.Marshal]::Copy($mem, $out, 0, $Size)
        return ,$out
    } finally { [Runtime.InteropServices.Marshal]::FreeHGlobal($mem) }
}
function Read-Range([IntPtr]$H, [uint32]$Address, [int]$Length) {
    $all = New-Object byte[] $Length
    for ($off = 0; $off -lt $Length; $off += 1024) {
        $n = [Math]::Min(1024, $Length - $off)
        $chunk = Read-Block $H ($Address + $off) $n
        [Array]::Copy($chunk, 0, $all, $off, $n)
    }
    return ,$all
}
function Erase-Page([IntPtr]$H, [uint32]$Address) {
    [void][H420xDfu]::GDDFU_ClearStatus($H); [void][H420xDfu]::GDDFU_WaitStatus($H, 2)
    $rc = Send-Command $H 0x41 $Address
    $st = New-Object byte[] 6; [void][H420xDfu]::GDDFU_GetStatus($H, $st)
    if ($rc -le 0) { throw ("erase failed at 0x{0:X8} (rc={1})" -f $Address, $rc) }
}
function Write-Block([IntPtr]$H, [uint32]$Address, [byte[]]$Data) {
    [void][H420xDfu]::GDDFU_ClearStatus($H); [void][H420xDfu]::GDDFU_WaitStatus($H, 2)
    [void](Send-Command $H 0x21 $Address)
    [void][H420xDfu]::GDDFU_ClearStatus($H); [void][H420xDfu]::GDDFU_WaitStatus($H, 2)
    $mem = [Runtime.InteropServices.Marshal]::AllocHGlobal($Data.Length)
    try {
        [Runtime.InteropServices.Marshal]::Copy($Data, 0, $mem, $Data.Length)
        $r = New-Object H420xDfu+Req; $r.Block = 2; $r.Length = $Data.Length; $r.Buffer = $mem
        $rc = [H420xDfu]::GDDFU_Download($H, [ref]$r)
    } finally { [Runtime.InteropServices.Marshal]::FreeHGlobal($mem) }
    $st = New-Object byte[] 6; [void][H420xDfu]::GDDFU_GetStatus($H, $st)
    if ($rc -le 0) { throw ("write failed at 0x{0:X8} (rc={1})" -f $Address, $rc) }
}
function Same([byte[]]$A, [byte[]]$B) {
    if ($A.Length -ne $B.Length) { return $false }
    for ($i = 0; $i -lt $A.Length; $i++) { if ($A[$i] -ne $B[$i]) { return $false } }
    return $true
}

# ---------- 2. wait for the bootloader ----------
Write-Host ""
Write-Host "  >>> UNPLUG the tablet's USB cable, then PLUG IT BACK IN. <<<" -ForegroundColor Cyan
Write-Host "      (waiting up to $TimeoutSeconds seconds; the tablet is caught automatically)"
$deadline = (Get-Date).AddSeconds($TimeoutSeconds)
$handle = [IntPtr]::Zero
$seenButBusy = $false
while ((Get-Date) -lt $deadline -and $handle -eq [IntPtr]::Zero) {
    $paths = @([H420xDfu]::DevicePaths() | Where-Object { $_ -match 'vid_28e9&pid_0189' })
    if ($paths.Count -gt 1) { Fail 'More than one tablet is in update mode. Connect only one tablet and try again.' }
    if ($paths.Count -eq 1) {
        $h = [H420xDfu]::GDDFU_DeviceOpen($paths[0])
        if ($h -ne [IntPtr]::Zero -and $h -ne [IntPtr](-1)) { $handle = $h } else { $seenButBusy = $true }
    }
    if ($handle -eq [IntPtr]::Zero) { Start-Sleep -Milliseconds 20 }
}
if ($handle -eq [IntPtr]::Zero) {
    # Explain why, using Windows' own record of the tablet's update-mode device.
    $key = 'HKLM:\SYSTEM\CurrentControlSet\Enum\USB\VID_28E9&PID_0189'
    $services = @(Get-ChildItem $key -ErrorAction SilentlyContinue | ForEach-Object { (Get-ItemProperty $_.PSPath).Service })
    if ($seenButBusy) {
        Fail 'The tablet was found but another program is using it. Close any other flasher or GD32 tool, then try again.'
    } elseif ($services.Count -eq 0) {
        Fail "The tablet never entered update mode. Unplug the cable fully (not just the tablet's end sleeping), plug it straight into the PC, and try again."
    } elseif (-not ($services -contains 'GDDFUDriver')) {
        Fail "Windows has no driver for the tablet's update mode. Install the GigaDevice DFU driver once (see README.md), then try again."
    } else {
        Fail 'The tablet was not caught this time. Run flash.bat again and replug when asked.'
    }
}
Write-Host "Tablet caught in update mode." -ForegroundColor Green

$erased = $false
try {
    # ---------- 3. check the tablet is the right one (read-only; nothing written) ----------
    $boot = Read-Range $handle $BOOT_START $BOOT_LEN
    if ((Sha256Hex $boot) -ne $BOOT_SHA256) { Fail 'This is not the supported H420X/T223 bootloader. Nothing was changed.' }
    $tail = Read-Range $handle $TAIL_START $TAIL_LEN
    foreach ($b in $tail) { if ($b -ne 0xFF) { Fail 'Unexpected data after the application area. Nothing was changed.' } }
    $cfg = Read-Range $handle $CFG_START $CFG_LEN
    $cfgPrint = (Sha256Hex $cfg).Substring(0, 12).ToLower()
    Write-Host "CHECK: supported H420X/T223 bootloader; page tail empty; settings page read OK (fingerprint $cfgPrint)."

    # ---------- 4. backup ----------
    $current = Read-Range $handle $APP_START $APP_LEN
    $again = Read-Range $handle $APP_START $APP_LEN
    if (-not (Same $current $again)) { Fail 'Two reads of the current firmware differ. Nothing was changed; replug and retry.' }
    # Identify what is currently installed (same vector-table rule as for files).
    $csp = [BitConverter]::ToUInt32($current, 0); $creset = [BitConverter]::ToUInt32($current, 4)
    $installedOk = -not ($csp -le 0x20000000 -or $csp -gt 0x20005000 -or ($csp % 4) -ne 0 -or ($creset -band 1) -ne 1 -or
        ($creset -band 0xFFFFFFFE) -lt $APP_START -or ($creset -band 0xFFFFFFFE) -ge ($APP_START + $APP_LEN))
    $currentSha = Sha256Hex $current
    $label = 'unknown'
    foreach ($k in $listed.Keys) { if ($listed[$k] -eq $currentSha -and $k -like '*.bin') { $label = [IO.Path]::GetFileNameWithoutExtension($k) } }
    if ($installedOk) {
        Write-Host ("CHECK: installed app is a valid H420X application: {0} ({1})." -f $label, $currentSha.Substring(0, 8).ToLower()) -ForegroundColor Green
    } else {
        Write-Host ("CHECK: supported tablet, but its installed app is DAMAGED (bad vector table, SHA-256 {0})." -f $currentSha) -ForegroundColor Red
        if (-not ($BackupOnly -or $isBackup -or ($name -eq 'stock.bin'))) {
            Fail 'Only a backup restore (choice R) or stock.bin may be installed over a damaged app. Anything else is blocked. Nothing was changed.'
        }
        Write-Host "Recovery path: only a verified backup or stock.bin will be installed." -ForegroundColor Yellow
    }
    $backupDir = Join-Path $Here 'backups'; New-Item -ItemType Directory -Force $backupDir | Out-Null
    $stamp = Get-Date -Format 'yyyy-MM-dd_HH-mm-ss'
    # $currentSha / $label were set by the check above; reuse them for the backup name.
    $backupFile = Join-Path $backupDir ("backup_{0}_{1}_{2}.bin" -f $stamp, $label, $currentSha.Substring(0, 8).ToLower())
    [System.IO.File]::WriteAllBytes($backupFile, $current)
    if ((Sha256Hex ([System.IO.File]::ReadAllBytes($backupFile))) -ne $currentSha) { Fail 'The backup file did not save correctly. Nothing was changed.' }
    Write-Host "Current firmware ($label) saved: $backupFile" -ForegroundColor Green
    if ($BackupOnly) {
        Write-Host ""
        Write-Host "  DONE - backup saved, nothing on the tablet was changed." -ForegroundColor Green
        Write-Host "  Unplug the tablet and plug it back in to use it normally."
        exit 0
    }

    # ---------- 5. erase, write, verify ----------
    Write-Host ""
    Write-Host "  Installing $name - DO NOT UNPLUG THE TABLET (about 10-20 seconds)..." -ForegroundColor Yellow
    # Last gate before erasing: the same tablet must still be connected, with the
    # same bootloader and the same app bytes just backed up. Anything else means
    # the tablet was swapped or disturbed mid-run.
    $reboot = Read-Range $handle $BOOT_START $BOOT_LEN
    if ((Sha256Hex $reboot) -ne $BOOT_SHA256) { throw 'tablet changed since backup (bootloader). Run flash.bat again.' }
    $reapp = Read-Range $handle $APP_START $APP_LEN
    if (-not (Same $reapp $current)) { throw 'tablet changed since backup (application). Run flash.bat again.' }
    Write-Host "  Same tablet re-verified; erasing and writing now."
    $erased = $true
    for ($a = $APP_START; $a -lt ($APP_START + $APP_LEN); $a += $PAGE) { Erase-Page $handle $a }
    for ($off = 0; $off -lt $APP_LEN; $off += 1024) {
        $n = [Math]::Min(1024, $APP_LEN - $off)
        $chunk = New-Object byte[] $n; [Array]::Copy($image, $off, $chunk, 0, $n)
        Write-Block $handle ($APP_START + $off) $chunk
    }
    $readback = Read-Range $handle $APP_START $APP_LEN
    if (-not (Same $readback $image)) { throw 'readback does not match the file' }
    if ((Sha256Hex (Read-Range $handle $BOOT_START $BOOT_LEN)) -ne $BOOT_SHA256) { throw 'bootloader changed' }
    Write-Host ""
    Write-Host "  DONE - every byte verified." -ForegroundColor Green
    Write-Host "  Unplug the tablet and plug it back in to start $name."
}
catch {
    if ($erased) {
        Write-Host ""
        Write-Host "FAILED during install: $($_.Exception.Message)" -ForegroundColor Red
        Write-Host "Nothing is broken: the tablet's bootloader is never touched, so it can always be fixed."
        Write-Host "Run flash.bat again and install any firmware, or choose R to restore your backup."
        exit 1
    }
    Fail $_.Exception.Message
}
finally { [void][H420xDfu]::GDDFU_DeviceClose($handle) }
