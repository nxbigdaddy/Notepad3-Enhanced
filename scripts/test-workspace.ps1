param([string]$Executable = 'Notepad3/Bin/Release_x64_v143/Notepad3.exe', [string]$AiEndpoint = '', [switch]$Preview, [ValidateSet('auto','dark','light')][string]$ThemeMode)
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
$taskPreviousAi = $env:NP3_AI_SELFTEST_URL
$taskPreviousPreview = $env:NP3_MARKDOWN_CHECK_PREVIEW
$taskPreviousDiagnostics = $env:NP3_MARKDOWN_DIAGNOSTICS
$taskArguments = @('/n', '/f0')
if ($AiEndpoint -or $ThemeMode) {
    $taskIni = Join-Path $taskFolder 'test.ini'
    $taskTheme = switch ($ThemeMode) { 'dark' { 1 } 'light' { 2 } default { 0 } }
    [IO.File]::WriteAllText($taskIni, "[Settings]`r`nWinThemeDarkMode=$taskTheme`r`n[Settings2]`r`nReopenWorkspace=false`r`n", [Text.UTF8Encoding]::new($true))
    $taskArguments = @('/n', '/f', ('"' + $taskIni + '"'))
}
try {
    $env:NP3_WORKSPACE_SELFTEST_DIR = $taskFolder
    if ($Preview) { $env:NP3_MARKDOWN_CHECK_PREVIEW = '1'; $env:NP3_MARKDOWN_DIAGNOSTICS = Join-Path $taskFolder 'preview-trace.log' } else { Remove-Item Env:NP3_MARKDOWN_CHECK_PREVIEW -ErrorAction SilentlyContinue }
    if ($AiEndpoint) { $env:NP3_AI_SELFTEST_URL = $AiEndpoint } else { Remove-Item Env:NP3_AI_SELFTEST_URL -ErrorAction SilentlyContinue }
    $taskProcess = Start-Process -FilePath $taskExe -ArgumentList $taskArguments -WindowStyle Hidden -PassThru
} finally {
    if ($null -eq $taskPreviousPreview) { Remove-Item Env:NP3_MARKDOWN_CHECK_PREVIEW -ErrorAction SilentlyContinue } else { $env:NP3_MARKDOWN_CHECK_PREVIEW = $taskPreviousPreview }
    if ($null -eq $taskPreviousDiagnostics) { Remove-Item Env:NP3_MARKDOWN_DIAGNOSTICS -ErrorAction SilentlyContinue } else { $env:NP3_MARKDOWN_DIAGNOSTICS = $taskPreviousDiagnostics }
    if ($null -eq $taskPreviousAi) { Remove-Item Env:NP3_AI_SELFTEST_URL -ErrorAction SilentlyContinue } else { $env:NP3_AI_SELFTEST_URL = $taskPreviousAi }
    if ($null -eq $taskPreviousEnvironment) { Remove-Item Env:NP3_WORKSPACE_SELFTEST_DIR -ErrorAction SilentlyContinue }
    else { $env:NP3_WORKSPACE_SELFTEST_DIR = $taskPreviousEnvironment }
}
if (!$taskProcess.WaitForExit(60000)) { throw "Native test still running, PID $($taskProcess.Id). Inspect $taskFolder before retrying." }
$taskLog = Join-Path $taskFolder 'workspace-test.log'
if (!(Test-Path -LiteralPath $taskLog)) { throw "Native test produced no log. Inspect $taskFolder" }
$taskResult = [IO.File]::ReadAllText($taskLog)
Write-Output $taskResult
if ($taskResult -notmatch 'RESULT checks=\d+ failures=0') { throw "Native workspace checks failed: $taskLog" }
$taskExpectedA = [byte[]](0xEF, 0xBB, 0xBF) + [Text.Encoding]::UTF8.GetBytes("alpha`r`nA")
$taskExpectedB = [byte[]](0xFF, 0xFE) + [Text.Encoding]::Unicode.GetBytes("beta`r`nB")
foreach ($taskPair in @(@('a.txt', $taskExpectedA), @('b.txt', $taskExpectedB), @('close-flow.txt', [Text.Encoding]::UTF8.GetBytes("close fixture`r`nsaved")))) {
    $taskActual = [IO.File]::ReadAllBytes((Join-Path $taskFolder $taskPair[0]))
    if ([Convert]::ToBase64String($taskActual) -ne [Convert]::ToBase64String($taskPair[1])) { throw "Saved bytes differ for $($taskPair[0]): $taskFolder" }
}
$taskCrypt = Join-Path (Split-Path $taskExe -Parent) 'np3encrypt.exe'
foreach ($taskEncryption in @(@('encrypted-a.bin','np3-fixture-a',"alpha`r`nA"), @('encrypted-b.bin','np3-fixture-b',"beta`r`nB"), @('encrypted-a-again.bin','np3-fixture-a',"alpha`r`nA"), @('encrypted-after-session.bin','np3-fixture-a',"alpha`r`nA"))) {
    $taskEncrypted = Join-Path $taskFolder $taskEncryption[0]
    $taskDecrypted = $taskEncrypted + '.plain'
    & $taskCrypt df $taskEncrypted $taskDecrypted $taskEncryption[1] | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "Independent decryption failed: $taskEncrypted" }
    if ([Convert]::ToBase64String([IO.File]::ReadAllBytes($taskDecrypted)) -ne [Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($taskEncryption[2]))) { throw "Per-tab encryption key or source bytes differ: $taskEncrypted" }
}
Write-Output 'A B A and post-session encryption keys and snapshot bytes independently decrypted and verified.'
Write-Output "Saved UTF-8 BOM / UTF-16 LE and close-dialog UTF-8 bytes independently verified. Evidence: $taskFolder"
$taskExportedHtml = [IO.File]::ReadAllText((Join-Path $taskFolder 'buffer-export.html'), [Text.Encoding]::UTF8)
if (!$taskExportedHtml.Contains('<h1>导出验证</h1>') -or $taskExportedHtml.Contains('np3-preview.invalid')) { throw 'Exported unsaved buffer HTML content incorrect' }
Write-Output 'Unsaved buffer Markdown HTML independently verified.'

if ($Preview) {
    $taskTrace = [IO.File]::ReadAllText((Join-Path $taskFolder 'preview-trace.log'))
    $taskStates = @($taskTrace -split "`r?`n" | Where-Object { $_.StartsWith('{') } | ForEach-Object { ($_ -replace ' HRESULT=.*$','') | ConvertFrom-Json })
    if (!($taskStates | Where-Object { $_.done -eq $true -and $_.diagrams -eq 1 -and $_.formulas -eq 2 -and $_.errors -eq 0 })) { throw "Native advanced renderer counts incorrect: $taskFolder" }
    Write-Output 'Native WebView2 diagram and formula counts independently verified.'
}
