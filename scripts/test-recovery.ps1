param([string]$Executable = 'artifacts/workspace-recovery-x64-v10/Notepad3.exe')
$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path $PSScriptRoot -Parent
$taskExe = [IO.Path]::GetFullPath((Join-Path $taskRoot $Executable))
if (!(Test-Path -LiteralPath $taskExe)) { throw "Missing executable: $taskExe" }
$taskFolder = Join-Path $taskRoot ('artifacts/recovery-test-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $taskFolder | Out-Null
[IO.File]::WriteAllText((Join-Path $taskFolder 'recovery-test.marker'), 'isolated process recovery regression')
$taskCorrupt = Join-Path $taskFolder 'np3-corrupt.np3recovery'
[IO.File]::WriteAllBytes($taskCorrupt, [byte[]](1, 2, 3, 4))
function Start-RecoveryTest([string]$Mode) {
    $taskPreviousRoot = $env:NP3_RECOVERY_TEST_DIR
    $taskPreviousMode = $env:NP3_RECOVERY_TEST_MODE
    try {
        $env:NP3_RECOVERY_TEST_DIR = $taskFolder
        $env:NP3_RECOVERY_TEST_MODE = $Mode
        Start-Process -FilePath $taskExe -ArgumentList @('/n', '/f0') -WindowStyle Hidden -PassThru
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
if ([Convert]::ToBase64String([IO.File]::ReadAllBytes($taskCorrupt)) -ne 'AQIDBA==') { throw 'Invalid recovery source was changed' }
Write-Output "Live isolation, protected bytes, crash recovery, normal discard and corrupt-source preservation verified. Evidence: $taskFolder"
