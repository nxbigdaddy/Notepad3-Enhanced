param([string]$Executable = 'Notepad3/Bin/Release_x64_v143/Notepad3.exe')
$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path $PSScriptRoot -Parent
$taskExe = [IO.Path]::GetFullPath((Join-Path $taskRoot $Executable))
$taskFolder = Join-Path $taskRoot ('artifacts/workspace-options-test-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $taskFolder | Out-Null
[IO.File]::WriteAllText((Join-Path $taskFolder 'workspace-test.marker'), 'isolated workspace option regression')
function New-TestIni([string]$Name, [bool]$Reopen, [bool]$Save) {
    $taskIni = Join-Path $taskFolder ($Name + '.ini')
    [IO.File]::WriteAllText($taskIni, "[Settings]`r`nSaveSettings=$Save`r`n[Settings2]`r`nReopenWorkspace=$Reopen`r`n", [Text.UTF8Encoding]::new($true))
    return $taskIni
}
function Run-Stage([string]$Stage, [string]$Ini) {
    $taskOldFolder = $env:NP3_WORKSPACE_SELFTEST_DIR
    $taskOldStage = $env:NP3_WORKSPACE_OPTION_TEST
    try {
        $env:NP3_WORKSPACE_SELFTEST_DIR = $taskFolder
        $env:NP3_WORKSPACE_OPTION_TEST = $Stage
        $taskProcess = Start-Process -FilePath $taskExe -ArgumentList @('/n', '/f', ('"' + $Ini + '"')) -WindowStyle Hidden -PassThru
    } finally {
        if ($null -eq $taskOldFolder) { Remove-Item Env:NP3_WORKSPACE_SELFTEST_DIR -ErrorAction SilentlyContinue } else { $env:NP3_WORKSPACE_SELFTEST_DIR = $taskOldFolder }
        if ($null -eq $taskOldStage) { Remove-Item Env:NP3_WORKSPACE_OPTION_TEST -ErrorAction SilentlyContinue } else { $env:NP3_WORKSPACE_OPTION_TEST = $taskOldStage }
    }
    if (!$taskProcess.WaitForExit(60000)) { throw "Stage $Stage still running, PID $($taskProcess.Id). Inspect $taskFolder" }
    $taskLog = Join-Path $taskFolder 'workspace-test.log'
    $taskResult = [IO.File]::ReadAllText($taskLog)
    [IO.File]::WriteAllText((Join-Path $taskFolder ($Stage + '-' + [IO.Path]::GetFileNameWithoutExtension($Ini) + '.log')), $taskResult)
    Write-Output $taskResult
    if ($taskResult -notmatch 'RESULT checks=\d+ failures=0') { throw "Stage $Stage failed: $taskFolder" }
}
function Assert-Reopen([string]$Ini, [bool]$Expected) {
    $taskContent = [IO.File]::ReadAllText($Ini)
    $taskMatch = [regex]::Match($taskContent, '(?im)^ReopenWorkspace\s*=\s*(true|false|0|1)\s*$')
    if (!$taskMatch.Success) { throw "Saved reopen setting missing: $Ini" }
    $taskValue = $taskMatch.Groups[1].Value.ToLowerInvariant() -in @('true', '1')
    if ($taskValue -ne $Expected) { throw "Saved reopen setting incorrect: $Ini" }
    Write-Output "PASS independent INI readback ReopenWorkspace=$Expected"
}
$taskPersist = New-TestIni 'persist' $false $true
Run-Stage 'toggle-on' $taskPersist
Assert-Reopen $taskPersist $true
Run-Stage 'read-on' $taskPersist
Run-Stage 'toggle-off' $taskPersist
Assert-Reopen $taskPersist $false
Run-Stage 'read-off' $taskPersist
$taskDisabled = New-TestIni 'disabled' $false $false
Run-Stage 'toggle-unsaved' $taskDisabled
Assert-Reopen $taskDisabled $false
Run-Stage 'read-off' $taskDisabled
Run-Stage 'force' $taskDisabled
Assert-Reopen $taskDisabled $true
Run-Stage 'read-on' $taskDisabled
Write-Output "PASS workspace options eight-process menu/persistence checks. Evidence: $taskFolder"
