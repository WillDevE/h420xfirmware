# H420X clean-room firmware build (PowerShell, no make/cmake required).
#
# Usage:
#   powershell -ExecutionPolicy Bypass -File .\build.ps1
#   powershell -ExecutionPolicy Bypass -File .\build.ps1 -Clean
#
# Usage:
#   powershell -ExecutionPolicy Bypass -File .\build.ps1 -Profile m0 -Clean
#   powershell -ExecutionPolicy Bypass -File .\build.ps1 -Profile rhythm900 -Clean
#   powershell -ExecutionPolicy Bypass -File .\build.ps1 -Profile pen500 [-FastPressure]
#   ... -Profile rhythm900 [-Telemetry] [-Lab] [-Smooth] [-Robust] [-NoCal] [-EarlyRecenter] [-EdgeExt] [-AdcStock] [-AddrClear] [-Pa15Fast] [-PinsStock] [-Spectrum] [-LedHold] [-Anchor] [-ReadGap <cycles>] [-Modes | -GapModes | -ConvModes | -JitterModes | -LoopMode <0..7> [-LoopWarm | -LoopArm] [-Diag18]] [-SysInit]
#
# Outputs in .\build (prefix h420x_<profile>):
#   .elf        ELF image with symbols
#   .map        linker map
#   .lst        disassembly listing
#   _raw.bin    exact loadable image (short)
#   _app.bin    32,000-byte DFU payload, 0xFF padded
#   .sha256     SHA-256 of the DFU payload
#
# SPDX-License-Identifier: GPL-3.0-or-later

[CmdletBinding()]
param(
    [switch]$Clean,
    [switch]$Diag,
    [switch]$Telemetry,
    [switch]$FastPressure,
    [switch]$SysInit,
    [switch]$Smooth,
    [switch]$Robust,
    [switch]$NoCal,
    [switch]$Lab,
    [switch]$EarlyRecenter,
    [switch]$EdgeExt,
    [switch]$AdcStock,
    [switch]$AddrClear,
    [switch]$Pa15Fast,
    [switch]$PinsStock,
    [switch]$Spectrum,
    [switch]$LedHold,
    [switch]$Anchor,
    [int]$ReadGap = 0,
    [switch]$Modes,
    [switch]$SamptModes,
    [switch]$FineModes,
    [switch]$RateModes,
    [switch]$Scan1kModes,
    [switch]$OrderModes,
    [switch]$SettleModes,
    [switch]$YPrepModes,
    [switch]$GapModes,
    [switch]$ConvModes,
    [switch]$JitterModes,
    [switch]$AxisGapModes,
    [int]$LoopMode = -1,
    [switch]$LoopWarm,
    [switch]$LoopArm,
    [switch]$Diag18,
    [switch]$NoUartLog,
    [switch]$Release,
    [switch]$StockIdentity,
    [switch]$FrameAux,
    [switch]$WaveAux,
    [switch]$PhaseModes,
    [switch]$LateConnect,
    [switch]$QuietHeartbeat,
    [int]$BcdDevice = 0,
    [int]$SerialNumber = 0,
    [int]$CadReset = 0,
    [int]$FrameFloorUs = 0,
    [int]$ConvBudget = 0,
    [int]$Pa2Cyc = 0,
    [switch]$SofLock,
    [switch]$KeepAlive,
    [switch]$FastEp1,
    [int]$SofLeadUs = -1,
    [switch]$InLock,
    [int]$InEpsUs = -1,
    [switch]$R55Modes,
    [switch]$R56Modes,
    [switch]$R57Modes,
    [switch]$R58Modes,
    [switch]$R59Modes,
    [switch]$R60Modes,
    [switch]$R61Modes,
    [switch]$R62Modes,
    [switch]$R63Modes,
    [switch]$NoR55Aux,
    [int]$DigitizerInterval = 0,
    [ValidateSet('m0', 'rhythm750', 'rhythm900', 'pen500')]
    [string]$Profile = 'm0'
)

# Round 49 table switches are their own -Scan1kModes tables; imply the base
# switch so the profile, AUX and CadReset validation below still applies.
if ($GapModes -or $ConvModes -or $JitterModes -or $AxisGapModes) { $Scan1kModes = $true }
# Round 56 is built on the round 55 machinery (grid, exact landings, R55 aux): imply it, so the
# -R55Modes validation below applies; the file name carries only _r56modes.
if ($R63Modes) { $R60Modes = $true }   # round 63: deterministic handoff, R60 table format, no R61 scale / learner code
if ($R62Modes) { $R61Modes = $true }   # round 62 reuses the round 61 scale / learner machinery
if ($R61Modes) { $R60Modes = $true }   # round 61 builds on round 60 (per-mode Y cubic weight) and everything below
if ($R60Modes) { $R59Modes = $true }   # round 60 builds on round 59 (28-pulse R139 reads) and everything below
if ($R59Modes) { $R58Modes = $true }   # round 59 builds on round 58 (relative grids) and everything below
if ($R58Modes) { $R57Modes = $true }   # round 58 builds on round 57 (grid sizes) and everything below
if ($R57Modes) { $R56Modes = $true }   # round 57 builds on round 56 (lead) and round 55 (grid, exact)
if ($R56Modes) { $R55Modes = $true }

