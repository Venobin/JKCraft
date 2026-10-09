param(
    [Parameter(Mandatory = $true)][int]$Type,
    [int]$FormId = 0,
    [single]$A = 0,
    [single]$B = 0,
    [single]$C = 0,
    [single]$D = 0,
    [int]$Flags = 0,
    [int]$Weapon = 0,
    [string]$MappingName = 'Local\JKCraft_v1'
)

$typeDefinition = @'
using System;
using System.Runtime.InteropServices;

public static class JKCraftEventMapping {
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

if (-not ('JKCraftEventMapping' -as [type])) {
    Add-Type -TypeDefinition $typeDefinition
}

$allAccess = 0xF001F
$handle = [JKCraftEventMapping]::OpenFileMapping($allAccess, $false, $MappingName)
if ($handle -eq [IntPtr]::Zero) {
    throw "JKCraft mapping '$MappingName' is not available (Win32 $([Runtime.InteropServices.Marshal]::GetLastWin32Error()))."
}
$view = [JKCraftEventMapping]::MapViewOfFile($handle, $allAccess, 0, 0, [UIntPtr]::Zero)
if ($view -eq [IntPtr]::Zero) {
    [JKCraftEventMapping]::CloseHandle($handle) | Out-Null
    throw "MapViewOfFile failed (Win32 $([Runtime.InteropServices.Marshal]::GetLastWin32Error()))."
}

try {
    $ring = 0x17000
    $head = [Runtime.InteropServices.Marshal]::ReadInt64([IntPtr]::Add($view, $ring))
    $tail = [Runtime.InteropServices.Marshal]::ReadInt64([IntPtr]::Add($view, $ring + 0x40))
    if ($head - $tail -ge 512) {
        throw 'JKCraft event ring is full.'
    }
    $event = $ring + 0x80 + (($head -band 511) * 32)
    [Runtime.InteropServices.Marshal]::WriteInt32([IntPtr]::Add($view, $event), $Type)
    [Runtime.InteropServices.Marshal]::WriteInt32([IntPtr]::Add($view, $event + 4), $FormId)
    [Runtime.InteropServices.Marshal]::WriteInt32([IntPtr]::Add($view, $event + 8), [BitConverter]::SingleToInt32Bits($A))
    [Runtime.InteropServices.Marshal]::WriteInt32([IntPtr]::Add($view, $event + 12), [BitConverter]::SingleToInt32Bits($B))
    [Runtime.InteropServices.Marshal]::WriteInt32([IntPtr]::Add($view, $event + 16), [BitConverter]::SingleToInt32Bits($C))
    [Runtime.InteropServices.Marshal]::WriteInt32([IntPtr]::Add($view, $event + 20), [BitConverter]::SingleToInt32Bits($D))
    [Runtime.InteropServices.Marshal]::WriteInt32([IntPtr]::Add($view, $event + 24), $Flags)
    [Runtime.InteropServices.Marshal]::WriteInt32([IntPtr]::Add($view, $event + 28), $Weapon)
    [Threading.Thread]::MemoryBarrier()
    [Runtime.InteropServices.Marshal]::WriteInt64([IntPtr]::Add($view, $ring), $head + 1)
    "queued event type={0} formId={1} at sequence={2}" -f $Type, $FormId, $head
} finally {
    [JKCraftEventMapping]::UnmapViewOfFile($view) | Out-Null
    [JKCraftEventMapping]::CloseHandle($handle) | Out-Null
}
