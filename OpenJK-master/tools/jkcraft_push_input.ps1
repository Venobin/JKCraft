param(
    [Parameter(Mandatory = $true)][int]$Type,
    [int]$Code = 0,
    [int]$A = 0,
    [int]$B = 0,
    [int]$C = 0,
    [string]$MappingName = 'Local\JKCraft_v1'
)

$typeDefinition = @'
using System;
using System.Runtime.InteropServices;

public static class JKCraftInputMapping {
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

if (-not ('JKCraftInputMapping' -as [type])) {
    Add-Type -TypeDefinition $typeDefinition
}

$allAccess = 0xF001F
$handle = [JKCraftInputMapping]::OpenFileMapping($allAccess, $false, $MappingName)
if ($handle -eq [IntPtr]::Zero) {
    throw "JKCraft mapping '$MappingName' is not available (Win32 $([Runtime.InteropServices.Marshal]::GetLastWin32Error()))."
}
$view = [JKCraftInputMapping]::MapViewOfFile($handle, $allAccess, 0, 0, [UIntPtr]::Zero)
if ($view -eq [IntPtr]::Zero) {
    [JKCraftInputMapping]::CloseHandle($handle) | Out-Null
    throw "MapViewOfFile failed (Win32 $([Runtime.InteropServices.Marshal]::GetLastWin32Error()))."
}

try {
    $ring = 0x1000
    $head = [Runtime.InteropServices.Marshal]::ReadInt64([IntPtr]::Add($view, $ring))
    $tail = [Runtime.InteropServices.Marshal]::ReadInt64([IntPtr]::Add($view, $ring + 0x40))
    if ($head - $tail -ge 4096) {
        throw 'JKCraft input ring is full.'
    }
    $event = $ring + 0x80 + (($head -band 4095) * 16)
    [Runtime.InteropServices.Marshal]::WriteInt16([IntPtr]::Add($view, $event), [int16]$Type)
    [Runtime.InteropServices.Marshal]::WriteInt16([IntPtr]::Add($view, $event + 2), [int16]$Code)
    [Runtime.InteropServices.Marshal]::WriteInt32([IntPtr]::Add($view, $event + 4), $A)
    [Runtime.InteropServices.Marshal]::WriteInt32([IntPtr]::Add($view, $event + 8), $B)
    [Runtime.InteropServices.Marshal]::WriteInt32([IntPtr]::Add($view, $event + 12), $C)
    [Threading.Thread]::MemoryBarrier()
    [Runtime.InteropServices.Marshal]::WriteInt64([IntPtr]::Add($view, $ring), $head + 1)
    "queued input type={0} code={1} a={2} at sequence={3}" -f $Type, $Code, $A, $head
} finally {
    [JKCraftInputMapping]::UnmapViewOfFile($view) | Out-Null
    [JKCraftInputMapping]::CloseHandle($handle) | Out-Null
}
