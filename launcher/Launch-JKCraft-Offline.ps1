param(
    [string]$Nickname = 'Jaden',
    [int]$Width = 1280,
    [int]$Height = 720,
    [int]$Fullscreen = 0,
    [int]$ConsoleLogs = 0,
    [switch]$CheckOnly
)

$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new($false)
$OutputEncoding = [Console]::OutputEncoding
if ($args.Count -gt 0) {
    throw 'JKCraft: external game resource paths are not supported.'
}
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$core = Join-Path $root 'ClientCore'
$javaHome = Join-Path $root 'Runtime\Java25'
$openJKRoot = Join-Path $root 'OpenJK'
$openJK = Join-Path $openJKRoot 'openjk_sp.x86.exe'
$userData = Join-Path $root 'UserData'
$stopMarker = Join-Path $userData 'stop-request.txt'
$readyMarker = Join-Path $userData 'minecraft-runtime-ready.txt'
$JediAcademyGameData = Join-Path $root 'GameResources\JediAcademy\GameData'
$MinecraftDataRoot = Join-Path $root 'GameResources\Minecraft\GameData'

if ($Nickname -cnotmatch '^[A-Za-z0-9_]{3,16}$') {
    throw 'JKCraft: nickname must contain 3-16 Latin letters, digits or underscores.'
}
if ($Width -lt 640 -or $Width -gt 3840 -or $Height -lt 480 -or $Height -gt 2160 -or
    $Fullscreen -notin @(0, 1) -or $ConsoleLogs -notin @(0, 1)) {
    throw 'JKCraft: invalid resolution or fullscreen setting.'
}
foreach ($required in @(
    (Join-Path $JediAcademyGameData 'base\assets0.pk3'),
    (Join-Path $JediAcademyGameData 'base\assets1.pk3'),
    (Join-Path $JediAcademyGameData 'base\assets2.pk3'),
    (Join-Path $JediAcademyGameData 'base\assets3.pk3'),
    (Join-Path $javaHome 'bin\java.exe'),
    (Join-Path $core 'gradlew.bat'),
    $openJK,
    (Join-Path $openJKRoot 'OpenJK\jagamex86.dll'),
    (Join-Path $openJKRoot 'rdsp-vanilla_x86.dll'),
    (Join-Path $root 'mods\jkcraft-0.1.2.jar')
)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
        throw "JKCraft: required file is missing: $required"
    }
}

if ($CheckOnly) {
    Write-Host "JKCraft: package layout OK for $Nickname. Minecraft dependencies are checked on launch."
    exit 0
}

if (Get-Process openjk_sp.x86 -ErrorAction SilentlyContinue) {
    throw 'JKCraft: OpenJK is already running. Close the previous game first.'
}

