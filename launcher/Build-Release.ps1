param([string]$Destination)

$ErrorActionPreference = 'Stop'
$workspace = Split-Path -Parent $PSScriptRoot
$releaseRoot = Join-Path $workspace 'releases'
if (-not $Destination) { $Destination = Join-Path $releaseRoot 'JKCraft-0.1.2-alpha' }
$Destination = [IO.Path]::GetFullPath($Destination)
$releaseRoot = [IO.Path]::GetFullPath($releaseRoot)
if (-not $Destination.StartsWith($releaseRoot + [IO.Path]::DirectorySeparatorChar,
        [StringComparison]::OrdinalIgnoreCase)) {
    throw "Release destination must be inside $releaseRoot"
}
if (Test-Path -LiteralPath $Destination) {
    throw "Release destination already exists; nothing was overwritten: $Destination"
}

& (Join-Path $PSScriptRoot 'Build-Launcher.ps1')
if ($LASTEXITCODE -ne 0) { throw 'Launcher build failed.' }

$oldClient = Join-Path $workspace 'dist\JKCraft-0.1.2'
$core = Join-Path $workspace 'SkyCraft-main\fabric'
$source = Join-Path $workspace 'OpenJK-master'
$openJkBuild = Join-Path $source 'build-jedicraft-ninja'
$assets = Join-Path $PSScriptRoot 'release_assets'
$java = Join-Path $workspace '.tools\temurin-25\jdk-25.0.4.1+1'
foreach ($required in @(
    (Join-Path $openJkBuild 'openjk_sp.x86.exe'),
    (Join-Path $openJkBuild 'jagamex86.dll'),
    (Join-Path $openJkBuild 'rdsp-vanilla_x86.dll'),
    (Join-Path $oldClient 'mods\fabric-api-0.161.0+26.3.jar'),
    (Join-Path $core 'build\libs\jkcraft-0.1.2.jar'),
    (Join-Path $java 'bin\java.exe')
)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
        throw "Required release input is missing: $required"
    }
}

New-Item -ItemType Directory -Path $Destination -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $oldClient 'OpenJK') -Destination $Destination -Recurse
# All three OpenJK binaries read the shared-memory protocol. Never take the
# renderer from an older packaged client while using a newly built game DLL.
Copy-Item -LiteralPath (Join-Path $openJkBuild 'openjk_sp.x86.exe') `
    -Destination (Join-Path $Destination 'OpenJK\openjk_sp.x86.exe') -Force
Copy-Item -LiteralPath (Join-Path $openJkBuild 'jagamex86.dll') `
    -Destination (Join-Path $Destination 'OpenJK\OpenJK\jagamex86.dll') -Force
Copy-Item -LiteralPath (Join-Path $openJkBuild 'rdsp-vanilla_x86.dll') `
    -Destination (Join-Path $Destination 'OpenJK\rdsp-vanilla_x86.dll') -Force
Copy-Item -LiteralPath (Join-Path $oldClient 'licenses') -Destination $Destination -Recurse
Copy-Item -LiteralPath (Join-Path $workspace 'LICENSE') `
    -Destination (Join-Path $Destination 'licenses\JKCraft-LICENSE.txt') -Force
Copy-Item -LiteralPath (Join-Path $workspace 'SkyCraft-main\LICENSE') `
    -Destination (Join-Path $Destination 'licenses\SkyCraft-LICENSE.txt')
New-Item -ItemType Directory -Path (Join-Path $Destination 'mods'),
    (Join-Path $Destination 'ClientCore'), (Join-Path $Destination 'Runtime'),
    (Join-Path $Destination 'GameResources\JediAcademy\GameData\base'),
    (Join-Path $Destination 'GameResources\Minecraft\GameData'),
    (Join-Path $Destination 'UserData') -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $core 'build\libs\jkcraft-0.1.2.jar') `
    -Destination (Join-Path $Destination 'mods\jkcraft-0.1.2.jar')
Copy-Item -LiteralPath (Join-Path $oldClient 'mods\fabric-api-0.161.0+26.3.jar') `
    -Destination (Join-Path $Destination 'mods\fabric-api-0.161.0+26.3.jar')
Copy-Item -LiteralPath $java -Destination (Join-Path $Destination 'Runtime\Java25') -Recurse
foreach ($item in @('src', 'gradle', 'build.gradle', 'gradle.properties', 'gradlew.bat', 'settings.gradle')) {
    Copy-Item -LiteralPath (Join-Path $core $item) -Destination (Join-Path $Destination 'ClientCore') -Recurse
}
Copy-Item -LiteralPath (Join-Path $workspace 'JKCraft-OfflineLauncher.jar') `
    -Destination (Join-Path $Destination 'JKCraft-Launcher.jar')
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'Launch-JKCraft-Offline.ps1') `
    -Destination (Join-Path $Destination 'Launch-JKCraft-Offline.ps1')
Copy-Item -LiteralPath (Join-Path $assets 'JKCraft.cmd'),
    (Join-Path $assets 'INSTALLATION_RU.txt'), (Join-Path $assets 'INSTALLATION_EN.txt'),
    (Join-Path $assets 'README.txt') -Destination $Destination
Copy-Item -LiteralPath (Join-Path $assets 'PUT_JEDI_ACADEMY_ASSETS_HERE.txt') `
    -Destination (Join-Path $Destination 'GameResources\JediAcademy\GameData\base')
Copy-Item -LiteralPath (Join-Path $assets 'MINECRAFT_DATA_README.txt') `
    -Destination (Join-Path $Destination 'GameResources\Minecraft\GameData')

$total = Get-ChildItem -LiteralPath $Destination -Recurse -File | Measure-Object Length -Sum
Write-Host ("JKCraft release staged: {0} ({1:N1} MiB, {2} files)" -f `
    $Destination, ($total.Sum / 1MB), $total.Count)
