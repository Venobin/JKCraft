param([string]$MappingName = 'Local\JKCraft_v1')

$typeDefinition = @'
using System;
using System.Runtime.InteropServices;
public static class JKCraftWorldEntityMapping {
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    public static extern IntPtr OpenFileMapping(uint access, bool inherit, string name);
    [DllImport("kernel32.dll", SetLastError = true)]
    public static extern IntPtr MapViewOfFile(IntPtr mapping, uint access, uint high, uint low, UIntPtr bytes);
    [DllImport("kernel32.dll")] public static extern bool UnmapViewOfFile(IntPtr address);
    [DllImport("kernel32.dll")] public static extern bool CloseHandle(IntPtr handle);
}
'@
if (-not ('JKCraftWorldEntityMapping' -as [type])) { Add-Type -TypeDefinition $typeDefinition }

$read = 4
$handle = [JKCraftWorldEntityMapping]::OpenFileMapping($read, $false, $MappingName)
if ($handle -eq [IntPtr]::Zero) { throw "JKCraft mapping '$MappingName' is unavailable." }
$view = [JKCraftWorldEntityMapping]::MapViewOfFile($handle, $read, 0, 0, [UIntPtr]::Zero)
if ($view -eq [IntPtr]::Zero) { [JKCraftWorldEntityMapping]::CloseHandle($handle) | Out-Null; throw 'MapViewOfFile failed.' }
try {
    $base = 0x1C000
    $seq = [Runtime.InteropServices.Marshal]::ReadInt32([IntPtr]::Add($view, $base))
    $count = [Math]::Min(160, [Runtime.InteropServices.Marshal]::ReadInt32([IntPtr]::Add($view, $base + 4)))
    "seq=$seq count=$count"
    for ($i = 0; $i -lt $count; $i++) {
        $r = $base + 0x40 + $i * 96
        $kind = [Runtime.InteropServices.Marshal]::ReadInt32([IntPtr]::Add($view, $r))
        $id = [Runtime.InteropServices.Marshal]::ReadInt32([IntPtr]::Add($view, $r + 4))
        $f = { param($o) [BitConverter]::Int32BitsToSingle([Runtime.InteropServices.Marshal]::ReadInt32([IntPtr]::Add($view, $r + $o))) }
        [pscustomobject]@{ Index=$i; Kind=$kind; Id=$id; X=&$f 8; Y=&$f 12; Z=&$f 16; Yaw=&$f 20; Pitch=&$f 24 }
    }
} finally {
    [JKCraftWorldEntityMapping]::UnmapViewOfFile($view) | Out-Null
    [JKCraftWorldEntityMapping]::CloseHandle($handle) | Out-Null
}
