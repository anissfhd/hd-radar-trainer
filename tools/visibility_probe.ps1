param(
    [string]$ProcessName = 'hde',
    [int]$FrameBytes = 512,
    [switch]$IncludeFrameTree
)

$ErrorActionPreference = 'Stop'

if (-not ('HdRadar.NativeMemory' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;

namespace HdRadar
{
    public static class NativeMemory
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
    param(
        [IntPtr]$Process,
        [UInt64]$Address,
        [int]$Count
    )

    $buffer = [byte[]]::new($Count)
    [UIntPtr]$bytesRead = [UIntPtr]::Zero
    $ok = [HdRadar.NativeMemory]::ReadProcessMemory(
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

function Read-F32 {
    param([IntPtr]$Process, [UInt64]$Address)
    return [BitConverter]::ToSingle((Read-Bytes $Process $Address 4), 0)
}

function Get-AllocationBase {
    param([IntPtr]$Process, [UInt64]$Address)
    $memoryInformation = [HdRadar.NativeMemory+MemoryBasicInformation]::new()
    $structureSize = [Runtime.InteropServices.Marshal]::SizeOf($memoryInformation)
    $result = [HdRadar.NativeMemory]::VirtualQueryEx(
        $Process,
        [IntPtr]::new([Int64]$Address),
        [ref]$memoryInformation,
        [UIntPtr]::new([UInt64]$structureSize))
    if ($result -eq [UIntPtr]::Zero) { return [UInt64]0 }
    return [UInt64]$memoryInformation.AllocationBase.ToInt64()
}

$game = @(Get-Process -Name $ProcessName -ErrorAction Stop)
if ($game.Count -ne 1) {
    throw "Expected one $ProcessName process, found $($game.Count)."
}

$process = $game[0]
$module = $process.MainModule
$handle = [HdRadar.NativeMemory]::OpenProcess(0x0410, $false, [UInt32]$process.Id)
if ($handle -eq [IntPtr]::Zero) {
    $errorCode = [Runtime.InteropServices.Marshal]::GetLastWin32Error()
    throw "OpenProcess failed, Win32=$errorCode."
}

try {
    [UInt64]$moduleBase = [UInt64]$module.BaseAddress.ToInt64()
    [UInt64]$mission = Read-U32 $handle ($moduleBase + 0x10AD4C)
    [UInt64]$scene = Read-U32 $handle ($mission + 0x10)
    [UInt64]$camera = Read-U32 $handle ($scene + 0x88)
    [UInt64]$actorBegin = Read-U32 $handle ($mission + 0x68)
    [UInt64]$actorEnd = Read-U32 $handle ($mission + 0x6C)
    [UInt64]$actorCapacity = Read-U32 $handle ($mission + 0x70)

    if ($actorBegin -eq 0 -or $actorEnd -lt $actorBegin -or
        $actorCapacity -lt $actorEnd -or (($actorEnd - $actorBegin) % 4) -ne 0) {
        throw 'The remote actor vector is invalid.'
    }

    $actorCount = [int](($actorEnd - $actorBegin) / 4)
    if ($actorCount -gt 4096) {
        throw "The remote actor vector count is not sane: $actorCount."
    }

    $matrixBytes = Read-Bytes $handle ($scene + 0x8C) 64
    $sceneBytes = Read-Bytes $handle $scene 512
    $sceneU32 = for ($sceneOffset = 0; $sceneOffset -lt $sceneBytes.Length; $sceneOffset += 4) {
        '0x{0:X8}' -f [BitConverter]::ToUInt32($sceneBytes, $sceneOffset)
    }
    $viewProjection = for ($matrixIndex = 0; $matrixIndex -lt 16; ++$matrixIndex) {
        [BitConverter]::ToSingle($matrixBytes, $matrixIndex * 4)
    }
    $cameraPosition = [pscustomobject]@{
        x = Read-F32 $handle ($camera + 0xBC)
        y = Read-F32 $handle ($camera + 0xC0)
        z = Read-F32 $handle ($camera + 0xC4)
    }

    $actors = [Collections.Generic.List[object]]::new()
    for ($index = 0; $index -lt $actorCount; ++$index) {
        [UInt64]$actor = Read-U32 $handle ($actorBegin + [UInt64]($index * 4))
        if ($actor -eq 0) { continue }

        try {
            $type = Read-U32 $handle ($actor + 0x1C)
            if ($type -ne 1 -and $type -ne 2) { continue }
            $stayMode = Read-U32 $handle ($actor + 0x254)
            if ($stayMode -eq 4) { continue }
            [UInt64]$frame = Read-U32 $handle ($actor + 0x28)
            if ($frame -eq 0 -or (Read-U32 $handle ($frame + 0x80)) -ne $actor) {
                continue
            }

            $activePlayer = [int](Read-Bytes $handle ($actor + 0x2B8) 1)[0]
            $frameData = Read-Bytes $handle $frame $FrameBytes
            $u32 = for ($offset = 0; $offset -lt $frameData.Length; $offset += 4) {
                '0x{0:X8}' -f [BitConverter]::ToUInt32($frameData, $offset)
            }

            $positionX = Read-F32 $handle ($frame + 0xBC)
            $positionY = Read-F32 $handle ($frame + 0xC0)
            $positionZ = Read-F32 $handle ($frame + 0xC4)
            $clipW =
                $positionX * $viewProjection[3] +
                $positionY * $viewProjection[7] +
                $positionZ * $viewProjection[11] +
                $viewProjection[15]
            $clipX =
                $positionX * $viewProjection[0] +
                $positionY * $viewProjection[4] +
                $positionZ * $viewProjection[8] +
                $viewProjection[12]
            $clipY =
                $positionX * $viewProjection[1] +
                $positionY * $viewProjection[5] +
                $positionZ * $viewProjection[9] +
                $viewProjection[13]

            [UInt64]$vtable = [BitConverter]::ToUInt32($frameData, 0)
            [UInt64]$actorVtable = Read-U32 $handle $actor
            [UInt64]$isEnemy = Read-U32 $handle ($actorVtable + 0xA4)
            [UInt64]$explode = Read-U32 $handle ($actorVtable + 0xDC)
            [UInt64]$die = Read-U32 $handle ($actorVtable + 0x100)
            [UInt64]$hit = Read-U32 $handle ($actorVtable + 0x104)
            $networkId = [BitConverter]::ToUInt16((Read-Bytes $handle ($actor + 0x20) 2), 0)
            $networkOwner = Read-U32 $handle ($actor + 0x34)
            $resistance = Read-U32 $handle ($actor + 0x2C)
            $nativeNoHit = [int](Read-Bytes $handle ($actor + 0x2D4) 1)[0]
            $isEnemyBytes = Read-Bytes $handle $isEnemy 16
            $frameTree = $null
            if ($IncludeFrameTree) {
                $frameTree = [Collections.Generic.List[object]]::new()
                $pendingFrames = [Collections.Generic.Queue[UInt64]]::new()
                $visitedFrames = [Collections.Generic.HashSet[UInt64]]::new()
                $pendingFrames.Enqueue($frame)

                while ($pendingFrames.Count -ne 0 -and $visitedFrames.Count -lt 512) {
                    [UInt64]$treeFrame = $pendingFrames.Dequeue()
                    if (-not $visitedFrames.Add($treeFrame)) { continue }

                    try {
                        $treeBytes = Read-Bytes $handle $treeFrame $FrameBytes
                        $treeType = [BitConverter]::ToUInt32($treeBytes, 0x08)
                        $treeFlags = [BitConverter]::ToUInt32($treeBytes, 0x0C)
                        [UInt64]$childBegin = [BitConverter]::ToUInt32($treeBytes, 0x20)
                        [UInt64]$childEnd = [BitConverter]::ToUInt32($treeBytes, 0x24)
                        $childCount = 0
                        if ($childBegin -ne 0 -and $childEnd -ge $childBegin -and
                            (($childEnd - $childBegin) % 4) -eq 0) {
                            $childCount = [int](($childEnd - $childBegin) / 4)
                        }
                        if ($childCount -gt 256) { $childCount = 0 }

                        for ($childIndex = 0; $childIndex -lt $childCount; ++$childIndex) {
                            [UInt64]$childFrame = Read-U32 $handle (
                                $childBegin + [UInt64]($childIndex * 4))
                            if ($childFrame -ne 0) { $pendingFrames.Enqueue($childFrame) }
                        }

                        $frameTree.Add([pscustomobject]@{
                            address = '0x{0:X8}' -f $treeFrame
                            vtable = '0x{0:X8}' -f
                                [BitConverter]::ToUInt32($treeBytes, 0x00)
                            type = $treeType
                            flags = '0x{0:X8}' -f $treeFlags
                            child_count = $childCount
                            world_position = [pscustomobject]@{
                                x = [BitConverter]::ToSingle($treeBytes, 0xBC)
                                y = [BitConverter]::ToSingle($treeBytes, 0xC0)
                                z = [BitConverter]::ToSingle($treeBytes, 0xC4)
                            }
                            value_0x10 = '0x{0:X8}' -f
                                [BitConverter]::ToUInt32($treeBytes, 0x10)
                            value_0x194 = '0x{0:X8}' -f
                                [BitConverter]::ToUInt32($treeBytes, 0x194)
                            value_0x1D8 = '0x{0:X8}' -f
                                [BitConverter]::ToUInt32($treeBytes, 0x1D8)
                            value_0x1DC = '0x{0:X8}' -f
                                [BitConverter]::ToUInt32($treeBytes, 0x1DC)
                            value_0x1F0 = '0x{0:X8}' -f
                                [BitConverter]::ToUInt32($treeBytes, 0x1F0)
                            value_0x1F4 = '0x{0:X8}' -f
                                [BitConverter]::ToUInt32($treeBytes, 0x1F4)
                        })
                    }
                    catch {
                        continue
                    }
                }
            }

            $actors.Add([pscustomobject]@{
                index = $index
                actor = '0x{0:X8}' -f $actor
                frame = '0x{0:X8}' -f $frame
                actor_vtable = '0x{0:X8}' -f $actorVtable
                network_id = $networkId
                network_owner = '0x{0:X8}' -f $networkOwner
                resistance = $resistance
                native_no_hit = $nativeNoHit
                hit = '0x{0:X8}' -f $hit
                die = '0x{0:X8}' -f $die
                explode = '0x{0:X8}' -f $explode
                is_enemy = '0x{0:X8}' -f $isEnemy
                is_enemy_bytes = (($isEnemyBytes | ForEach-Object {
                    '{0:X2}' -f $_
                }) -join ' ')
                vtable = $u32[0]
                vtable_allocation_base = '0x{0:X8}' -f
                    (Get-AllocationBase $handle $vtable)
                type = $type
                stay_mode = $stayMode
                active_player = $activePlayer
                position = [pscustomobject]@{
                    x = $positionX
                    y = $positionY
                    z = $positionZ
                }
                projection = [pscustomobject]@{
                    clip_w = $clipW
                    ndc_x = if ($clipW -gt 0.01) { $clipX / $clipW } else { $null }
                    ndc_y = if ($clipW -gt 0.01) { $clipY / $clipW } else { $null }
                }
                frame_u32 = $u32
                frame_tree = $frameTree
            })
        }
        catch {
            continue
        }
    }

    [pscustomobject]@{
        process_id = $process.Id
        module_base = '0x{0:X8}' -f $moduleBase
        mission = '0x{0:X8}' -f $mission
        scene = '0x{0:X8}' -f $scene
        camera = '0x{0:X8}' -f $camera
        camera_position = $cameraPosition
        view_projection = $viewProjection
        scene_u32 = $sceneU32
        source_actor_count = $actorCount
        actors = $actors
    } | ConvertTo-Json -Depth 8
}
finally {
    [void][HdRadar.NativeMemory]::CloseHandle($handle)
}
