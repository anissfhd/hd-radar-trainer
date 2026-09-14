param([string]$ProcessName = 'hde')

$ErrorActionPreference = 'Stop'

if (-not ('HdRadar.MapProbeMemory' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;

namespace HdRadar
{
    public static class MapProbeMemory
    {
        [DllImport("kernel32.dll", SetLastError = true)]
        public static extern IntPtr OpenProcess(
            UInt32 desiredAccess, bool inheritHandle, UInt32 processId);

        [DllImport("kernel32.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        public static extern bool ReadProcessMemory(
            IntPtr process, IntPtr address, [Out] byte[] buffer,
            UIntPtr size, out UIntPtr bytesRead);

        [DllImport("kernel32.dll")]
        [return: MarshalAs(UnmanagedType.Bool)]
        public static extern bool CloseHandle(IntPtr handle);
    }
}
'@
}

function Read-Bytes {
    param([IntPtr]$Process, [UInt64]$Address, [int]$Count)
    $buffer = [byte[]]::new($Count)
    [UIntPtr]$read = [UIntPtr]::Zero
    $ok = [HdRadar.MapProbeMemory]::ReadProcessMemory(
        $Process, [IntPtr]::new([Int64]$Address), $buffer,
        [UIntPtr]::new([UInt64]$Count), [ref]$read)
    if (-not $ok -or $read.ToUInt64() -ne [UInt64]$Count) {
        throw ('ReadProcessMemory failed at 0x{0:X8}' -f $Address)
    }
    return ,$buffer
}

function Read-U32 {
    param([IntPtr]$Process, [UInt64]$Address)
    [BitConverter]::ToUInt32((Read-Bytes $Process $Address 4), 0)
}

function Read-Floats {
    param([IntPtr]$Process, [UInt64]$Address, [int]$Count)
    $bytes = Read-Bytes $Process $Address ($Count * 4)
    $values = for ($index = 0; $index -lt $Count; ++$index) {
        [BitConverter]::ToSingle($bytes, $index * 4)
    }
    return ,$values
}

$games = @(Get-Process -Name $ProcessName -ErrorAction Stop)
if ($games.Count -ne 1) {
    throw "Expected one $ProcessName process, found $($games.Count)."
}

$game = $games[0]
$handle = [HdRadar.MapProbeMemory]::OpenProcess(0x0410, $false, [UInt32]$game.Id)
if ($handle -eq [IntPtr]::Zero) {
    throw "OpenProcess failed: $([Runtime.InteropServices.Marshal]::GetLastWin32Error())."
}

try {
    [UInt64]$module = [UInt64]$game.MainModule.BaseAddress.ToInt64()
    [UInt64]$mission = Read-U32 $handle ($module + 0x10AD4C)
    [UInt64]$scene = Read-U32 $handle ($mission + 0x10)
    # Installed SwitchToMap: map_mgr +0xA0, map_active +0xA4.
    [UInt64]$mapManager = Read-U32 $handle ($mission + 0xA0)
    $mapActive = (Read-Bytes $handle ($mission + 0xA4) 1)[0]
    [UInt64]$mapScene = if ($mapManager -ne 0) {
        Read-U32 $handle ($mapManager + 0x10)
    } else { 0 }

    [UInt64]$player = 0
    $actorBegin = Read-U32 $handle ($mission + 0x68)
    $actorEnd = Read-U32 $handle ($mission + 0x6C)
    for ([UInt64]$slot = $actorBegin; $slot -lt $actorEnd; $slot += 4) {
        [UInt64]$actor = Read-U32 $handle $slot
        if ($actor -ne 0 -and (Read-U32 $handle ($actor + 0x1C)) -eq 1) {
            $active = Read-U32 $handle ($actor + 0x2B8)
            if ($active -ne 0) { $player = $actor; break }
        }
    }
    [UInt64]$frame = if ($player -ne 0) { Read-U32 $handle ($player + 0x28) } else { 0 }
    $world = if ($frame -ne 0) { Read-Floats $handle ($frame + 0xBC) 3 } else { @() }
    $local = if ($frame -ne 0) { Read-Floats $handle ($frame + 0x14C) 3 } else { @() }
    $mapMatrix = if ($mapScene -ne 0) { Read-Floats $handle ($mapScene + 0x8C) 16 } else { @() }
    $missionWords = [ordered]@{}
    for ($offset = 0x60; $offset -le 0xC0; $offset += 4) {
        $value = Read-U32 $handle ($mission + [UInt64]$offset)
        $missionWords[('0x{0:X2}' -f $offset)] = '0x{0:X8}' -f $value
    }

    [pscustomobject]@{
        pid = $game.Id
        module = '0x{0:X8}' -f $module
        mission = '0x{0:X8}' -f $mission
        scene = '0x{0:X8}' -f $scene
        map_active = $mapActive
        map_manager = '0x{0:X8}' -f $mapManager
        map_scene = '0x{0:X8}' -f $mapScene
        actor_count = [int](($actorEnd - $actorBegin) / 4)
        player = '0x{0:X8}' -f $player
        frame = '0x{0:X8}' -f $frame
        world_position = @($world)
        local_position = @($local)
        map_view_projection = @($mapMatrix)
        mission_words = $missionWords
    } | ConvertTo-Json -Depth 4
}
finally {
    [void][HdRadar.MapProbeMemory]::CloseHandle($handle)
}
