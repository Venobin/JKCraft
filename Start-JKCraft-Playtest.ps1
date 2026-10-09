param(
    [string]$Nickname = 'Jaden',
    [string]$JediAcademyGameData,
    [string]$MinecraftDataRoot,
    [int]$Width = 3440,
    [int]$Height = 1440,
    [int]$Fullscreen = 1,
    [int]$ConsoleLogs = 0,
    [switch]$CheckOnly
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$fabric = Join-Path $root 'SkyCraft-main\fabric'
$javaHome = Join-Path $root '.tools\temurin-25\jdk-25.0.4.1+1'
$gradleHome = Join-Path $root '.gradle-cache'
$client = Join-Path $root 'dist\JKCraft-0.1.2'
$openJKRoot = Join-Path $client 'OpenJK'
$openJK = Join-Path $openJKRoot 'openjk_sp.x86.exe'
if (-not $JediAcademyGameData) {
    $JediAcademyGameData = Join-Path $client 'GameResources\JediAcademy\GameData'
}
$gameData = $JediAcademyGameData

if ($Nickname -cnotmatch '^[A-Za-z0-9_]{3,16}$') {
    throw 'JKCraft: nickname must be 3-16 Latin letters, digits or underscores.'
}
if ($Width -lt 640 -or $Width -gt 3840 -or $Height -lt 480 -or $Height -gt 2160 -or
    $Fullscreen -notin @(0, 1) -or $ConsoleLogs -notin @(0, 1)) {
    throw 'JKCraft: invalid resolution or fullscreen setting.'
}
if ($MinecraftDataRoot) {
    $MinecraftDataRoot = [IO.Path]::GetFullPath($MinecraftDataRoot)
    New-Item -ItemType Directory -Path $MinecraftDataRoot -Force | Out-Null
}

foreach ($required in @(
    (Join-Path $gameData 'base\assets0.pk3'),
    (Join-Path $javaHome 'bin\java.exe'),
    (Join-Path $fabric 'gradlew.bat'),
    $openJK,
    (Join-Path $openJKRoot 'OpenJK\jagamex86.dll')
)) {
    if (-not (Test-Path -LiteralPath $required)) {
        throw "JKCraft: required file is missing: $required"
    }
}

if ($CheckOnly) {
    Write-Host "JKCraft: offline installation is ready for $Nickname."
    exit 0
}

$existingOpenJK = @(Get-Process openjk_sp.x86 -ErrorAction SilentlyContinue)
if ($existingOpenJK.Count) {
    throw 'JKCraft: OpenJK is already running. Close the old game window and try again.'
}

$pendingOpenJK = Join-Path $openJKRoot 'openjk_sp.x86.next.exe'
if (Test-Path -LiteralPath $pendingOpenJK) {
    Copy-Item -LiteralPath $pendingOpenJK -Destination $openJK -Force
    Remove-Item -LiteralPath $pendingOpenJK -Force
    Write-Host 'JKCraft: installed the pending OpenJK physics update.'
}

$pendingGameDll = Join-Path $openJKRoot 'OpenJK\jagamex86.next.dll'
$gameDll = Join-Path $openJKRoot 'OpenJK\jagamex86.dll'
if (Test-Path -LiteralPath $pendingGameDll) {
    Copy-Item -LiteralPath $pendingGameDll -Destination $gameDll -Force
    Remove-Item -LiteralPath $pendingGameDll -Force
    Write-Host 'JKCraft: installed the pending Jedi Academy gameplay update.'
}

$userData = Join-Path $client 'UserData'
New-Item -ItemType Directory -Force -Path (Join-Path $userData 'OpenJK') | Out-Null
Copy-Item -LiteralPath (Join-Path $openJKRoot 'OpenJK\jagamex86.dll') `
    -Destination (Join-Path $userData 'OpenJK\jagamex86.dll') -Force

$quotedGameData = '"' + $gameData + '"'
$quotedUserData = '"' + $userData + '"'
$arguments = @(
    '+set', 'fs_basepath', $quotedGameData,
    '+set', 'fs_homepath', $quotedUserData,
    '+set', 'fs_game', 'OpenJK',
    '+set', 'jkc_enabled', '1',
    '+set', 'jkc_puppet', '1',
	'+set', 'jkc_camera_sync', '1',
	# The Minecraft overlay follows this OpenJK viewport (up to 3840x2160).
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

$env:JAVA_HOME = $javaHome
$env:GRADLE_USER_HOME = $gradleHome
$env:JKCRAFT_PLAYTEST = '1'
$env:JKCRAFT_SHOW_MINECRAFT = '0'
$env:JKCRAFT_KEEP_MINECRAFT = '1'
$env:JKCRAFT_USERNAME = $Nickname
$env:JKCRAFT_CLIENT_CORE = $fabric
if ($MinecraftDataRoot) { $env:JKCRAFT_MINECRAFT_DATA = $MinecraftDataRoot }

Push-Location $fabric
try {
    Write-Output 'JKCRAFT_STAGE:PREPARING'
    & (Join-Path $fabric 'gradlew.bat') --offline --no-daemon --console=plain classes
    if ($LASTEXITCODE -ne 0) { throw "Minecraft preparation failed with code $LASTEXITCODE" }

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
                throw 'JKCraft: OpenJK and Minecraft protocol versions differ. Rebuild all OpenJK components.'
            }
        }
    }

    Write-Output 'JKCRAFT_STAGE:MINECRAFT'
    Write-Host 'JKCraft: starting Minecraft. Jedi Academy will wait until the client is ready.'
    $minecraftJob = Start-Job -ScriptBlock {
        $clientCore = $env:JKCRAFT_CLIENT_CORE
        Push-Location $clientCore
        try {
            & (Join-Path $clientCore 'gradlew.bat') --offline --no-daemon --console=plain runClient
            if ($LASTEXITCODE -ne 0) { throw "Minecraft exited with code $LASTEXITCODE" }
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
            throw 'JKCraft: Minecraft did not become ready; OpenJK was not started.'
        }

        Write-Output 'JKCRAFT_STAGE:OPENJK'
        Write-Host 'JKCraft: Minecraft is ready; starting Jedi Academy.'
        $openJKProcess = Start-Process -FilePath $openJK -WorkingDirectory $openJKRoot `
            -ArgumentList $arguments -PassThru
        try {
            $gameClosed = $false
            while ($minecraftJob.State -eq 'Running') {
                Receive-MinecraftOutput $minecraftJob
                if ($openJKProcess.HasExited) {
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
                throw 'JKCraft: Minecraft exited with an error.'
            }
        } finally {
            if (-not $openJKProcess.HasExited) {
                Stop-Process -Id $openJKProcess.Id -ErrorAction SilentlyContinue
            }
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
        & (Join-Path $fabric 'gradlew.bat') --offline --stop
    }
} finally {
    Pop-Location
}
