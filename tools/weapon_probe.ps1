param(
    [string]$ProcessName = 'hde',
    [int]$ActorBytes = 0x500
)

# Probe strictement en lecture seule. Il sert à relever l'acteur local et les
# champs candidats (pointeurs, compteurs, timers) sans effectuer de scan massif.
$ErrorActionPreference = 'Stop'

if (-not ('HdRadar.WeaponProbeMemory' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;

namespace HdRadar
{
    public static class WeaponProbeMemory
    {
        [DllImport("kernel32.dll", SetLastError = true)]
        public static extern IntPtr OpenProcess(UInt32 access, bool inherit, UInt32 pid);

        [DllImport("kernel32.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        public static extern bool ReadProcessMemory(
            IntPtr process, IntPtr address, [Out] byte[] buffer,
            UIntPtr size, out UIntPtr bytesRead);

        [DllImport("kernel32.dll")]
        [return: MarshalAs(UnmanagedType.Bool)]
        public static extern bool CloseHandle(IntPtr handle);

        [StructLayout(LayoutKind.Sequential)]
        public struct MemoryBasicInformation
        {
            public IntPtr BaseAddress;
            public IntPtr AllocationBase;
            public UInt32 AllocationProtect;
            public UIntPtr RegionSize;
            public UInt32 State;
            public UInt32 Protect;
            public UInt32 Type;
        }

        [DllImport("kernel32.dll", SetLastError = true)]
        public static extern UIntPtr VirtualQueryEx(
            IntPtr process, IntPtr address,
            out MemoryBasicInformation information, UIntPtr length);
    }
}
'@
}

function Read-Bytes {
    param([IntPtr]$Process, [UInt64]$Address, [int]$Count)
    $buffer = [byte[]]::new($Count)
    [UIntPtr]$read = [UIntPtr]::Zero
    $ok = [HdRadar.WeaponProbeMemory]::ReadProcessMemory(
        $Process, [IntPtr]::new([Int64]$Address), $buffer,
        [UIntPtr]::new([UInt64]$Count), [ref]$read)
    if (-not $ok -or $read.ToUInt64() -ne [UInt64]$Count) {
        throw ('ReadProcessMemory failed at 0x{0:X8}' -f $Address)
    }
    return ,$buffer
}

function Read-U32 { param([IntPtr]$Process, [UInt64]$Address)
    [BitConverter]::ToUInt32((Read-Bytes $Process $Address 4), 0)
}

function Read-U16 { param([IntPtr]$Process, [UInt64]$Address)
    [BitConverter]::ToUInt16((Read-Bytes $Process $Address 2), 0)
}

function Read-U8 { param([IntPtr]$Process, [UInt64]$Address)
    (Read-Bytes $Process $Address 1)[0]
}

function Read-F32 { param([IntPtr]$Process, [UInt64]$Address)
    [BitConverter]::ToSingle((Read-Bytes $Process $Address 4), 0)
}

function Get-AllocationBase {
    param([IntPtr]$Process, [UInt64]$Address)
    $info = [HdRadar.WeaponProbeMemory+MemoryBasicInformation]::new()
    $size = [Runtime.InteropServices.Marshal]::SizeOf($info)
    $result = [HdRadar.WeaponProbeMemory]::VirtualQueryEx(
        $Process, [IntPtr]::new([Int64]$Address), [ref]$info,
        [UIntPtr]::new([UInt64]$size))
    if ($result -eq [UIntPtr]::Zero) { return [UInt64]0 }
    [UInt64]$info.AllocationBase.ToInt64()
}

$games = @(Get-Process -Name $ProcessName -ErrorAction Stop)
if ($games.Count -ne 1) { throw "Expected one $ProcessName process, found $($games.Count)." }
$game = $games[0]
$module = $game.MainModule
$handle = [HdRadar.WeaponProbeMemory]::OpenProcess(0x0410, $false, [UInt32]$game.Id)
if ($handle -eq [IntPtr]::Zero) { throw 'OpenProcess failed.' }

try {
    [UInt64]$base = [UInt64]$module.BaseAddress.ToInt64()
    [UInt64]$mission = Read-U32 $handle ($base + 0x10AD4C)
    if ($mission -eq 0) { throw 'Mission pointer is null; load an active mission first.' }
    [UInt64]$begin = Read-U32 $handle ($mission + 0x68)
    [UInt64]$end = Read-U32 $handle ($mission + 0x6C)
    if ($begin -eq 0 -or $end -lt $begin -or (($end - $begin) % 4) -ne 0) {
        throw 'Actor vector is invalid; load an active mission first.'
    }
    $count = [int](($end - $begin) / 4)
    if ($count -gt 4096) { throw 'Actor vector is not sane.' }

    $local = $null
    for ($i = 0; $i -lt $count; ++$i) {
        [UInt64]$actor = Read-U32 $handle ($begin + [UInt64]($i * 4))
        if ($actor -eq 0) { continue }
        try {
            $type = Read-U32 $handle ($actor + 0x1C)
            $state = Read-U32 $handle ($actor + 0x254)
            $active = [int](Read-Bytes $handle ($actor + 0x2B8) 1)[0]
            [UInt64]$frame = Read-U32 $handle ($actor + 0x28)
            if (($type -eq 1 -or $type -eq 2) -and $state -ne 4 -and
                $active -eq 1 -and $frame -ne 0 -and
                (Read-U32 $handle ($frame + 0x80)) -eq $actor) {
                $local = [pscustomobject]@{
                    index = $i; actor = $actor; frame = $frame; type = $type
                }
                break
            }
        } catch { continue }
    }
    if ($null -eq $local) { throw 'No active local actor found.' }

    [UInt64]$inventoryItems = Read-U32 $handle ($local.actor + 0x5C)
    [UInt64]$inventoryEnd = Read-U32 $handle ($local.actor + 0x60)
    $inventoryCount = if ($inventoryEnd -ge $inventoryItems) {
        [int](($inventoryEnd - $inventoryItems) / 4)
    } else { 0 }
    [int]$selectedIndex = [int](Read-U32 $handle ($local.actor + 0x258))
    [UInt64]$selectedItem = 0
    if ($inventoryItems -ne 0 -and $selectedIndex -ge 0 -and $selectedIndex -lt 64) {
        $selectedItem = Read-U32 $handle (
            $inventoryItems + [UInt64]($selectedIndex * 4))
    }
    [UInt64]$weaponState = Read-U32 $handle ($local.actor + 0x244)
    [UInt64]$weaponSlot0 = Read-U32 $handle ($local.actor + 0x248)
    [UInt64]$weaponSlot1 = Read-U32 $handle ($local.actor + 0x24C)

    [UInt32]$itemId = 0
    if ($selectedItem -ne 0) {
        $itemId = Read-U32 $handle ($selectedItem + 0x08)
    }

    # La table d'armes contient les valeurs de cadence et de dispersion
    # réellement lues par le chemin de tir. Ces relevés restent en lecture seule.
    [UInt32]$gameMode = Read-U32 $handle ($base + 0x1086E0)
    $tableIndex = if ($gameMode -ge 6 -and $gameMode -ne 9) { 1 } else { 0 }
    [UInt64]$weaponTable = Read-U32 $handle (
        $base + 0x10AAD0 + [UInt64]($tableIndex * 4))
    $weaponProperties = [Collections.Generic.List[object]]::new()
    if ($weaponTable -ne 0) {
        [UInt32]$propertyCount = Read-U32 $handle ($weaponTable + 0x0C)
        [UInt64]$descriptors = Read-U32 $handle ($weaponTable + 0x20)
        [UInt64]$data = Read-U32 $handle ($weaponTable + 0x24)
        foreach ($property in @(
            0x08, 0x0C, 0x19, 0x1C, 0x1D, 0x1E, 0x1F, 0x20, 0x21, 0x22,
            0x23, 0x24, 0x25)) {
            if ($property -ge $propertyCount) { continue }
            [UInt64]$descriptor = $descriptors + [UInt64]($property * 8)
            [UInt32]$dataOffset = Read-U32 $handle $descriptor
            [byte]$valueType = Read-U8 $handle ($descriptor + 4)
            [UInt16]$arrayLength = Read-U16 $handle ($descriptor + 6)
            [UInt64]$valueAddress = 0
            [UInt32]$rawValue = 0
            if ($itemId -lt $arrayLength) {
                $elementSize = switch ($valueType) {
                    1 { 1 } # bool
                    2 { 4 } # int
                    3 { 4 } # float
                    4 { 1 } # enum
                    6 { 3 } # RGB
                    7 { 12 } # vector
                    default { 0 }
                }
                if ($elementSize -ne 0) {
                    $valueAddress = $data + $dataOffset +
                        [UInt64]($itemId * $elementSize)
                    if ($elementSize -eq 1) {
                        $rawValue = Read-U8 $handle $valueAddress
                    } else {
                        $rawValue = Read-U32 $handle $valueAddress
                    }
                }
            }
            $weaponProperties.Add([pscustomobject][ordered]@{
                property = ('0x{0:X2}' -f $property)
                type = $valueType
                array_length = $arrayLength
                data_offset = ('0x{0:X8}' -f $dataOffset)
                value_address = ('0x{0:X8}' -f $valueAddress)
                raw = ('0x{0:X8}' -f $rawValue)
                uint = $rawValue
                float = [BitConverter]::ToSingle([BitConverter]::GetBytes($rawValue), 0)
            })
        }
    }

    $bytes = Read-Bytes $handle $local.actor $ActorBytes
    $fields = [Collections.Generic.List[object]]::new()
    for ($offset = 0; $offset -le $ActorBytes - 4; $offset += 4) {
        $u32 = [BitConverter]::ToUInt32($bytes, $offset)
        $f32 = [BitConverter]::ToSingle($bytes, $offset)
        $entry = [ordered]@{
            offset = ('0x{0:X3}' -f $offset)
            u32 = ('0x{0:X8}' -f $u32)
            uint = $u32
            float = $f32
        }
        if ($u32 -ge 0x10000 -and (Get-AllocationBase $handle $u32) -ne 0) {
            $entry.pointer_allocation_base = ('0x{0:X8}' -f (Get-AllocationBase $handle $u32))
            try {
                $entry.pointed_vtable = ('0x{0:X8}' -f (Read-U32 $handle $u32))
            } catch { }
        }
        $fields.Add([pscustomobject]$entry)
    }

    $selectedItemFields = [Collections.Generic.List[object]]::new()
    if ($selectedItem -ne 0 -and (Get-AllocationBase $handle $selectedItem) -ne 0) {
        try {
            $itemBytes = Read-Bytes $handle $selectedItem 0x300
            for ($offset = 0; $offset -le 0x2FC; $offset += 4) {
                $u32 = [BitConverter]::ToUInt32($itemBytes, $offset)
                $selectedItemFields.Add([pscustomobject][ordered]@{
                    offset = ('0x{0:X3}' -f $offset)
                    u32 = ('0x{0:X8}' -f $u32)
                    uint = $u32
                    float = [BitConverter]::ToSingle($itemBytes, $offset)
                })
            }
        } catch { }
    }

    [pscustomobject]@{
        process_id = $game.Id
        module_base = ('0x{0:X8}' -f $base)
        mission = ('0x{0:X8}' -f $mission)
        local_actor = ('0x{0:X8}' -f $local.actor)
        local_frame = ('0x{0:X8}' -f $local.frame)
        actor_type = $local.type
        inventory_items = ('0x{0:X8}' -f $inventoryItems)
        inventory_end = ('0x{0:X8}' -f $inventoryEnd)
        inventory_count = $inventoryCount
        selected_index = $selectedIndex
        selected_item = ('0x{0:X8}' -f $selectedItem)
        selected_item_id = $itemId
        weapon_state = ('0x{0:X8}' -f $weaponState)
        weapon_slot0 = ('0x{0:X8}' -f $weaponSlot0)
        weapon_slot1 = ('0x{0:X8}' -f $weaponSlot1)
        actor_bytes = $ActorBytes
        fields = $fields
        selected_item_fields = $selectedItemFields
        game_mode = $gameMode
        weapon_table_index = $tableIndex
        weapon_table = ('0x{0:X8}' -f $weaponTable)
        weapon_properties = $weaponProperties
        note = 'Lecture seule; conserver un relevé arme rangée/equipee/tir/recharge pour comparaison.'
    } | ConvertTo-Json -Depth 5
}
finally {
    [void][HdRadar.WeaponProbeMemory]::CloseHandle($handle)
}
