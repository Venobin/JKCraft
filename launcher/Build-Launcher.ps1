$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$jdkBin = Join-Path $root '.tools\temurin-25\jdk-25.0.4.1+1\bin'
$classes = Join-Path $PSScriptRoot 'build\launcher'
$jar = Join-Path $root 'JKCraft-OfflineLauncher.jar'

New-Item -ItemType Directory -Path $classes -Force | Out-Null
& (Join-Path $jdkBin 'javac.exe') --release 17 -Xlint:-options -encoding UTF-8 `
    -d $classes (Join-Path $PSScriptRoot 'src\JKCraftLauncher.java')
if ($LASTEXITCODE -ne 0) { throw 'JKCraft launcher compilation failed.' }
& (Join-Path $jdkBin 'jar.exe') --create --file $jar --main-class JKCraftLauncher -C $classes .
if ($LASTEXITCODE -ne 0) { throw 'JKCraft launcher JAR creation failed.' }
Write-Host 'JKCraft launcher built for bundled Java 25 (Java 17+ compatible).'