New-Item -ItemType Directory -Path $MinecraftDataRoot, (Join-Path $userData 'OpenJK') -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $openJKRoot 'OpenJK\jagamex86.dll') `
    -Destination (Join-Path $userData 'OpenJK\jagamex86.dll') -Force

$env:JAVA_HOME = $javaHome
$env:GRADLE_USER_HOME = Join-Path $root 'RuntimeCache'
$env:JKCRAFT_MINECRAFT_DATA = [IO.Path]::GetFullPath($MinecraftDataRoot)
$env:JKCRAFT_PLAYTEST = '1'
$env:JKCRAFT_SHOW_MINECRAFT = '0'
$env:JKCRAFT_KEEP_MINECRAFT = '1'
$env:JKCRAFT_USERNAME = $Nickname
$gradleArgs = @('--no-daemon', '--console=plain')
if (Test-Path -LiteralPath $readyMarker) { $gradleArgs += '--offline' }

Push-Location $core
try {
    Write-Output 'JKCRAFT_STAGE:PREPARING'
    Write-Host 'JKCraft: preparing the Minecraft 26.3 runtime. First setup may download dependencies.'
    $prepareArgs = @($gradleArgs)
    if (-not (Test-Path -LiteralPath $readyMarker)) { $prepareArgs += '--info' }
    & (Join-Path $core 'gradlew.bat') @prepareArgs classes
    if ($LASTEXITCODE -ne 0) { throw "JKCraft: Minecraft preparation failed ($LASTEXITCODE)." }

    $env:JKCRAFT_CLIENT_CORE = $core
    $env:JKCRAFT_GRADLE_OFFLINE = if (Test-Path -LiteralPath $readyMarker) { '1' } else { '0' }
    $script:minecraftReady = $false
    $script:minecraftProcess = $null
    function Receive-MinecraftOutput {
        param([System.Management.Automation.Job]$MinecraftJob)
        foreach ($entry in @(Receive-Job -Job $MinecraftJob -ErrorAction Continue)) {
            $line = [string]$entry
            Write-Output $line
            if ($line.Contains('JKCRAFT_MINECRAFT_READY')) {
                $script:minecraftReady = $true
                if ($line -match 'JKCRAFT_MINECRAFT_READY pid=(\d+)') {
                    $script:minecraftProcess = Get-Process -Id ([int]$Matches[1]) -ErrorAction SilentlyContinue
                }
            }
            if ($line.Contains('JKCraft: protocol mismatch')) {
                throw 'JKCraft: OpenJK and Minecraft protocol versions differ. Replace the whole client folder with one matching release and close older JKCraft processes.'
            }
        }
    }

    Write-Output 'JKCRAFT_STAGE:MINECRAFT'
    Write-Host "JKCraft: starting offline Minecraft as $Nickname. OpenJK will wait for the first ready frame."
    $minecraftJob = Start-Job -ScriptBlock {
        $clientCore = $env:JKCRAFT_CLIENT_CORE
        $clientArgs = @('--no-daemon', '--console=plain')
        if ($env:JKCRAFT_GRADLE_OFFLINE -eq '1') { $clientArgs += '--offline' }
        Push-Location $clientCore
        try {
            & (Join-Path $clientCore 'gradlew.bat') @clientArgs runClient
            if ($LASTEXITCODE -ne 0) { throw "Minecraft exited with code $LASTEXITCODE." }
        } finally {
            Pop-Location
        }
    }
    try {
        $deadline = [DateTime]::UtcNow.AddMinutes(10)
        while (-not $script:minecraftReady -and [DateTime]::UtcNow -lt $deadline) {
            Receive-MinecraftOutput $minecraftJob
            if ($script:minecraftReady -or $minecraftJob.State -in @('Completed', 'Failed', 'Stopped')) { break }
            Start-Sleep -Milliseconds 250
        }
        Receive-MinecraftOutput $minecraftJob
        if (-not $script:minecraftReady -or -not $script:minecraftProcess -or
            $minecraftJob.State -ne 'Running') {
            throw 'JKCraft: Minecraft did not become ready; OpenJK was not started. See UserData/launcher.log.'
        }

        Write-Output 'JKCRAFT_STAGE:OPENJK'
        $openJKArgs = @(
            '+set', 'fs_basepath', ('"' + $JediAcademyGameData + '"'),
            '+set', 'fs_homepath', ('"' + $userData + '"'),
            '+set', 'fs_game', 'OpenJK',
            '+set', 'jkc_enabled', '1',
            '+set', 'jkc_puppet', '1',
            '+set', 'jkc_camera_sync', '1',
            '+set', 'r_mode', '-1',
            '+set', 'r_customwidth', [string]$Width,
            '+set', 'r_customheight', [string]$Height,
            '+set', 'r_fullscreen', [string]$Fullscreen,
            '+set', 'jkc_console_logs', [string]$ConsoleLogs,
            '+set', 'logfile', $(if ($ConsoleLogs) { '2' } else { '0' }),
            '+bind', 'r', '"+use"',
            '+bind', 'g', '"+useforce"',
            '+bind', 'TAB', '"datapad"',
            '+bind', 'MWHEELUP', 'forcenext',
            '+bind', 'MWHEELDOWN', 'forceprev'
        )
        $game = Start-Process -FilePath $openJK -WorkingDirectory $openJKRoot `
            -ArgumentList $openJKArgs -PassThru
        Write-Output "JKCRAFT_OPENJK_PID:$($game.Id)"
        try {
            $gameClosed = $false
            while ($minecraftJob.State -eq 'Running') {
                Receive-MinecraftOutput $minecraftJob
                if (Test-Path -LiteralPath $stopMarker) {
                    Write-Output 'JKCraft: launcher requested game shutdown.'
                    if (-not $game.HasExited) {
                        $game.CloseMainWindow() | Out-Null
                        if (-not $game.WaitForExit(4000)) {
                            Stop-Process -Id $game.Id -ErrorAction SilentlyContinue
                        }
                    }
                    $gameClosed = $true
                    break
                }
                if ($game.HasExited) {
                    $gameClosed = $true
                    Write-Output 'JKCraft: OpenJK closed; waiting for Minecraft to save and exit.'
                    break
                }
                Start-Sleep -Milliseconds 250
            }
            if ($gameClosed) {
                $shutdownDeadline = [DateTime]::UtcNow.AddSeconds(15)
                while ($minecraftJob.State -eq 'Running' -and
                    -not $script:minecraftProcess.HasExited -and
                    [DateTime]::UtcNow -lt $shutdownDeadline) {
                    Receive-MinecraftOutput $minecraftJob
                    Start-Sleep -Milliseconds 250
                }
                if ($minecraftJob.State -eq 'Running' -and -not $script:minecraftProcess.HasExited) {
                    $expectedJava = [IO.Path]::GetFullPath((Join-Path $javaHome 'bin\java.exe'))
                    if ([string]::Equals($script:minecraftProcess.Path, $expectedJava,
                        [StringComparison]::OrdinalIgnoreCase)) {
                        Write-Output 'JKCraft: hidden Minecraft did not exit; stopping its exact process.'
                        try {
                            $script:minecraftProcess.Kill()
                            $script:minecraftProcess.WaitForExit(5000) | Out-Null
                        }
                        catch [System.InvalidOperationException] { }
                    }
                }
            }
            Receive-MinecraftOutput $minecraftJob
            if (-not $gameClosed -and $minecraftJob.State -ne 'Completed') {
                throw 'JKCraft: Minecraft exited with an error. See UserData/launcher.log.'
            }
        } finally {
            if (-not $game.HasExited) {
                Stop-Process -Id $game.Id -ErrorAction SilentlyContinue
            }
        }
        if (-not (Test-Path -LiteralPath $readyMarker)) {
            Set-Content -LiteralPath $readyMarker -Value 'Minecraft 26.3 runtime prepared.' -Encoding ASCII
        }
    } finally {
        if ($minecraftJob.State -eq 'Running') { Stop-Job -Job $minecraftJob -ErrorAction SilentlyContinue }
        Remove-Job -Job $minecraftJob -Force -ErrorAction SilentlyContinue
        if ($script:minecraftProcess -and -not $script:minecraftProcess.HasExited) {
            $expectedJava = [IO.Path]::GetFullPath((Join-Path $javaHome 'bin\java.exe'))
            if ([string]::Equals($script:minecraftProcess.Path, $expectedJava,
                [StringComparison]::OrdinalIgnoreCase)) {
                Write-Output 'JKCraft: cleaning up the remaining Minecraft process.'
                try {
                    $script:minecraftProcess.Kill()
                    $script:minecraftProcess.WaitForExit(5000) | Out-Null
                } catch [System.InvalidOperationException] { }
            }
        }
        Write-Output 'JKCraft: stopping background Gradle workers.'
        & (Join-Path $core 'gradlew.bat') --offline --stop
    }
} finally {
    Pop-Location
}
