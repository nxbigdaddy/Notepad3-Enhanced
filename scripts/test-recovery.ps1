param([string]$Executable = 'artifacts/workspace-options-x64-v16/Notepad3.exe')
$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path $PSScriptRoot -Parent
$taskExe = [IO.Path]::GetFullPath((Join-Path $taskRoot $Executable))
if (!(Test-Path -LiteralPath $taskExe)) { throw "Missing executable: $taskExe" }
$taskFolder = Join-Path $taskRoot ('artifacts/recovery-test-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $taskFolder | Out-Null
[IO.File]::WriteAllText((Join-Path $taskFolder 'recovery-test.marker'), 'isolated process recovery regression')
$taskCorrupt = Join-Path $taskFolder 'np3-corrupt.np3recovery'
[IO.File]::WriteAllBytes($taskCorrupt, [byte[]](1, 2, 3, 4))
[IO.File]::WriteAllText((Join-Path $taskFolder 'normal-a.txt'), "alpha`r`n", [Text.UTF8Encoding]::new($false))
[IO.File]::WriteAllText((Join-Path $taskFolder 'normal-b.txt'), "beta`r`n", [Text.UnicodeEncoding]::new($false, $true))
[IO.File]::WriteAllText((Join-Path $taskFolder 'incoming.txt'), "incoming`r`n", [Text.UTF8Encoding]::new($false))
function Start-RecoveryTest([string]$Mode, [string]$Incoming = '') {
    $taskPreviousRoot = $env:NP3_RECOVERY_TEST_DIR
    $taskPreviousMode = $env:NP3_RECOVERY_TEST_MODE
    try {
        $env:NP3_RECOVERY_TEST_DIR = $taskFolder
        $env:NP3_RECOVERY_TEST_MODE = $Mode
        $taskArguments = @('/n', '/f0')
        if ($Mode -eq 'normal-disabled') { $taskArguments = @('/n', '/f', ('"' + (Join-Path $taskFolder 'disabled.ini') + '"')) }
        if ($Incoming) { $taskArguments += ('"' + $Incoming + '"') }
        Start-Process -FilePath $taskExe -ArgumentList $taskArguments -WindowStyle Hidden -PassThru
    } finally {
        if ($null -eq $taskPreviousRoot) { Remove-Item Env:NP3_RECOVERY_TEST_DIR -ErrorAction SilentlyContinue } else { $env:NP3_RECOVERY_TEST_DIR = $taskPreviousRoot }
        if ($null -eq $taskPreviousMode) { Remove-Item Env:NP3_RECOVERY_TEST_MODE -ErrorAction SilentlyContinue } else { $env:NP3_RECOVERY_TEST_MODE = $taskPreviousMode }
    }
}
function Read-RecoveryResult([string]$Mode) {
    $taskLog = Join-Path $taskFolder ($Mode + '.log')
    if (!(Test-Path -LiteralPath $taskLog)) { throw "Missing native result: $taskLog" }
    $taskResult = [IO.File]::ReadAllText($taskLog)
    Write-Output $taskResult
    if ($taskResult -notmatch 'RESULT checks=\d+ failures=0') { throw "Recovery checks failed: $taskLog" }
}
$taskSeed = Start-RecoveryTest 'seed'
$taskDeadline = [DateTime]::UtcNow.AddSeconds(30)
while (!(Test-Path -LiteralPath (Join-Path $taskFolder 'seed.ready'))) {
    if ($taskSeed.HasExited -or [DateTime]::UtcNow -gt $taskDeadline) { throw "Seed not ready. Inspect PID $($taskSeed.Id), $taskFolder" }
    Start-Sleep -Milliseconds 100
}
Read-RecoveryResult 'seed'
$taskBackup = @(Get-ChildItem -LiteralPath $taskFolder -Filter "np3-$($taskSeed.Id)-*.np3recovery")
if ($taskBackup.Count -ne 1) { throw 'Expected one owned backup' }
$taskProtected = [IO.File]::ReadAllBytes($taskBackup[0].FullName)
if ([Text.Encoding]::UTF8.GetString($taskProtected).Contains('unsaved alpha')) { throw 'Plaintext present in encrypted backup' }
$taskLive = Start-RecoveryTest 'live'
if (!$taskLive.WaitForExit(30000)) { throw "Live ownership test still running: PID $($taskLive.Id)" }
Read-RecoveryResult 'live'
if (!(Test-Path -LiteralPath $taskBackup[0].FullName)) { throw 'Live process backup was removed' }
# This process was created by this runner solely for a crash simulation.
Stop-Process -Id $taskSeed.Id -Force
if (!$taskSeed.WaitForExit(10000)) { throw 'Owned crash seed did not exit' }
$taskVerify = Start-RecoveryTest 'verify'
if (!$taskVerify.WaitForExit(30000)) { throw "Crash recovery test still running: PID $($taskVerify.Id)" }
Read-RecoveryResult 'verify'
if (Test-Path -LiteralPath $taskBackup[0].FullName) { throw 'Transferred orphan backup was not cleaned' }
$taskClean = Start-RecoveryTest 'clean'
if (!$taskClean.WaitForExit(30000)) { throw "Normal discard test still running: PID $($taskClean.Id)" }
Read-RecoveryResult 'clean'
$taskFailure = Start-RecoveryTest 'failure'
if (!$taskFailure.WaitForExit(30000)) { throw "Write failure test still running: PID $($taskFailure.Id)" }
Read-RecoveryResult 'failure'
foreach ($taskMode in @('normal-seed', 'normal-verify', 'normal-explicit')) {
    $taskIncoming = if ($taskMode -eq 'normal-explicit') { Join-Path $taskFolder 'incoming.txt' } else { '' }
    $taskNormal = Start-RecoveryTest $taskMode $taskIncoming
    if (!$taskNormal.WaitForExit(30000)) { throw "Normal session test still running: PID $($taskNormal.Id), mode $taskMode" }
    Read-RecoveryResult $taskMode
}
if ([IO.File]::ReadAllText((Join-Path $taskFolder 'normal-a.txt')) -ne "alpha`r`nSAVED") { throw 'Saved A disk content changed unexpectedly' }
if ([IO.File]::ReadAllText((Join-Path $taskFolder 'normal-b.txt')) -ne "beta`r`n") { throw 'Discarded B modified its original disk file' }
[IO.File]::WriteAllText((Join-Path $taskFolder 'normal-a.txt'), 'x', [Text.UTF8Encoding]::new($false))
Remove-Item -LiteralPath (Join-Path $taskFolder 'normal-b.txt')
$taskMissing = Start-RecoveryTest 'normal-missing'
if (!$taskMissing.WaitForExit(30000)) { throw "Missing file test still running: PID $($taskMissing.Id)" }
Read-RecoveryResult 'normal-missing'
[IO.File]::WriteAllText((Join-Path $taskFolder 'disabled.ini'), "[Settings2]`r`nReopenWorkspace=false`r`n", [Text.UTF8Encoding]::new($true))
$taskArchiveBefore = @(Get-ChildItem -LiteralPath $taskFolder -Filter '*.np3workspace' | Sort-Object Name | ForEach-Object Name)
$taskDisabled = Start-RecoveryTest 'normal-disabled'
if (!$taskDisabled.WaitForExit(30000)) { throw "Disabled reopening test still running: PID $($taskDisabled.Id)" }
Read-RecoveryResult 'normal-disabled'
$taskArchiveAfter = @(Get-ChildItem -LiteralPath $taskFolder -Filter '*.np3workspace' | Sort-Object Name | ForEach-Object Name)
if (($taskArchiveBefore -join '|') -ne ($taskArchiveAfter -join '|')) { throw 'Disabled reopening changed normal-session archives' }
if ([Convert]::ToBase64String([IO.File]::ReadAllBytes($taskCorrupt)) -ne 'AQIDBA==') { throw 'Invalid recovery source was changed' }
Write-Output "Live isolation, protected bytes, crash recovery, normal discard and corrupt-source preservation verified. Evidence: $taskFolder"

$taskMainEvidence = $taskFolder
$taskFolder = Join-Path $taskMainEvidence 'split'
New-Item -ItemType Directory -Path $taskFolder | Out-Null
[IO.File]::WriteAllText((Join-Path $taskFolder 'recovery-test.marker'), 'isolated split session regression')
$taskRows = (0..79 | ForEach-Object { '    [1,2]' + $(if ($_ -lt 79) { ',' } else { '' }) }) -join "`r`n"
$taskJson = "{`r`n  ""left"": [`r`n$taskRows`r`n  ],`r`n  ""right"": [`r`n$taskRows`r`n  ]`r`n}`r`n"
[IO.File]::WriteAllText((Join-Path $taskFolder 'split-long.json'), $taskJson, [Text.UTF8Encoding]::new($false))
foreach ($taskMode in @('split-seed', 'split-verify', 'split-import')) {
    $taskSplit = Start-RecoveryTest $taskMode
    if (!$taskSplit.WaitForExit(30000)) { throw "Split session test still running: PID $($taskSplit.Id), mode $taskMode" }
    Read-RecoveryResult $taskMode
}
if ([IO.File]::ReadAllText((Join-Path $taskFolder 'split-long.json')) -ne $taskJson) { throw 'Split session import changed original disk bytes' }
Write-Output "Long document split scrolling, opposite folds, selections, zoom and manual import verified across processes. Evidence: $taskFolder"

$taskInitFailure = Start-RecoveryTest 'init-failure'
if (!$taskInitFailure.WaitForExit(30000)) { throw "Initialization failure test still running: PID $($taskInitFailure.Id)" }
Read-RecoveryResult 'init-failure'
if ([IO.File]::ReadAllText((Join-Path $taskFolder 'blocked-directory')) -ne 'directory fixture') { throw 'Initialization failure changed blocking file bytes' }
if ([IO.File]::ReadAllText((Join-Path $taskFolder 'split-long.json')) -ne $taskJson) { throw 'Initialization failure test changed original disk bytes' }
Write-Output "Initialization failure reporting, draft preservation, manual backup and successful retry verified. Evidence: $taskFolder"

$taskHistory = Start-RecoveryTest 'history'
if (!$taskHistory.WaitForExit(30000)) { throw "History cleanup test still running: PID $($taskHistory.Id)" }
Read-RecoveryResult 'history'
$taskHistoryRoot = Join-Path $taskFolder 'history'
$taskHistoryExpected = @(7..16 + @(19,20) | ForEach-Object { 'np3-400000-{0:x16}.np3workspace' -f $_ } | Sort-Object)
$taskHistoryActual = @(Get-ChildItem -LiteralPath $taskHistoryRoot -File -Filter 'np3-400000-*.np3workspace' | Select-Object -ExpandProperty Name | Sort-Object)
if (($taskHistoryActual -join '|') -ne ($taskHistoryExpected -join '|')) { throw 'History surviving file set differs from newest ten plus preserved non-normal records' }
foreach ($taskIndex in 7..16) {
    $taskArchive = Join-Path $taskHistoryRoot ('np3-400000-{0:x16}.np3workspace' -f $taskIndex)
    $taskBefore = Join-Path $taskHistoryRoot ("valid-$taskIndex-before.np3session")
    if ([Convert]::ToBase64String([IO.File]::ReadAllBytes($taskArchive)) -ne [Convert]::ToBase64String([IO.File]::ReadAllBytes($taskBefore))) { throw "Retained archive bytes changed: $taskIndex" }
}
foreach ($taskPair in @(@('np3-400000-0000000000000013.np3workspace','corrupt-before.bin'), @('np3-400000-0000000000000014.np3workspace','dirty-before.np3session'), @('np3-user-not-generated.np3workspace','manual-before.np3session'), @('np3-400000-0000000000000015.np3recovery','crash-before.np3session'))) {
    if ([Convert]::ToBase64String([IO.File]::ReadAllBytes((Join-Path $taskHistoryRoot $taskPair[0]))) -ne [Convert]::ToBase64String([IO.File]::ReadAllBytes((Join-Path $taskHistoryRoot $taskPair[1])))) { throw "Protected history file bytes changed: $($taskPair[0])" }
}
Write-Output "History surviving names and retained/protected bytes independently verified. Evidence: $taskHistoryRoot"