if ($FastPressure -and $Profile -ne 'pen500') { throw '-FastPressure requires -Profile pen500' }
if (($Modes -or $SamptModes -or $FineModes -or $RateModes -or $Scan1kModes -or $OrderModes -or $SettleModes -or $YPrepModes -or $LoopMode -ge 0) -and ($Profile -ne 'rhythm900' -or $Telemetry)) { throw '-Modes / -SamptModes / -FineModes / -RateModes / -Scan1kModes / -OrderModes / -SettleModes / -YPrepModes / -LoopMode require -Profile rhythm900 without -Telemetry' }
if ($Modes -and $LoopMode -ge 0) { throw 'use either -Modes (cycle all loop modes) or -LoopMode <n> (one fixed mode)' }
if ($SamptModes -and $Modes) { throw 'use either -Modes (e table) or -SamptModes (sample-time table)' }
if ($FineModes -and ($Modes -or $SamptModes)) { throw 'use either -FineModes (hyst/e table) or -Modes/-SamptModes' }
if ($RateModes -and ($Modes -or $SamptModes -or $FineModes)) { throw 'use either -RateModes (800 Hz table) or -Modes/-SamptModes/-FineModes' }
if ($Scan1kModes -and ($Modes -or $SamptModes -or $FineModes -or $RateModes)) { throw 'use either -Scan1kModes (1 kHz table) or -Modes/-SamptModes/-FineModes/-RateModes' }
if ($OrderModes -and -not $Scan1kModes) { throw '-OrderModes requires -Scan1kModes' }
if ($SettleModes -and -not $Scan1kModes) { throw '-SettleModes requires -Scan1kModes' }
if ($OrderModes -and $SettleModes) { throw 'use either -OrderModes or -SettleModes, not both' }
if ($YPrepModes -and -not $Scan1kModes) { throw '-YPrepModes requires -Scan1kModes' }
if ($YPrepModes -and ($OrderModes -or $SettleModes -or $Modes -or $SamptModes -or $FineModes -or $RateModes -or $PhaseModes)) { throw '-YPrepModes uses its own -Scan1kModes table; do not combine with -OrderModes/-SettleModes/-Modes/-SamptModes/-FineModes/-RateModes/-PhaseModes' }
if ($OrderModes -and ($Modes -or $SamptModes -or $FineModes -or $RateModes -or $PhaseModes)) { throw '-OrderModes uses its own -Scan1kModes table; do not combine with -Modes/-SamptModes/-FineModes/-RateModes/-PhaseModes' }
if ($SettleModes -and ($Modes -or $SamptModes -or $FineModes -or $RateModes -or $PhaseModes)) { throw '-SettleModes uses its own -Scan1kModes table; do not combine with -Modes/-SamptModes/-FineModes/-RateModes/-PhaseModes' }
$round49Count = 0
if ($GapModes) { $round49Count++ }
if ($ConvModes) { $round49Count++ }
if ($JitterModes) { $round49Count++ }
if ($AxisGapModes) { $round49Count++ }
if ($round49Count -gt 1) { throw 'use only one of -GapModes / -ConvModes / -JitterModes / -AxisGapModes' }
if ($round49Count -eq 1 -and ($OrderModes -or $SettleModes -or $YPrepModes -or $PhaseModes -or $Modes -or $SamptModes -or $FineModes -or $RateModes)) { throw '-GapModes/-ConvModes/-JitterModes/-AxisGapModes use their own -Scan1kModes table; do not combine with other mode tables' }
if ($LoopMode -gt 7) { throw '-LoopMode must be 0..7' }
if ($LoopWarm -and $LoopMode -lt 0) { throw '-LoopWarm requires -LoopMode <n>' }
if ($LoopArm -and $LoopMode -lt 0) { throw '-LoopArm requires -LoopMode <n>' }
if ($LoopArm -and $LoopWarm) { throw 'use -LoopArm or -LoopWarm, not both' }
if ($Diag18 -and -not ($Modes -or $SamptModes -or $FineModes -or $RateModes -or $Scan1kModes -or $OrderModes -or $SettleModes -or $YPrepModes) -and $LoopMode -lt 0) { throw '-Diag18 requires -Modes, -SamptModes, -FineModes, -RateModes, -Scan1kModes, -OrderModes, -SettleModes or -LoopMode <n>' }
if ($Release -and $Diag18) { throw 'use -Release or -Diag18, not both' }
if ($Release -and -not ($Modes -or $SamptModes -or $FineModes -or $RateModes -or $Scan1kModes -or $OrderModes -or $SettleModes -or $YPrepModes) -and $LoopMode -lt 0) { throw '-Release requires -Modes, -SamptModes, -FineModes, -RateModes, -Scan1kModes, -OrderModes, -SettleModes or -LoopMode <n>' }
if ($FrameAux -and -not ($Modes -or $SamptModes -or $FineModes -or $RateModes -or $Scan1kModes -or $OrderModes -or $SettleModes -or $YPrepModes) -and $LoopMode -lt 0) { throw '-FrameAux requires -Modes, -SamptModes, -FineModes, -RateModes, -Scan1kModes, -OrderModes, -SettleModes or -LoopMode <n>' }
if ($WaveAux -and -not $Release) { throw '-WaveAux requires -Release' }
if ($PhaseModes -and -not $Scan1kModes) { throw '-PhaseModes requires -Scan1kModes' }
if ($BcdDevice -ne 0 -and ($BcdDevice -lt 0x0112 -or $BcdDevice -gt 0xFFFF)) { throw '-BcdDevice must be 0x0112..0xFFFF' }
if ($SerialNumber -ne 0 -and -not $StockIdentity) { throw '-SerialNumber requires -StockIdentity' }
if ($SerialNumber -ne 0 -and ($SerialNumber -lt 1 -or $SerialNumber -gt 99999999)) { throw '-SerialNumber must be 1..99999999' }
if ($CadReset -gt 0 -and ($CadReset -lt 1 -or $CadReset -gt 4096)) { throw '-CadReset must be 1..4096 frames' }
if ($CadReset -gt 0 -and -not ($Modes -or $SamptModes -or $FineModes -or $RateModes -or $Scan1kModes -or $OrderModes -or $SettleModes -or $YPrepModes) -and $LoopMode -lt 0) { throw '-CadReset requires -Modes, -SamptModes, -FineModes, -RateModes, -Scan1kModes, -OrderModes, -SettleModes or -LoopMode <n>' }
if ($FrameFloorUs -ne 0 -and ($FrameFloorUs -lt 900 -or $FrameFloorUs -gt 2000)) { throw '-FrameFloorUs must be 900..2000 us' }
if ($ConvBudget -gt 0 -and ($ConvBudget -lt 264 -or $ConvBudget -gt 440)) { throw '-ConvBudget must be 264..440 cycles' }
if ($Pa2Cyc -ne 0 -and ($Pa2Cyc -lt 468 -or $Pa2Cyc -gt 600)) { throw '-Pa2Cyc must be 468..600 cycles (sensing_set_loop_pa2 silently replaces anything below 468 with the 756-cycle default; the R52 pa2_440/pa2_400 images ran at 756)' }
if ($DigitizerInterval -ne 0 -and ($DigitizerInterval -lt 1 -or $DigitizerInterval -gt 16)) { throw '-DigitizerInterval must be 1..16' }
if ($FastEp1 -and $KeepAlive) { throw '-FastEp1 and -KeepAlive cannot be combined' }
if ($SofLeadUs -ge 0 -and -not $SofLock) { throw '-SofLeadUs requires -SofLock' }
if ($SofLeadUs -gt 50) { throw '-SofLeadUs must be 0..50 us' }
if ($InLock -and $SofLock) { throw '-InLock and -SofLock cannot be combined' }
if ($InLock -and $LoopMode -lt 0) { throw '-InLock requires -LoopMode <n>' }
if ($InEpsUs -ge 0 -and -not $InLock) { throw '-InEpsUs requires -InLock' }
if ($InEpsUs -gt 50) { throw '-InEpsUs must be 0..50 us' }
if ($R55Modes -and -not ($AxisGapModes -and $FastEp1)) { throw '-R55Modes requires -AxisGapModes and -FastEp1' }
if ($R55Modes -and ($InLock -or $SofLock -or $FrameFloorUs -ne 0 -or $FrameAux -or $WaveAux -or $Diag18 -or $PhaseModes)) { throw '-R55Modes sets the lock and frame floor per mode and has its own aux; do not combine with -InLock/-SofLock/-FrameFloorUs/-FrameAux/-WaveAux/-Diag18/-PhaseModes' }
if ($NoR55Aux -and -not ($R55Modes -and $Release)) { throw '-NoR55Aux requires -R55Modes (or -R56Modes) and -Release (the aux falls through to the release zero)' }

