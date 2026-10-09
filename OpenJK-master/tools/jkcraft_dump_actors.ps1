param([string]$MappingName = 'Local\JKCraft_v1')

$type = @'
using System;
using System.Runtime.InteropServices;

public static class JKCraftMapping {
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    public static extern IntPtr OpenFileMapping(uint access, bool inherit, string name);

    [DllImport("kernel32.dll", SetLastError = true)]
    public static extern IntPtr MapViewOfFile(IntPtr mapping, uint access, uint high, uint low, UIntPtr bytes);

    [DllImport("kernel32.dll")]
    public static extern bool UnmapViewOfFile(IntPtr address);

    [DllImport("kernel32.dll")]
    public static extern bool CloseHandle(IntPtr handle);
}
'@

if (-not ('JKCraftMapping' -as [type])) {
    Add-Type -TypeDefinition $type
}

$allAccess = 0xF001F
$handle = [JKCraftMapping]::OpenFileMapping($allAccess, $false, $MappingName)
if ($handle -eq [IntPtr]::Zero) {
    throw "JKCraft mapping '$MappingName' is not available (Win32 $([Runtime.InteropServices.Marshal]::GetLastWin32Error()))."
}

$view = [JKCraftMapping]::MapViewOfFile($handle, $allAccess, 0, 0, [UIntPtr]::Zero)
if ($view -eq [IntPtr]::Zero) {
    [JKCraftMapping]::CloseHandle($handle) | Out-Null
    throw "MapViewOfFile failed (Win32 $([Runtime.InteropServices.Marshal]::GetLastWin32Error()))."
}

function Read-Int32([int64]$Offset) {
    [Runtime.InteropServices.Marshal]::ReadInt32([IntPtr]::Add($view, [int]$Offset))
}

function Read-Float([int64]$Offset) {
    [BitConverter]::Int32BitsToSingle((Read-Int32 $Offset))
}

function Read-Double([int64]$Offset) {
    [BitConverter]::Int64BitsToDouble([Runtime.InteropServices.Marshal]::ReadInt64([IntPtr]::Add($view, [int]$Offset)))
}

try {
    $magic = Read-Int32 0
    $version = Read-Int32 4
    $hostPid = Read-Int32 8
    $minecraftPid = Read-Int32 12
    "magic=0x{0:X8} version={1} openjkPid={2} minecraftPid={3}" -f $magic, $version, $hostPid, $minecraftPid

    $minecraft = 0x200
    "player flags=0x{0:X} pos=({1:F3}, {2:F3}, {3:F3}) yaw={4:F1} pitch={5:F1} teleportAck={6}" -f `
        (Read-Int32 ($minecraft + 4)), (Read-Double ($minecraft + 8)), (Read-Double ($minecraft + 16)), `
        (Read-Double ($minecraft + 24)), (Read-Float ($minecraft + 32)), (Read-Float ($minecraft + 36)), `
        (Read-Int32 ($minecraft + 48))

    "camera eye=({0:F3}, {1:F3}, {2:F3}) height={3:F3} fov={4:F1} bobPhase={5:F3} bobAmount={6:F3} mode={7} distance={8:F3}" -f `
        (Read-Double ($minecraft + 0x50)), (Read-Double ($minecraft + 0x58)), (Read-Double ($minecraft + 0x60)), `
        (Read-Float ($minecraft + 0x28)), (Read-Float ($minecraft + 0x40)), (Read-Float ($minecraft + 0x44)), `
        (Read-Float ($minecraft + 0x48)), (Read-Int32 ($minecraft + 0xC0)), (Read-Float ($minecraft + 0xC4))

    $water = 0x400
    $waterSeq = Read-Int32 $water
    $wet = 0
    $waterMin = [single]::PositiveInfinity
    $waterMax = [single]::NegativeInfinity
    for ($i = 0; $i -lt 256; $i++) {
        $surface = Read-Float ($water + 0x10 + $i * 4)
        if ($surface -gt -1.0e20) {
            $wet++
            $waterMin = [Math]::Min($waterMin, $surface)
            $waterMax = [Math]::Max($waterMax, $surface)
        }
    }
    "waterSeq={0} origin=({1}, {2}) world={3} wetColumns={4} surface={5}" -f $waterSeq, `
        (Read-Int32 ($water + 4)), (Read-Int32 ($water + 8)), (Read-Int32 ($water + 12)), $wet, `
        $(if ($wet) { "{0:F3}..{1:F3}" -f $waterMin, $waterMax } else { 'none nearby' })

    $table = 0x12000
    $sequence = Read-Int32 $table
    $count = [Math]::Min((Read-Int32 ($table + 4)), 256)
    "actorSeq={0} count={1}" -f $sequence, $count
    for ($i = 0; $i -lt $count; $i++) {
        $record = $table + 0x40 + $i * 64
        $nameBytes = [byte[]]::new(24)
        $nameLength = 0
        while ($nameLength -lt 24) {
            $b = [Runtime.InteropServices.Marshal]::ReadByte([IntPtr]::Add($view, $record + 40 + $nameLength))
            if ($b -eq 0) { break }
            $nameBytes[$nameLength++] = $b
        }
        [pscustomobject]@{
            Id = Read-Int32 $record
            Flags = Read-Int32 ($record + 4)
            X = Read-Float ($record + 8)
            Y = Read-Float ($record + 12)
            Z = Read-Float ($record + 16)
            Health = Read-Float ($record + 32)
            Name = [Text.Encoding]::UTF8.GetString($nameBytes, 0, $nameLength)
        }
    }
} finally {
    [JKCraftMapping]::UnmapViewOfFile($view) | Out-Null
    [JKCraftMapping]::CloseHandle($handle) | Out-Null
}
