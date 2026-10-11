param([string]$Executable = 'Notepad3/Bin/Release_x64_v143/Notepad3.exe', [switch]$Advanced, [switch]$Long, [switch]$Tall)
$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path $PSScriptRoot -Parent
$taskExe = [IO.Path]::GetFullPath((Join-Path $taskRoot $Executable))
if (!(Test-Path -LiteralPath $taskExe)) { throw "Missing executable: $taskExe" }
$taskFolder = Join-Path $taskRoot ('artifacts/pdf-test-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $taskFolder | Out-Null
[IO.File]::WriteAllText((Join-Path $taskFolder 'pdf-test.marker'), 'isolated native PDF export regression')
[IO.File]::WriteAllText((Join-Path $taskFolder 'source.md'), "# Markdown PDF 导出验证`r`n`r`n这是中文正文，English text.`r`n`r`n| 项目 | 数量 |`r`n|---|---|`r`n| 示例 | 42 |`r`n`r`n``````cpp`r`nif (a < b) { return 42; }`r`n```````r`n`r`n![本地图片](sample.png)`r`n", [Text.UTF8Encoding]::new($false))
if ($Advanced) {
    $taskExtra = '

## 离线图表与公式

```mermaid
flowchart LR
  A[图表起点] --> B[图表终点]
```

公式标记：$E=mc^2$。

$$
\frac{1}{2} + \sum_{i=1}^{n} i
$$
'
    [IO.File]::AppendAllText((Join-Path $taskFolder 'source.md'), $taskExtra, [Text.UTF8Encoding]::new($false))
}
if ($Tall) {
    $taskDiagram = "`r`n`r`n## 超高图表`r`n`r`n``````mermaid`r`nflowchart TD`r`n"
    for ($taskNode = 1; $taskNode -le 30; ++$taskNode) {
        $taskDiagram += ('  N{0}[TALLNODE{0:D2}]' -f $taskNode) + "`r`n"
        if ($taskNode -gt 1) { $taskDiagram += ('  N{0} --> N{1}' -f ($taskNode - 1), $taskNode) + "`r`n" }
    }
    $taskDiagram += "```````r`n`r`nTALL-DIAGRAM-END`r`n"
    [IO.File]::AppendAllText((Join-Path $taskFolder 'source.md'), $taskDiagram, [Text.UTF8Encoding]::new($false))
}
if ($Long) {
    for ($taskParagraph = 1; $taskParagraph -le 60; ++$taskParagraph) {
        [IO.File]::AppendAllText((Join-Path $taskFolder 'source.md'), "`r`n`r`n### 分页段落 $taskParagraph`r`n`r`n这是用于核对分页的正文。每个段落都应完整保留，最后一页包含分页末尾标记。This paragraph must remain complete across page boundaries.`r`n", [Text.UTF8Encoding]::new($false))
    }
    [IO.File]::AppendAllText((Join-Path $taskFolder 'source.md'), "`r`n`r`n分页末尾标记 END-OF-LONG-DOCUMENT`r`n", [Text.UTF8Encoding]::new($false))
}
# A known PNG fixture is kept next to the Markdown source.
[IO.File]::WriteAllBytes((Join-Path $taskFolder 'sample.png'), [Convert]::FromBase64String('iVBORw0KGgoAAAANSUhEUgAAAGAAAABACAIAAABqVuVZAAABU0lEQVR4nO2bwU3DQBBFnSjNDC3QEucUwZmWaCFTBUfOIOUItr+IPX8W+b1j7Gg1b/9mV/bk9PH5NcEy55VrgCANCRIgSIAgAYIECBJcli48X2/TkXh/fZr9nAQJECRAkABBAgQ9uos5ybf5HSRebocWlAteft/QaOoypprZ+1s0nce3s/2L/yZBublCf5R8Ccr95t8ZJZOg3LskmyPOQQMIyprZ9oSoXFBWlmFwxBJrFZT1M1w9BAkSIKhPULqOKqUDkSABggQIEiBIgCABggQI6hMUrud+pQORIAGCWgVF/SqrHoIEdQuKyhk2JNSRoKgpw7NLssTGEBR7z7btkOVLUOxXkvPVs/XdfNwL2/IA0N/g0fAbFI8W2dL+0tMfFH+M0uEaqH6UTQueYIRexCU4BwkQJECQAEECBAlO/G9+HRIkQJAAQQIECRAkQJAAQdM631MXUr7ET6ogAAAAAElFTkSuQmCC'))
$taskPrevious = $env:NP3_PDF_SELFTEST_DIR
try {
    $env:NP3_PDF_SELFTEST_DIR = $taskFolder
    $taskProcess = Start-Process -FilePath $taskExe -ArgumentList @('/n', '/f0') -WindowStyle Hidden -PassThru
} finally {
    if ($null -eq $taskPrevious) { Remove-Item Env:NP3_PDF_SELFTEST_DIR -ErrorAction SilentlyContinue } else { $env:NP3_PDF_SELFTEST_DIR = $taskPrevious }
}
if (!$taskProcess.WaitForExit(90000)) { throw "PDF test still running: PID $($taskProcess.Id), evidence $taskFolder" }
$taskLog = Join-Path $taskFolder 'pdf-test.log'
if (!(Test-Path -LiteralPath $taskLog)) { throw "Missing native PDF test log: $taskFolder" }
$taskResult = [IO.File]::ReadAllText($taskLog)
Write-Output $taskResult
if ($taskResult -notmatch 'RESULT checks=\d+ failures=0') { throw "Native PDF tests failed: $taskFolder" }
$taskOutput = Join-Path $taskFolder 'export.pdf'
$taskBaseline = Join-Path $taskFolder 'baseline.pdf'
if ((Get-FileHash -LiteralPath $taskOutput).Hash -ne (Get-FileHash -LiteralPath $taskBaseline).Hash) { throw 'Failed replacement changed earlier PDF bytes' }
Write-Output "PDF baseline SHA-256 retained after replacement failure. Evidence: $taskFolder"
