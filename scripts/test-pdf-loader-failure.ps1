$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path $PSScriptRoot -Parent
$taskBuild = Join-Path $taskRoot 'Notepad3/Bin/Release_x64_v143'
$taskFolder = Join-Path $taskRoot ('artifacts/pdf-failure-' + [Guid]::NewGuid().ToString('N'))
$taskBin = Join-Path $taskFolder 'bin'
New-Item -ItemType Directory -Path $taskFolder, $taskBin | Out-Null
Get-ChildItem -LiteralPath $taskBuild -File | Where-Object {
    $_.Extension -in @('.exe', '.dll', '.manifest') -and $_.Name -ne 'WebView2Loader.dll'
} | Copy-Item -Destination $taskBin
Copy-Item -LiteralPath (Join-Path $taskBuild 'lng') -Destination $taskBin -Recurse
[IO.File]::WriteAllText((Join-Path $taskFolder 'pdf-test.marker'), 'isolated native failure regression')
[IO.File]::WriteAllText((Join-Path $taskFolder 'source.md'), "# 失败保护`r`n中文 source stays intact.", [Text.UTF8Encoding]::new($false))
$taskOutput = Join-Path $taskFolder 'export.pdf'
[IO.File]::WriteAllText($taskOutput, '%PDF- prior output must remain byte-identical', [Text.UTF8Encoding]::new($false))
$taskSourceHash = (Get-FileHash -LiteralPath (Join-Path $taskFolder 'source.md')).Hash
$taskOutputHash = (Get-FileHash -LiteralPath $taskOutput).Hash
$taskVars = @('NP3_PDF_SELFTEST_DIR', 'NP3_PDF_SELFTEST_FAILURE', 'NP3_PDF_SELFTEST_CLOSE')
$taskPrevious = @{}
foreach ($taskVar in $taskVars) { $taskPrevious[$taskVar] = [Environment]::GetEnvironmentVariable($taskVar, 'Process') }
$taskStartedUtc = [DateTime]::UtcNow
try {
    $env:NP3_PDF_SELFTEST_DIR = $taskFolder
    $env:NP3_PDF_SELFTEST_FAILURE = '0000007e'
    Remove-Item Env:NP3_PDF_SELFTEST_CLOSE -ErrorAction SilentlyContinue
    $taskProcess = Start-Process -FilePath (Join-Path $taskBin 'Notepad3.exe') -ArgumentList @('/n', '/f0') -WindowStyle Hidden -PassThru
} finally {
    foreach ($taskVar in $taskVars) { [Environment]::SetEnvironmentVariable($taskVar, $taskPrevious[$taskVar], 'Process') }
}
if (!$taskProcess.WaitForExit(60000)) { throw "Failure test still running: PID $($taskProcess.Id), evidence $taskFolder" }
if ($taskProcess.ExitCode -ne 0) { throw "Failure test exit code $($taskProcess.ExitCode): $taskFolder" }
$taskResult = [IO.File]::ReadAllText((Join-Path $taskFolder 'pdf-test.log'))
Write-Output $taskResult
if ($taskResult -notmatch 'RESULT checks=\d+ failures=0') { throw "Native failure tests failed: $taskFolder" }
if ((Get-FileHash -LiteralPath $taskOutput).Hash -ne $taskOutputHash) { throw 'Initialization failure changed existing output' }
if ((Get-FileHash -LiteralPath (Join-Path $taskFolder 'source.md')).Hash -ne $taskSourceHash) { throw 'Initialization failure changed source bytes' }
$taskStaging = @(Get-ChildItem -LiteralPath ([IO.Path]::GetTempPath()) -Directory -Filter ('np3-pdf-' + $taskProcess.Id + '-*') | Where-Object { $_.CreationTimeUtc -ge $taskStartedUtc })
$taskTemporary = @(Get-ChildItem -LiteralPath $taskFolder -File -Filter ('*.tmp-pdf-' + $taskProcess.Id + '-*'))
if ($taskStaging.Count -or $taskTemporary.Count) { throw "Failure left owned temporary artifacts: $taskFolder" }
Write-Output "Source/output hashes retained; owned temporary artifacts absent. Mode=missing-loader Evidence=$taskFolder"
