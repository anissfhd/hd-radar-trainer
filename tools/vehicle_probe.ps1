param([string]$ProcessName = 'hde')

$ErrorActionPreference = 'Stop'

if (-not ('HdRadar.VehicleProbeMemory' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;

namespace HdRadar
{
    public static class VehicleProbeMemory
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
    $ok = [HdRadar.VehicleProbeMemory]::ReadProcessMemory(
        $Process, [IntPtr]::new([Int64]$Address), $buffer,
        [UIntPtr]::new([UInt64]$Count), [ref]$read)
    if (-not $ok -or $read.ToUInt64() -ne [UInt64]$Count) { return $null }
    return ,$buffer
}

function Read-U32 {
    param([IntPtr]$Process, [UInt64]$Address)
    $bytes = Read-Bytes $Process $Address 4
    if ($null -eq $bytes) { return $null }
    return [BitConverter]::ToUInt32($bytes, 0)
}

function Read-F32 {
    param([IntPtr]$Process, [UInt64]$Address)
    $bytes = Read-Bytes $Process $Address 4
    if ($null -eq $bytes) { return $null }
    return [BitConverter]::ToSingle($bytes, 0)
}

$games = @(Get-Process -Name $ProcessName -ErrorAction Stop)
if ($games.Count -ne 1) {
    throw "Expected one $ProcessName process, found $($games.Count)."
}

$game = $games[0]
$handle = [HdRadar.VehicleProbeMemory]::OpenProcess(0x0410, $false, [UInt32]$game.Id)
if ($handle -eq [IntPtr]::Zero) {
    throw "OpenProcess failed: $([Runtime.InteropServices.Marshal]::GetLastWin32Error())."
}

try {
    [UInt64]$module = [UInt64]$game.MainModule.BaseAddress.ToInt64()
    [UInt64]$mission = Read-U32 $handle ($module + 0x10AD4C)
    [UInt64]$actorBegin = Read-U32 $handle ($mission + 0x68)
    [UInt64]$actorEnd = Read-U32 $handle ($mission + 0x6C)
    if ($actorEnd -lt $actorBegin -or (($actorEnd - $actorBegin) % 4) -ne 0) {
        throw 'Invalid actor vector.'
    }

    $actors = @()
    $players = @()
    [UInt64]$player = 0
    for ([UInt64]$slot = $actorBegin; $slot -lt $actorEnd; $slot += 4) {
        [UInt64]$actor = Read-U32 $handle $slot
        if ($actor -lt 0x10000 -or $actor -gt 0x7FFFFFFF) { continue }
        $type = Read-U32 $handle ($actor + 0x1C)
        if ($null -eq $type) { continue }
        $actors += [pscustomobject]@{ Address = $actor; Type = [UInt32]$type }
        if ($type -eq 1) {
            $activeBytes = Read-Bytes $handle ($actor + 0x2B8) 4
            $activeByte = if ($null -eq $activeBytes) { 0 } else { $activeBytes[0] }
            [UInt64]$actorUsingItem = Read-U32 $handle ($actor + 0x250)
            $players += [pscustomobject]@{
                address = '0x{0:X8}' -f $actor
                active_byte = $activeByte
                active_u32 = if ($null -eq $activeBytes) { $null } else { '0x{0:X8}' -f [BitConverter]::ToUInt32($activeBytes, 0) }
                using_item = '0x{0:X8}' -f $actorUsingItem
            }
            if ($activeByte -eq 1) { $player = $actor }
        }
    }
    if ($player -eq 0) { throw 'Local player not found.' }

    [UInt64]$usingItem = Read-U32 $handle ($player + 0x250)
    $vehicles = @()
    foreach ($item in $actors) {
        if ($item.Type -ne 8 -and $item.Type -ne 16) { continue }
        [UInt64]$vehicle = $item.Address
        $matches = @()
        $block = Read-Bytes $handle ($vehicle + 0x100) 0x201
        if ($null -ne $block) {
            for ($offset = 0; $offset -le 0x1FC; $offset += 4) {
                $word = [BitConverter]::ToUInt32($block, $offset)
                foreach ($knownPlayer in $players) {
                    if ($word -eq [Convert]::ToUInt32($knownPlayer.address.Substring(2), 16)) {
                        $matches += [pscustomobject]@{
                            offset = '+0x{0:X3}' -f ($offset + 0x100)
                            player = $knownPlayer.address
                        }
                    }
                }
            }
        }
        $seats = @()
        for ($seatIndex = 0; $seatIndex -lt 8; ++$seatIndex) {
            [UInt64]$seat = $vehicle + 0x140 + ($seatIndex * 0x20)
            $driverBytes = Read-Bytes $handle $seat 4
            $user = Read-U32 $handle ($seat + 0x0C)
            $seats += [pscustomobject]@{
                index = $seatIndex
                driver_u32 = if ($null -eq $driverBytes) { $null } else { '0x{0:X8}' -f [BitConverter]::ToUInt32($driverBytes, 0) }
                user = if ($null -eq $user) { $null } else { '0x{0:X8}' -f $user }
                local_player = ($user -eq $player)
            }
        }
        $samples = @()
        for ($sampleIndex = 0; $sampleIndex -lt 8; ++$sampleIndex) {
            $samples += Read-F32 $handle ($vehicle + 0x244)
            Start-Sleep -Milliseconds 100
        }
        $layoutWords = @()
        for ($offset = 0x110; $offset -le 0x250; $offset += 4) {
            $bytes = Read-Bytes $handle ($vehicle + $offset) 4
            if ($null -eq $bytes) { continue }
            $word = [BitConverter]::ToUInt32($bytes, 0)
            if ($word -eq 0) { continue }
            $layoutWords += [pscustomobject]@{
                offset = '+0x{0:X3}' -f $offset
                u32 = '0x{0:X8}' -f $word
                f32 = [BitConverter]::ToSingle($bytes, 0)
            }
        }
        $vehicles += [pscustomobject]@{
            address = '0x{0:X8}' -f $vehicle
            type = $item.Type
            is_using_item = ($vehicle -eq $usingItem)
            player_pointer_offsets = @($matches)
            speed_0x244 = @($samples)
            assumed_seats = @($seats)
            nonzero_layout_words = @($layoutWords)
        }
    }

    $typeCounts = @($actors | Group-Object Type | Sort-Object {[int]$_.Name} | ForEach-Object {
        [pscustomobject]@{ type = [int]$_.Name; count = $_.Count }
    })
    [pscustomobject]@{
        pid = $game.Id
        module = '0x{0:X8}' -f $module
        mission = '0x{0:X8}' -f $mission
        actor_count = $actors.Count
        actor_types = $typeCounts
        player = '0x{0:X8}' -f $player
        players = @($players)
        player_using_item = '0x{0:X8}' -f $usingItem
        vehicles = @($vehicles)
    } | ConvertTo-Json -Depth 7
}
finally {
    [void][HdRadar.VehicleProbeMemory]::CloseHandle($handle)
}