# Persistent automatic-pickup rule: stock-identity images keep the stable stock
# serial unless a deliberate identity test overrides it. Windows/OTD bind the
# device by serial, so a per-candidate serial makes every flash look like a new
# tablet and breaks automatic pickup.
if ($StockIdentity -and $SerialNumber -eq 0) { $SerialNumber = 20210233 }

$ErrorActionPreference = 'Stop'

$FirmwareRoot = $PSScriptRoot
$SrcDir       = Join-Path $FirmwareRoot 'src'
$TusbDir      = Join-Path $FirmwareRoot 'third_party\tinyusb'
$BuildDir     = Join-Path $FirmwareRoot 'build'
$ProfileTag   = $Profile + $(if ($Diag) { '-diag' } else { '' }) + $(if ($Telemetry) { '-telemetry' } else { '' }) + $(if ($FastPressure) { '-fastp' } else { '' }) + $(if ($SysInit) { '-sysinit' } else { '' }) + $(if ($Smooth) { '-smooth' } else { '' }) + $(if ($Robust) { '-robust' } else { '' }) + $(if ($NoCal) { '-nocal' } else { '' }) + $(if ($Lab) { '-lab' } else { '' }) + $(if ($EarlyRecenter) { '-early' } else { '' }) + $(if ($EdgeExt) { '-edge' } else { '' }) + $(if ($AdcStock) { '-adcstock' } else { '' }) + $(if ($AddrClear) { '-addrclear' } else { '' }) + $(if ($Pa15Fast) { '-pa15fast' } else { '' }) + $(if ($PinsStock) { '-pinsstock' } else { '' }) + $(if ($Spectrum) { '-spectrum' } else { '' }) + $(if ($LedHold) { '-ledhold' } else { '' }) + $(if ($Anchor) { '-anchor' } else { '' }) + $(if ($ReadGap -gt 0) { '-gap' + $ReadGap } else { '' }) + $(if ($Scan1kModes) { '-scan1k' } elseif ($RateModes) { '-rate800' } elseif ($FineModes) { '-fine' } elseif ($SamptModes) { '-sampt' } elseif ($Modes) { '-modes26' } else { '' }) + $(if ($LoopMode -ge 0) { if ($Scan1kModes) { '-scan1kmode' + $LoopMode } elseif ($RateModes) { '-rate800mode' + $LoopMode } elseif ($FineModes) { '-finemode' + $LoopMode } elseif ($SamptModes) { '-samptmode' + $LoopMode } else { '-r26mode' + $LoopMode } } else { '' }) + $(if ($LoopWarm) { '-warm' } else { '' }) + $(if ($LoopArm) { '-arm' } else { '' }) + $(if ($Diag18) { '-diag18' } else { '' }) + $(if ($NoUartLog) { '-nouart' } else { '' }) + $(if ($Release) { '-release' } else { '' }) + $(if ($StockIdentity) { '-stockid' } else { '' }) + $(if ($FrameAux) { '-frameaux' } else { '' }) + $(if ($WaveAux) { '-waveaux' } else { '' }) + $(if ($PhaseModes) { '-phase' } else { '' }) + $(if ($OrderModes) { '-order' } else { '' }) + $(if ($SettleModes) { '-settle' } else { '' }) + $(if ($YPrepModes) { '-yprep' } else { '' }) + $(if ($GapModes) { '-gapmodes' } else { '' }) + $(if ($ConvModes) { '-convmodes' } else { '' }) + $(if ($JitterModes) { '-jitmodes' } else { '' }) + $(if ($AxisGapModes) { '-axisgapmodes' } else { '' }) + $(if ($LateConnect) { '-lateconnect' } else { '' }) + $(if ($QuietHeartbeat) { '-quiet' } else { '' }) + $(if ($BcdDevice -ne 0) { '-bcd' + ('{0:X4}' -f $BcdDevice) } else { '' }) + $(if ($SerialNumber -ne 0) { '-sn' + $SerialNumber } else { '' }) + $(if ($CadReset -gt 0) { '-cadreset' + $CadReset } else { '' }) + $(if ($FrameFloorUs -ne 0) { '-ff' + $FrameFloorUs } else { '' }) + $(if ($ConvBudget -gt 0) { '-cb' + $ConvBudget } else { '' })
if ($Pa2Cyc -gt 0) { $ProfileTag += '_pa2' + $Pa2Cyc }
if ($SofLock) { $ProfileTag += '_sof' }
if ($KeepAlive) { $ProfileTag += '_keepalive' }
if ($FastEp1) { $ProfileTag += '_fastep1' }
if ($SofLeadUs -ge 0) { $ProfileTag += '_sl' + $SofLeadUs }
if ($InLock) { $ProfileTag += '_in' }
if ($InEpsUs -ge 0) { $ProfileTag += '_ie' + $InEpsUs }
if ($DigitizerInterval -gt 0) { $ProfileTag += '_dig' + $DigitizerInterval }
if ($R63Modes) { $ProfileTag += '_r63modes' } elseif ($R62Modes) { $ProfileTag += '_r62modes' } elseif ($R61Modes) { $ProfileTag += '_r61modes' } elseif ($R60Modes) { $ProfileTag += '_r60modes' } elseif ($R59Modes) { $ProfileTag += '_r59modes' } elseif ($R58Modes) { $ProfileTag += '_r58modes' } elseif ($R57Modes) { $ProfileTag += '_r57modes' } elseif ($R56Modes) { $ProfileTag += '_r56modes' } elseif ($R55Modes) { $ProfileTag += '_r55modes' }
if ($NoR55Aux) { $ProfileTag += '_noaux' }
$ObjDir       = Join-Path $BuildDir ("obj\" + $ProfileTag)
$LdScript     = Join-Path $FirmwareRoot 'ld\h420x_app.ld'

switch ($Profile) {
    'm0'        { $ProfileNum = 0; $Prefix = 'h420x_m0' }
    'rhythm750' { $ProfileNum = 1; $Prefix = 'h420x_rhythm750' }
    'rhythm900' { $ProfileNum = 2; $Prefix = 'h420x_rhythm900' }
    'pen500'    { $ProfileNum = 3; $Prefix = 'h420x_pen500' }
}
if ($Diag) { $Prefix = $Prefix + '_diag' }
if ($Telemetry) { $Prefix = $Prefix + '_telemetry' }
if ($FastPressure) { $Prefix = $Prefix + '_fastp' }
if ($SysInit) { $Prefix = $Prefix + '_sysinit' }
if ($Smooth) { $Prefix = $Prefix + '_smooth' }
if ($Robust) { $Prefix = $Prefix + '_robust' }
if ($NoCal) { $Prefix = $Prefix + '_nocal' }
if ($Lab) { $Prefix = $Prefix + '_lab' }
if ($EarlyRecenter) { $Prefix = $Prefix + '_early' }
if ($EdgeExt) { $Prefix = $Prefix + '_edge' }
if ($AdcStock) { $Prefix = $Prefix + '_adcstock' }
if ($AddrClear) { $Prefix = $Prefix + '_addrclear' }
if ($Pa15Fast) { $Prefix = $Prefix + '_pa15fast' }
if ($PinsStock) { $Prefix = $Prefix + '_pinsstock' }
if ($Spectrum) { $Prefix = $Prefix + '_spectrum' }
if ($LedHold) { $Prefix = $Prefix + '_ledhold' }
if ($Anchor) { $Prefix = $Prefix + '_anchor' }
if ($ReadGap -gt 0) { $Prefix = $Prefix + '_gap' + $ReadGap }
if ($Scan1kModes) { $Prefix = $Prefix + '_scan1k' } elseif ($RateModes) { $Prefix = $Prefix + '_rate800' } elseif ($FineModes) { $Prefix = $Prefix + '_fine' } elseif ($SamptModes) { $Prefix = $Prefix + '_sampt' } elseif ($Modes) { $Prefix = $Prefix + '_modes26' }
if ($LoopMode -ge 0) { if ($Scan1kModes) { $Prefix = $Prefix + '_scan1kmode' + $LoopMode } elseif ($RateModes) { $Prefix = $Prefix + '_rate800mode' + $LoopMode } elseif ($FineModes) { $Prefix = $Prefix + '_finemode' + $LoopMode } elseif ($SamptModes) { $Prefix = $Prefix + '_samptmode' + $LoopMode } else { $Prefix = $Prefix + '_r26mode' + $LoopMode } }
if ($LoopWarm) { $Prefix = $Prefix + '_warm' }
if ($LoopArm) { $Prefix = $Prefix + '_arm' }
if ($Diag18) { $Prefix = $Prefix + '_diag18' }
if ($NoUartLog) { $Prefix = $Prefix + '_nouart' }
if ($Release) { $Prefix = $Prefix + '_release' }
if ($StockIdentity) { $Prefix = $Prefix + '_stockid' }
if ($FrameAux) { $Prefix = $Prefix + '_frameaux' }
if ($WaveAux) { $Prefix = $Prefix + '_waveaux' }
if ($PhaseModes) { $Prefix = $Prefix + '_phase' }
if ($OrderModes) { $Prefix = $Prefix + '_order' }
if ($SettleModes) { $Prefix = $Prefix + '_settle' }
if ($YPrepModes) { $Prefix = $Prefix + '_yprep' }
if ($GapModes) { $Prefix = $Prefix + '_gapmodes' }
if ($ConvModes) { $Prefix = $Prefix + '_convmodes' }
if ($JitterModes) { $Prefix = $Prefix + '_jitmodes' }
if ($AxisGapModes) { $Prefix = $Prefix + '_axisgapmodes' }
if ($LateConnect) { $Prefix = $Prefix + '_lateconnect' }
if ($QuietHeartbeat) { $Prefix = $Prefix + '_quiet' }
if ($BcdDevice -ne 0) { $Prefix = $Prefix + '_bcd' + ('{0:X4}' -f $BcdDevice) }
if ($SerialNumber -ne 0) { $Prefix = $Prefix + '_sn' + $SerialNumber }
if ($CadReset -gt 0) { $Prefix = $Prefix + '_cadreset' + $CadReset }
if ($FrameFloorUs -ne 0) { $Prefix = $Prefix + '_ff' + $FrameFloorUs }
if ($ConvBudget -gt 0) { $Prefix = $Prefix + '_cb' + $ConvBudget }
if ($Pa2Cyc -gt 0) { $Prefix = $Prefix + '_pa2' + $Pa2Cyc }
if ($SofLock) { $Prefix = $Prefix + '_sof' }
if ($KeepAlive) { $Prefix = $Prefix + '_keepalive' }
if ($FastEp1) { $Prefix = $Prefix + '_fastep1' }
if ($SofLeadUs -ge 0) { $Prefix = $Prefix + '_sl' + $SofLeadUs }
if ($InLock) { $Prefix = $Prefix + '_in' }
if ($InEpsUs -ge 0) { $Prefix = $Prefix + '_ie' + $InEpsUs }
if ($DigitizerInterval -gt 0) { $Prefix = $Prefix + '_dig' + $DigitizerInterval }
if ($R63Modes) { $Prefix = $Prefix + '_r63modes' } elseif ($R62Modes) { $Prefix = $Prefix + '_r62modes' } elseif ($R61Modes) { $Prefix = $Prefix + '_r61modes' } elseif ($R60Modes) { $Prefix = $Prefix + '_r60modes' } elseif ($R59Modes) { $Prefix = $Prefix + '_r59modes' } elseif ($R58Modes) { $Prefix = $Prefix + '_r58modes' } elseif ($R57Modes) { $Prefix = $Prefix + '_r57modes' } elseif ($R56Modes) { $Prefix = $Prefix + '_r56modes' } elseif ($R55Modes) { $Prefix = $Prefix + '_r55modes' }
if ($NoR55Aux) { $Prefix = $Prefix + '_noaux' }

$AppBinSize   = 32000          # 0x7D00, the stock DFU application window
$AppFlashBase = 0x08004000
$AppFlashEnd  = 0x0800BD00

function Find-Tool {
    param([string]$Name, [string]$Fallback)
    $cmd = Get-Command $Name -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    if (Test-Path $Fallback) { return $Fallback }
    throw "Required tool '$Name' not found. Install the Arm GNU Toolchain and add its bin directory to PATH."
}

$Gcc     = Find-Tool 'arm-none-eabi-gcc'     'C:\Program Files (x86)\Arm GNU Toolchain arm-none-eabi\12.2 mpacbti-rel1\bin\arm-none-eabi-gcc.exe'
$Objcopy = Find-Tool 'arm-none-eabi-objcopy' (Join-Path (Split-Path $Gcc) 'arm-none-eabi-objcopy.exe')
$Size    = Find-Tool 'arm-none-eabi-size'    (Join-Path (Split-Path $Gcc) 'arm-none-eabi-size.exe')
$Objdump = Find-Tool 'arm-none-eabi-objdump' (Join-Path (Split-Path $Gcc) 'arm-none-eabi-objdump.exe')
$Nm      = Find-Tool 'arm-none-eabi-nm'      (Join-Path (Split-Path $Gcc) 'arm-none-eabi-nm.exe')

if ($Clean -and (Test-Path $BuildDir)) {
    Remove-Item -Recurse -Force $BuildDir
}
New-Item -ItemType Directory -Force -Path $ObjDir | Out-Null

# ---------------------------------------------------------------------------
# Source list
# ---------------------------------------------------------------------------
$AppSources = @(Get-ChildItem -Path $SrcDir -Filter '*.c' -File | Sort-Object Name)

$TusbSources = @(
    (Join-Path $TusbDir 'src\tusb.c'),
    (Join-Path $TusbDir 'src\common\tusb_fifo.c'),
    (Join-Path $TusbDir 'src\device\usbd.c'),
    (Join-Path $TusbDir 'src\class\hid\hid_device.c'),
    (Join-Path $TusbDir 'src\portable\synopsys\dwc2\dcd_dwc2.c'),
    (Join-Path $TusbDir 'src\portable\synopsys\dwc2\dwc2_common.c')
)

$Sources = @($AppSources.FullName) + $TusbSources

foreach ($s in $Sources) {
    if (-not (Test-Path $s)) { throw "Missing source: $s" }
}

# ---------------------------------------------------------------------------
# Compile
# ---------------------------------------------------------------------------
$CommonFlags = @(
    '-mcpu=cortex-m4',
    '-mthumb',
    '-mfloat-abi=soft',
    '-std=gnu11',
    '-ffreestanding',
    '-fno-builtin',
    '-fno-common',
    '-ffunction-sections',
    '-fdata-sections',
    '-fno-strict-aliasing',
    '-Os',
    '-g3',
    '-gdwarf-4',
    "-DH420X_PROFILE=$ProfileNum",
    $(if ($Diag) { '-DSENSING_DIAG=1' } else { '-DSENSING_DIAG=0' }),
    $(if ($Telemetry) { '-DSENSING_TELEMETRY=1' } else { '-DSENSING_TELEMETRY=0' }),
    '-Wall',
    '-Wextra',
    '-Wno-unused-parameter',
    '-Wno-unused-const-variable'
)
if ($FastPressure) { $CommonFlags += '-DPEN_PROBE_MASK=0' }
if ($SysInit) { $CommonFlags += '-DH420X_SYSINIT_PARITY=1' }
if ($Smooth) { $CommonFlags += '-DOUTPUT_SMOOTH=1' }
if ($Robust) { $CommonFlags += '-DROBUST_LOCK=1' }
if ($NoCal) { $CommonFlags += '-DADC_NO_CALIBRATE=1' }
if ($Lab) { $CommonFlags += '-DLAB_ENABLE=1' }
if ($EarlyRecenter) { $CommonFlags += '-DTRACK_DEFER_SHIFT=5U' }
if ($EdgeExt) { $CommonFlags += '-DTRACK_EDGE_EXT=1' }
if ($AdcStock) { $CommonFlags += '-DSENSING_ADC_STOCK=1' }
if ($AddrClear) { $CommonFlags += '-DSENSING_ADDR_CLEAR=1' }
if ($Pa15Fast) { $CommonFlags += '-DSENSING_PA15_FAST=1' }
if ($PinsStock) { $CommonFlags += '-DSENSING_PINS_STOCK=1' }
if ($Spectrum) { $CommonFlags += '-DLAB_SPECTRUM=1' }
if ($LedHold) { $CommonFlags += '-DSENSING_LED_HOLD=1' }
if ($Anchor) { $CommonFlags += '-DSENSING_ANCHOR=1' }
if ($ReadGap -gt 0) { $CommonFlags += ('-DREAD_GAP_CYC_OVERRIDE=' + $ReadGap + 'U') }
if ($Modes -or $SamptModes -or $FineModes -or $RateModes -or $Scan1kModes) { $CommonFlags += '-DLOOP_MODES=1' }
if ($SamptModes -or $FineModes -or $RateModes -or $Scan1kModes) { $CommonFlags += '-DSAMPT_MODES=1' }
if ($FineModes) { $CommonFlags += '-DFINE_MODES=1' }
if ($RateModes) { $CommonFlags += '-DRATE_MODES=1' }
if ($Scan1kModes) { $CommonFlags += '-DSCAN1K_MODES=1' }
if ($LoopMode -ge 0) { $CommonFlags += '-DLOOP_MODES=1'; $CommonFlags += ('-DLOOP_MODE_FIXED=' + $LoopMode + 'U') }
if ($LoopWarm) { $CommonFlags += '-DLOOP_WARM=1' }
if ($LoopArm) { $CommonFlags += '-DLOOP_ARM=1' }
if ($Diag18) { $CommonFlags += '-DLOOP_DIAG=1' }
if ($NoUartLog) { $CommonFlags += '-DNO_UART_LOG=1' }
if ($Release) { $CommonFlags += '-DLOOP_HEARTBEAT=1'; $CommonFlags += '-DRELEASE_AUX_ZERO=1'; $CommonFlags += '-DNO_UART_LOG=1' }
if ($StockIdentity) { $CommonFlags += '-DSTOCK_IDENTITY=1' }
if ($FrameAux) { $CommonFlags += '-DFRAME_AUX=1' }
if ($WaveAux) { $CommonFlags += '-DWAVE_AUX=1' }
if ($PhaseModes) { $CommonFlags += '-DPHASE_MODES=1' }
if ($OrderModes) { $CommonFlags += '-DORDER_MODES=1' }
if ($SettleModes) { $CommonFlags += '-DSETTLE_MODES=1' }
if ($YPrepModes) { $CommonFlags += '-DORDER_MODES=1'; $CommonFlags += '-DYPREP_MODES=1' }
if ($GapModes) { $CommonFlags += '-DORDER_MODES=1'; $CommonFlags += '-DGAP_MODES=1' }
if ($ConvModes) { $CommonFlags += '-DORDER_MODES=1'; $CommonFlags += '-DCONV_MODES=1' }
if ($JitterModes) { $CommonFlags += '-DORDER_MODES=1'; $CommonFlags += '-DJITTER_MODES=1' }
if ($AxisGapModes) { $CommonFlags += '-DORDER_MODES=1'; $CommonFlags += '-DAXIS_GAP_MODES=1' }
if ($LateConnect) { $CommonFlags += '-DLATE_CONNECT=1' }
if ($QuietHeartbeat) { $CommonFlags += '-DQUIET_HEARTBEAT=1' }
if ($BcdDevice -ne 0) { $CommonFlags += ('-DBCD_DEVICE=0x{0:X4}' -f $BcdDevice) }
if ($SerialNumber -ne 0) { $CommonFlags += ('-DSERIAL_NUMBER=' + $SerialNumber) }
if ($CadReset -gt 0) { $CommonFlags += ('-DCAD_RESET_FRAMES=' + $CadReset + 'U') }
if ($FrameFloorUs -ne 0) { $CommonFlags += ('-DFRAME_FLOOR_US=' + $FrameFloorUs + 'U') }
if ($ConvBudget -gt 0) { $CommonFlags += ('-DCAD_CONV_BUDGET_CYC=' + $ConvBudget + 'U') }
if ($Pa2Cyc -gt 0) { $CommonFlags += ('-DPA2_MODE1_CYC=' + $Pa2Cyc + 'U') }
if ($SofLock) { $CommonFlags += '-DMODE1_SOF=1' }
if ($KeepAlive) { $CommonFlags += '-DREPORT_KEEPALIVE=1' }
if ($FastEp1) { $CommonFlags += '-DFAST_EP1=1' }
if ($SofLeadUs -ge 0) { $CommonFlags += ('-DSOF_LEAD_US=' + $SofLeadUs + 'UL') }
if ($InLock) { $CommonFlags += '-DMODE1_IN=1' }
if ($InEpsUs -ge 0) { $CommonFlags += ('-DIN_EPS_US=' + $InEpsUs + 'UL') }
if ($DigitizerInterval -gt 0) { $CommonFlags += ('-DDIGITIZER_BINTERVAL=' + $DigitizerInterval + 'U') }
if ($R55Modes) { $CommonFlags += '-DR55_MODES=1' }
if ($R56Modes) { $CommonFlags += '-DR56_MODES=1' }
if ($R57Modes) { $CommonFlags += '-DR57_MODES=1' }
if ($R58Modes) { $CommonFlags += '-DR58_MODES=1' }
if ($R59Modes) { $CommonFlags += '-DR59_MODES=1' }
if ($R60Modes) { $CommonFlags += '-DR60_MODES=1' }
if ($R61Modes) { $CommonFlags += '-DR61_MODES=1' }
if ($R62Modes) { $CommonFlags += '-DR62_MODES=1' }
if ($R63Modes) { $CommonFlags += '-DR63_MODES=1' }
if ($NoR55Aux) { $CommonFlags += '-DR55_AUX=0' }

$IncludeFlags = @(
    '-I', $SrcDir,
    '-I', (Join-Path $TusbDir 'src')
)

function Compile-One {
    param([string]$Source)

    $relative = $Source.Substring($FirmwareRoot.Length).TrimStart('\', '/')
    $objectName = ($relative -replace '[\\/]', '__') + '.o'
    $objectPath = Join-Path $ObjDir $objectName

    $needsBuild = $true
    if (Test-Path $objectPath) {
        $needsBuild = (Get-Item $Source).LastWriteTimeUtc -gt (Get-Item $objectPath).LastWriteTimeUtc
        if (-not $needsBuild) {
            # Header changes also invalidate everything; cheap and safe.
            $newestHeader = Get-ChildItem -Path $SrcDir -Filter '*.h' -File |
                Sort-Object LastWriteTimeUtc -Descending | Select-Object -First 1
            if ($newestHeader -and $newestHeader.LastWriteTimeUtc -gt (Get-Item $objectPath).LastWriteTimeUtc) {
                $needsBuild = $true
            }
        }
    }

    if (-not $needsBuild) { return $objectPath }

    $extra = @()
    if ([System.IO.Path]::GetFileName($Source) -eq 'minilibc.c') {
        # Keep GCC from turning the byte loops back into calls to themselves.
        $extra += '-fno-tree-loop-distribute-patterns'
    }

    Write-Host ("  CC  " + $relative)
    $gccArgs = $CommonFlags + $extra + $IncludeFlags + @('-c', $Source, '-o', $objectPath)
    & $Gcc @gccArgs
    if ($LASTEXITCODE -ne 0) { throw "Compilation failed: $relative" }
    return $objectPath
}

Write-Host ''
Write-Host 'H420X clean-room firmware build'
Write-Host ("  gcc: " + $Gcc)
Write-Host ''

$Objects = @()
foreach ($source in $Sources) {
    $Objects += (Compile-One $source)
}

# ---------------------------------------------------------------------------
# Link
# ---------------------------------------------------------------------------
$Elf  = Join-Path $BuildDir ($Prefix + '.elf')
$Map  = Join-Path $BuildDir ($Prefix + '.map')
$Raw  = Join-Path $BuildDir ($Prefix + '_raw.bin')
$App  = Join-Path $BuildDir ($Prefix + '_app.bin')
$Lst  = Join-Path $BuildDir ($Prefix + '.lst')
$HashFile = Join-Path $BuildDir ($Prefix + '.sha256')

Write-Host ''
Write-Host ('  LD  ' + $Prefix + '.elf')
$LinkArgs = @(
    '-mcpu=cortex-m4',
    '-mthumb',
    '-mfloat-abi=soft',
    '-nostdlib',
    '-T', $LdScript,
    '-Wl,--gc-sections',
    '-Wl,--build-id=none',
    '-Wl,--cref',
    '-Xlinker', '-Map', '-Xlinker', $Map,
    '-Wl,--print-memory-usage',
    '-o', $Elf
) + $Objects + @('-lgcc')

& $Gcc @LinkArgs
if ($LASTEXITCODE -ne 0) { throw 'Link failed.' }

# ---------------------------------------------------------------------------
# Post-process
# ---------------------------------------------------------------------------
& $Objcopy -O binary $Elf $Raw
if ($LASTEXITCODE -ne 0) { throw 'objcopy failed.' }

$rawBytes = [System.IO.File]::ReadAllBytes($Raw)
if ($rawBytes.Length -gt $AppBinSize) {
    throw ("Image is {0} bytes, larger than the {1}-byte application window." -f $rawBytes.Length, $AppBinSize)
}

# DFU tooling expects exactly the 32,000-byte application interval.
$appBytes = New-Object byte[] $AppBinSize
for ($i = 0; $i -lt $AppBinSize; $i++) { $appBytes[$i] = 0xFF }
[Array]::Copy($rawBytes, 0, $appBytes, 0, $rawBytes.Length)
[System.IO.File]::WriteAllBytes($App, $appBytes)

& $Objdump -d -S $Elf | Out-File -Encoding ascii $Lst

$hash = (Get-FileHash -Algorithm SHA256 -Path $App).Hash.ToLowerInvariant()
("" + $hash + "  " + $Prefix + "_app.bin") | Out-File -Encoding ascii $HashFile

Write-Host ''
Write-Host 'Section sizes:'
& $Size $Elf

$vectors = & $Nm $Elf | Select-String ' T Reset_Handler$'
Write-Host ''
Write-Host ("Raw image:      {0} bytes" -f $rawBytes.Length)
Write-Host ("DFU image:      {0} bytes (0xFF padded)" -f $appBytes.Length)
Write-Host ("Flash window:   0x{0:X8}..0x{1:X8} ({2} bytes)" -f $AppFlashBase, $AppFlashEnd, $AppBinSize)
Write-Host ("SHA-256:        {0}" -f $hash)
Write-Host ("Artifacts:      {0}" -f $BuildDir)
Write-Host ''
Write-Host ('Build OK. Run tools\verify_image.py build\' + $Prefix + '.elf to validate the image.')
