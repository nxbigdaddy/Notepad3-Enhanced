param([string]$Executable = 'artifacts/file-tree-pages-x64-v19/Notepad3.exe')
$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path $PSScriptRoot -Parent
$taskExe = [IO.Path]::GetFullPath((Join-Path $taskRoot $Executable))
if (!(Test-Path -LiteralPath $taskExe)) { throw "Missing test executable: $taskExe" }
$taskFolder = Join-Path $taskRoot ('artifacts/workspace-test-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $taskFolder | Out-Null
[IO.File]::WriteAllText((Join-Path $taskFolder 'workspace-test.marker'), 'isolated native workspace regression', [Text.UTF8Encoding]::new($false))
New-Item -ItemType Directory -Path (Join-Path $taskFolder 'nested') | Out-Null
[IO.File]::WriteAllText((Join-Path $taskFolder 'nested/child.txt'), 'nested fixture', [Text.UTF8Encoding]::new($false))
for ($taskIndex = 0; $taskIndex -lt 12; ++$taskIndex) {
    $taskDirectory = Join-Path $taskFolder ('queued-{0:00}' -f $taskIndex)
    New-Item -ItemType Directory -Path $taskDirectory | Out-Null
    [IO.File]::WriteAllText((Join-Path $taskDirectory 'child.txt'), 'queued fixture', [Text.UTF8Encoding]::new($false))
}
[IO.File]::WriteAllText((Join-Path $taskFolder 'a.txt'), "alpha`r`n", [Text.UTF8Encoding]::new($false))
[IO.File]::WriteAllText((Join-Path $taskFolder 'b.txt'), "beta`r`n", [Text.UnicodeEncoding]::new($false, $true))
[IO.File]::WriteAllText((Join-Path $taskFolder 'c.txt'), "gamma`r`n", [Text.UTF8Encoding]::new($false))
[IO.File]::WriteAllText((Join-Path $taskFolder 'fold.json'), "{`r`n  `"nested`": {`r`n    `"value`": 1`r`n  }`r`n}`r`n", [Text.UTF8Encoding]::new($false))
$taskLargeDirectory = Join-Path $taskFolder 'large'
New-Item -ItemType Directory -Path $taskLargeDirectory | Out-Null
for ($taskIndex = 0; $taskIndex -lt 5012; ++$taskIndex) {
    [IO.File]::WriteAllText((Join-Path $taskLargeDirectory ('item-{0:00000}.txt' -f $taskIndex)), '', [Text.UTF8Encoding]::new($false))
}
$taskPreviousEnvironment = $env:NP3_WORKSPACE_SELFTEST_DIR
try {
    $env:NP3_WORKSPACE_SELFTEST_DIR = $taskFolder
    $taskProcess = Start-Process -FilePath $taskExe -ArgumentList @('/n', '/f0') -WindowStyle Hidden -PassThru
} finally {
    if ($null -eq $taskPreviousEnvironment) { Remove-Item Env:NP3_WORKSPACE_SELFTEST_DIR -ErrorAction SilentlyContinue }
    else { $env:NP3_WORKSPACE_SELFTEST_DIR = $taskPreviousEnvironment }
}
if (!$taskProcess.WaitForExit(30000)) { throw "Native test still running, PID $($taskProcess.Id). Inspect $taskFolder before retrying." }
$taskLog = Join-Path $taskFolder 'workspace-test.log'
if (!(Test-Path -LiteralPath $taskLog)) { throw "Native test produced no log. Inspect $taskFolder" }
$taskResult = [IO.File]::ReadAllText($taskLog)
Write-Output $taskResult
if ($taskResult -notmatch 'RESULT checks=\d+ failures=0') { throw "Native workspace checks failed: $taskLog" }
$taskExpectedA = [byte[]](0xEF, 0xBB, 0xBF) + [Text.Encoding]::UTF8.GetBytes("alpha`r`nA")
$taskExpectedB = [byte[]](0xFF, 0xFE) + [Text.Encoding]::Unicode.GetBytes("beta`r`nB")
foreach ($taskPair in @(@('a.txt', $taskExpectedA), @('b.txt', $taskExpectedB))) {
    $taskActual = [IO.File]::ReadAllBytes((Join-Path $taskFolder $taskPair[0]))
    if ([Convert]::ToBase64String($taskActual) -ne [Convert]::ToBase64String($taskPair[1])) { throw "Saved bytes differ for $($taskPair[0]): $taskFolder" }
}
Write-Output "Saved UTF-8 BOM / UTF-16 LE bytes independently verified. Evidence: $taskFolder"
$taskExportedHtml = [IO.File]::ReadAllText((Join-Path $taskFolder 'buffer-export.html'), [Text.Encoding]::UTF8)
if (!$taskExportedHtml.Contains('<h1>导出验证</h1>') -or $taskExportedHtml.Contains('np3-preview.invalid')) { throw 'Exported unsaved buffer HTML content incorrect' }
Write-Output 'Unsaved buffer Markdown HTML independently verified.'
