param(
    [string]$ProcessName = 'hde',
    [int]$SceneScanBytes = 16384
)

$ErrorActionPreference = 'Stop'

if (-not ('HdRadar.BspNativeMemory' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;

namespace HdRadar
{
    public static class BspNativeMemory
    {
        [DllImport("kernel32.dll", SetLastError = true)]
        public static extern IntPtr OpenProcess(
            UInt32 desiredAccess,
            bool inheritHandle,
            UInt32 processId);

        [DllImport("kernel32.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        public static extern bool ReadProcessMemory(
            IntPtr process,
            IntPtr address,
            [Out] byte[] buffer,
            UIntPtr size,
            out UIntPtr bytesRead);

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
            IntPtr process,
            IntPtr address,
            out MemoryBasicInformation memoryInformation,
            UIntPtr length);
    }
}
'@
}

function Read-Bytes {
    param([IntPtr]$Process, [UInt64]$Address, [int]$Count)

    $buffer = [byte[]]::new($Count)
    [UIntPtr]$bytesRead = [UIntPtr]::Zero
    $ok = [HdRadar.BspNativeMemory]::ReadProcessMemory(
        $Process,
        [IntPtr]::new([Int64]$Address),
        $buffer,
        [UIntPtr]::new([UInt64]$Count),
        [ref]$bytesRead)
    if (-not $ok -or $bytesRead.ToUInt64() -ne [UInt64]$Count) {
        $errorCode = [Runtime.InteropServices.Marshal]::GetLastWin32Error()
        throw ('ReadProcessMemory failed at 0x{0:X8} ({1} bytes), Win32={2}' -f
            $Address, $Count, $errorCode)
    }
    return ,$buffer
}

function Read-U32 {
    param([IntPtr]$Process, [UInt64]$Address)
    return [BitConverter]::ToUInt32((Read-Bytes $Process $Address 4), 0)
}

function Get-MemoryRegion {
    param([IntPtr]$Process, [UInt64]$Address)

    $information = [HdRadar.BspNativeMemory+MemoryBasicInformation]::new()
    $size = [Runtime.InteropServices.Marshal]::SizeOf($information)
    $result = [HdRadar.BspNativeMemory]::VirtualQueryEx(
        $Process,
        [IntPtr]::new([Int64]$Address),
        [ref]$information,
        [UIntPtr]::new([UInt64]$size))
    if ($result -eq [UIntPtr]::Zero) { return $null }
    return $information
}

$games = @(Get-Process -Name $ProcessName -ErrorAction Stop)
if ($games.Count -ne 1) {
    throw "Expected one $ProcessName process, found $($games.Count)."
}

$game = $games[0]
$handle = [HdRadar.BspNativeMemory]::OpenProcess(0x0410, $false, [UInt32]$game.Id)
if ($handle -eq [IntPtr]::Zero) {
    $errorCode = [Runtime.InteropServices.Marshal]::GetLastWin32Error()
    throw "OpenProcess failed, Win32=$errorCode."
}

try {
    [UInt64]$moduleBase = [UInt64]$game.MainModule.BaseAddress.ToInt64()
    [UInt64]$mission = Read-U32 $handle ($moduleBase + 0x10AD4C)
    [UInt64]$scene = Read-U32 $handle ($mission + 0x10)
    [UInt64]$vtable = Read-U32 $handle $scene
    $vtableRegion = Get-MemoryRegion $handle $vtable
    [UInt64]$bspObject = $scene + 0x1C4
    [UInt64]$bspVtable = Read-U32 $handle $bspObject
    [UInt64]$collisionTableObject = Read-U32 $handle ($moduleBase + 0x10AC04)
    [UInt64]$collisionTableVtable = if ($collisionTableObject -ne 0) {
        Read-U32 $handle $collisionTableObject
    } else { 0 }
    [UInt64]$collisionPropertyGetter = if ($collisionTableVtable -ne 0) {
        Read-U32 $handle ($collisionTableVtable + 0x28)
    } else { 0 }
    [UInt64]$collisionPropertyBaseGetter = if ($collisionTableVtable -ne 0) {
        Read-U32 $handle ($collisionTableVtable + 0x24)
    } else { 0 }
    $passThroughMaterialIds = [Collections.Generic.HashSet[int]]::new()
    if ($collisionTableObject -ne 0) {
        [UInt64]$propertyDescriptors = Read-U32 $handle ($collisionTableObject + 0x20)
        [UInt64]$propertyData = Read-U32 $handle ($collisionTableObject + 0x24)
        foreach ($propertyIndex in 8, 9, 11) {
            $descriptor = Read-Bytes $handle (
                $propertyDescriptors + [UInt64]($propertyIndex * 8)) 8
            [UInt64]$propertyOffset = [BitConverter]::ToUInt32($descriptor, 0)
            $propertyCount = [BitConverter]::ToUInt16($descriptor, 6)
            if ($propertyOffset -ne 0xFFFFFFFF -and $propertyCount -gt 0) {
                $values = Read-Bytes $handle ($propertyData + $propertyOffset) $propertyCount
                for ($materialId = 0; $materialId -lt $values.Length; ++$materialId) {
                    if ($values[$materialId] -ne 0) {
                        [void]$passThroughMaterialIds.Add($materialId)
                    }
                }
            }
        }
    }
    [void]$passThroughMaterialIds.Add(42)

    $vtableEntries = [Collections.Generic.List[object]]::new()
    for ($index = 0; $index -lt 96; ++$index) {
        [UInt64]$function = Read-U32 $handle ($vtable + [UInt64]($index * 4))
        if ($function -eq 0) { break }
        try {
            $bytes = Read-Bytes $handle $function 24
            $hex = ($bytes | ForEach-Object { $_.ToString('X2') }) -join ' '
            $vtableEntries.Add([pscustomobject]@{
                index = $index
                address = '0x{0:X8}' -f $function
                bytes = $hex
            })
        } catch {
            break
        }
    }

    $sceneData = Read-Bytes $handle $scene $SceneScanBytes
    $bspData = Read-Bytes $handle $bspObject 512
    [UInt64]$faceBegin = [BitConverter]::ToUInt32($bspData, 0x20)
    [UInt64]$faceEnd = [BitConverter]::ToUInt32($bspData, 0x24)
    $faceSamples = [Collections.Generic.List[object]]::new()
    $materialVtableEntries = [Collections.Generic.List[object]]::new()
    if ($faceBegin -ne 0 -and $faceEnd -ge $faceBegin -and
        (($faceEnd - $faceBegin) % 156) -eq 0) {
        $sampleCount = [Math]::Min(5, [int](($faceEnd - $faceBegin) / 156))
        for ($index = 0; $index -lt $sampleCount; ++$index) {
            $face = Read-Bytes $handle ($faceBegin + [UInt64]($index * 156)) 156
            [UInt64]$material = [BitConverter]::ToUInt32($face, 0x08)
            [UInt64]$originFrame = [BitConverter]::ToUInt32($face, 0x00)
            [UInt64]$originVtable = if ($originFrame -ne 0) {
                Read-U32 $handle $originFrame
            } else { 0 }
            [UInt64]$materialVtable = if ($material -ne 0) {
                Read-U32 $handle $material
            } else { 0 }
            [UInt64]$isTwoSidedFunction = if ($materialVtable -ne 0) {
                Read-U32 $handle ($materialVtable + 15 * 4)
            } else { 0 }
            $points = for ($pointIndex = 0; $pointIndex -lt 3; ++$pointIndex) {
                $pointOffset = 0x14 + $pointIndex * 12
                [pscustomobject]@{
                    x = [BitConverter]::ToSingle($face, $pointOffset)
                    y = [BitConverter]::ToSingle($face, $pointOffset + 4)
                    z = [BitConverter]::ToSingle($face, $pointOffset + 8)
                }
            }
            $faceSamples.Add([pscustomobject]@{
                index = $index
                origin_frame = '0x{0:X8}' -f $originFrame
                origin_type = if ($originFrame -ne 0) {
                    Read-U32 $handle ($originFrame + 0x08)
                } else { $null }
                origin_flags = if ($originFrame -ne 0) {
                    '0x{0:X8}' -f (Read-U32 $handle ($originFrame + 0x0C))
                } else { $null }
                origin_e4_bytes = if ($originVtable -ne 0) {
                    [UInt64]$originFunction = Read-U32 $handle ($originVtable + 0xE4)
                    ((Read-Bytes $handle $originFunction 16) |
                        ForEach-Object { $_.ToString('X2') }) -join ' '
                } else { $null }
                origin_e8_bytes = if ($originVtable -ne 0) {
                    [UInt64]$originFunction = Read-U32 $handle ($originVtable + 0xE8)
                    ((Read-Bytes $handle $originFunction 16) |
                        ForEach-Object { $_.ToString('X2') }) -join ' '
                } else { $null }
                origin_face_index = [BitConverter]::ToUInt32($face, 0x04)
                material = '0x{0:X8}' -f $material
                material_vtable = '0x{0:X8}' -f $materialVtable
                is_two_sided_function = '0x{0:X8}' -f $isTwoSidedFunction
                is_two_sided_bytes = if ($isTwoSidedFunction -ne 0) {
                    ((Read-Bytes $handle $isTwoSidedFunction 16) |
                        ForEach-Object { $_.ToString('X2') }) -join ' '
                } else { $null }
                plane_id = [BitConverter]::ToUInt32($face, 0x0C)
                plane_side = [bool]$face[0x10]
                points = $points
            })
            if ($index -eq 0 -and $materialVtable -ne 0) {
                for ($materialIndex = 0; $materialIndex -lt 24; ++$materialIndex) {
                    [UInt64]$materialFunction = Read-U32 $handle (
                        $materialVtable + [UInt64]($materialIndex * 4))
                    $materialVtableEntries.Add([pscustomobject]@{
                        index = $materialIndex
                        address = '0x{0:X8}' -f $materialFunction
                        bytes = ((Read-Bytes $handle $materialFunction 16) |
                            ForEach-Object { $_.ToString('X2') }) -join ' '
                    })
                }
            }
        }
    }
    $bspWords = for ($offset = 0; $offset -lt $bspData.Length; $offset += 4) {
        [pscustomobject]@{
            offset = '0x{0:X3}' -f $offset
            value = '0x{0:X8}' -f [BitConverter]::ToUInt32($bspData, $offset)
        }
    }
    $bspVtableEntries = [Collections.Generic.List[object]]::new()
    for ($index = 0; $index -lt 16; ++$index) {
        [UInt64]$function = Read-U32 $handle ($bspVtable + [UInt64]($index * 4))
        try {
            $bytes = Read-Bytes $handle $function 32
            $bspVtableEntries.Add([pscustomobject]@{
                index = $index
                address = '0x{0:X8}' -f $function
                bytes = ($bytes | ForEach-Object { $_.ToString('X2') }) -join ' '
            })
        } catch { break }
    }
    $candidates = [Collections.Generic.List[object]]::new()
    for ($offset = 0; $offset -le $sceneData.Length - 0x84; $offset += 4) {
        # C_bsp_tree x86 signature from the official source:
        # valid; C_vector<S_plane>; C_vector<S_bsp_triface>;
        # C_vector<S_vector>; C_bsp_frames; C_bsp_node_list.
        $valid = $sceneData[$offset]
        $planeSize = [BitConverter]::ToUInt32($sceneData, $offset + 0x08)
        $faceSize = [BitConverter]::ToUInt32($sceneData, $offset + 0x20)
        $vertexSize = [BitConverter]::ToUInt32($sceneData, $offset + 0x38)
        if (($valid -ne 0 -and $valid -ne 1) -or
            $planeSize -ne 16 -or $vertexSize -ne 12 -or
            $faceSize -lt 64 -or $faceSize -gt 160 -or ($faceSize % 4) -ne 0) {
            continue
        }

        $planeCount = [BitConverter]::ToUInt32($sceneData, $offset + 0x10)
        $faceCount = [BitConverter]::ToUInt32($sceneData, $offset + 0x28)
        $vertexCount = [BitConverter]::ToUInt32($sceneData, $offset + 0x40)
        $nodeBytes = [BitConverter]::ToUInt32($sceneData, $offset + 0x74)
        $nodeCount = [BitConverter]::ToUInt32($sceneData, $offset + 0x78)
        $planeAddress = [BitConverter]::ToUInt32($sceneData, $offset + 0x18)
        $faceAddress = [BitConverter]::ToUInt32($sceneData, $offset + 0x30)
        $vertexAddress = [BitConverter]::ToUInt32($sceneData, $offset + 0x48)
        $nodeAddress = [BitConverter]::ToUInt32($sceneData, $offset + 0x7C)

        if ($planeCount -gt 1000000 -or $faceCount -gt 1000000 -or
            $vertexCount -gt 1000000 -or $nodeCount -gt 1000000) {
            continue
        }

        $candidates.Add([pscustomobject]@{
            scene_offset = '0x{0:X}' -f $offset
            valid = [bool]$valid
            plane_element_size = $planeSize
            plane_count = $planeCount
            plane_address = '0x{0:X8}' -f $planeAddress
            face_element_size = $faceSize
            face_count = $faceCount
            face_address = '0x{0:X8}' -f $faceAddress
            vertex_count = $vertexCount
            vertex_address = '0x{0:X8}' -f $vertexAddress
            node_used_bytes = $nodeBytes
            node_count = $nodeCount
            node_address = '0x{0:X8}' -f $nodeAddress
        })
    }

    [pscustomobject]@{
        process_id = $game.Id
        module_base = '0x{0:X8}' -f $moduleBase
        mission = '0x{0:X8}' -f $mission
        scene = '0x{0:X8}' -f $scene
        scene_vtable = '0x{0:X8}' -f $vtable
        engine_allocation_base = if ($vtableRegion) {
            '0x{0:X8}' -f [UInt64]$vtableRegion.AllocationBase.ToInt64()
        } else { $null }
        vtable_entries = $vtableEntries
        bsp_object = '0x{0:X8}' -f $bspObject
        bsp_vtable = '0x{0:X8}' -f $bspVtable
        collision_table_object = '0x{0:X8}' -f $collisionTableObject
        collision_property_getter = '0x{0:X8}' -f $collisionPropertyGetter
        collision_property_base_getter = '0x{0:X8}' -f $collisionPropertyBaseGetter
        collision_property_base_getter_bytes = if ($collisionPropertyBaseGetter -ne 0) {
            ((Read-Bytes $handle $collisionPropertyBaseGetter 48) |
                ForEach-Object { $_.ToString('X2') }) -join ' '
        } else { $null }
        pass_through_material_ids = @($passThroughMaterialIds | Sort-Object)
        collision_property_getter_bytes = if ($collisionPropertyGetter -ne 0) {
            ((Read-Bytes $handle $collisionPropertyGetter 48) |
                ForEach-Object { $_.ToString('X2') }) -join ' '
        } else { $null }
        bsp_vtable_entries = $bspVtableEntries
        bsp_words = $bspWords
        bsp_face_count = if ($faceEnd -ge $faceBegin) {
            [UInt64](($faceEnd - $faceBegin) / 156)
        } else { 0 }
        bsp_face_samples = $faceSamples
        material_vtable_entries = $materialVtableEntries
        bsp_candidates = $candidates
    } | ConvertTo-Json -Depth 5
}
finally {
    [void][HdRadar.BspNativeMemory]::CloseHandle($handle)
}
