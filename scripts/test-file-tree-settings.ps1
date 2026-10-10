param([string]$Executable = 'Notepad3/Bin/Release_x64_v143/Notepad3.exe')
$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path $PSScriptRoot -Parent
$taskExe = [IO.Path]::GetFullPath((Join-Path $taskRoot $Executable))
$taskFolder = Join-Path $taskRoot ('artifacts/file-tree-settings-test-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $taskFolder, (Join-Path $taskFolder '自选目录') | Out-Null
[IO.File]::WriteAllText((Join-Path $taskFolder 'workspace-test.marker'), 'isolated file tree settings regression')
$taskChecks = 0
function Assert-Setting($Condition, [string]$Label) {
    if (!$Condition) { throw "FAIL $Label. Evidence: $taskFolder" }
    $script:taskChecks++
    Write-Output "PASS $Label"
}
function Read-Setting([string]$Ini, [string]$Key) {
    $taskContent = [IO.File]::ReadAllText($Ini)
    $taskMatch = [regex]::Match($taskContent, '(?m)^' + [regex]::Escape($Key) + '\s*=\s*([^\r\n]*)')
    if (!$taskMatch.Success) { return '' }
    return $taskMatch.Groups[1].Value.Trim().Trim('"')
}
function New-TestIni([string]$Name, [string]$Width, [string]$Root, [bool]$Save = $true) {
    $taskIni = Join-Path $taskFolder ($Name + '.ini')
    [IO.File]::WriteAllText($taskIni, "[Settings]`r`nSaveSettings=$Save`r`n[Settings2]`r`nReopenWorkspace=false`r`nFileTreeWidth=$Width`r`nFileTreeRoot=$Root`r`n", [Text.UTF8Encoding]::new($true))
    return $taskIni
}
function Run-Stage([string]$Stage, [string]$Ini) {
    $taskOldFolder = $env:NP3_WORKSPACE_SELFTEST_DIR
    $taskOldStage = $env:NP3_FILETREE_SETTINGS_TEST
    try {
        $env:NP3_WORKSPACE_SELFTEST_DIR = $taskFolder
        $env:NP3_FILETREE_SETTINGS_TEST = $Stage
        $taskProcess = Start-Process -FilePath $taskExe -ArgumentList @('/n', '/f', ('"' + $Ini + '"')) -WindowStyle Hidden -PassThru
    } finally {
        if ($null -eq $taskOldFolder) { Remove-Item Env:NP3_WORKSPACE_SELFTEST_DIR -ErrorAction SilentlyContinue } else { $env:NP3_WORKSPACE_SELFTEST_DIR = $taskOldFolder }
        if ($null -eq $taskOldStage) { Remove-Item Env:NP3_FILETREE_SETTINGS_TEST -ErrorAction SilentlyContinue } else { $env:NP3_FILETREE_SETTINGS_TEST = $taskOldStage }
    }
    if (!$taskProcess.WaitForExit(60000)) { throw "Stage $Stage still running, PID $($taskProcess.Id). Inspect $taskFolder before retrying." }
    $taskLog = Join-Path $taskFolder 'workspace-test.log'
    $taskResult = [IO.File]::ReadAllText($taskLog)
    [IO.File]::WriteAllText((Join-Path $taskFolder ($Stage + '.log')), $taskResult)
    Assert-Setting ($taskProcess.ExitCode -eq 0 -and $taskResult -match 'RESULT checks=\d+ failures=0') "$Stage native process checks"
    Write-Output $taskResult
}
$taskChosen = Join-Path $taskFolder '自选目录'
$taskIni = New-TestIni 'persist' '240' ''
Run-Stage 'write' $taskIni
$taskWidth = Read-Setting $taskIni 'FileTreeWidth'
Assert-Setting ([int]$taskWidth -gt 240) 'normal exit writes confirmed width to independent INI readback'
Assert-Setting ((Read-Setting $taskIni 'FileTreeRoot') -eq $taskChosen) 'normal exit writes Unicode selected directory'
Run-Stage 'read' $taskIni
Assert-Setting ((Read-Setting $taskIni 'FileTreeWidth') -eq $taskWidth -and (Read-Setting $taskIni 'FileTreeRoot') -eq $taskChosen) 'second process retains persisted tree preferences'
$taskMissingIni = New-TestIni 'missing' '320' (Join-Path $taskFolder 'missing-directory')
Run-Stage 'missing' $taskMissingIni
Assert-Setting ((Read-Setting $taskMissingIni 'FileTreeRoot') -eq $taskFolder) 'unavailable stored root replaced with valid fallback'
$taskLowIni = New-TestIni 'low' '-200' $taskFolder
Run-Stage 'low' $taskLowIni
Assert-Setting ((Read-Setting $taskLowIni 'FileTreeWidth') -eq '120') 'negative width clamped and saved'
$taskHighIni = New-TestIni 'high' '999999' $taskFolder
Run-Stage 'high' $taskHighIni
Assert-Setting ((Read-Setting $taskHighIni 'FileTreeWidth') -eq '10000') 'oversized width clamped and saved'
$taskDisabledIni = New-TestIni 'disabled' '240' $taskFolder $false
Run-Stage 'disabled' $taskDisabledIni
Assert-Setting ((Read-Setting $taskDisabledIni 'FileTreeWidth') -eq '240' -and (Read-Setting $taskDisabledIni 'FileTreeRoot') -eq $taskFolder) 'disabled settings saving leaves original preferences unchanged'
Write-Output "RESULT independent_checks=$taskChecks failures=0 Evidence: $taskFolder"
