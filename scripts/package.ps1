$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path $PSScriptRoot -Parent
$taskOutput = Join-Path $taskRoot 'Notepad3\Bin\Release_x64_v143'
$taskPackage = Join-Path $taskRoot 'artifacts\markdown-x64'
if (-not (Test-Path "$taskOutput\Notepad3.exe")) { throw 'Build x64 Release first.' }
New-Item -ItemType Directory -Force $taskPackage, "$taskPackage\licenses", "$taskPackage\lng" | Out-Null
Get-ChildItem -LiteralPath $taskOutput -File | Where-Object { $_.Extension -in @('.exe', '.dll', '.manifest') } | Copy-Item -Destination $taskPackage
if (Test-Path "$taskOutput\lng") {
    Get-ChildItem -LiteralPath "$taskOutput\lng" -Directory | Where-Object { $_.Name -ne 'obj' } | Copy-Item -Destination "$taskPackage\lng" -Recurse -Force
}
Copy-Item -LiteralPath "$taskRoot\Notepad3\Build\Notepad3.ini" -Destination "$taskPackage\Notepad3.ini"
Copy-Item -LiteralPath "$taskRoot\Notepad3\Build\Docs\License.txt" -Destination "$taskPackage\licenses\Notepad3.txt"
Copy-Item -LiteralPath "$taskRoot\Notepad3\src\Markdown\vendor\md4c\LICENSE.md" -Destination "$taskPackage\licenses\MD4C.md"
Copy-Item -LiteralPath "$taskRoot\Notepad3\src\Markdown\vendor\webview2\LICENSE.txt" -Destination "$taskPackage\licenses\WebView2.txt"
Copy-Item -LiteralPath "$taskRoot\Notepad3\src\Markdown\vendor\webview2\NOTICE.txt" -Destination "$taskPackage\licenses\WebView2-NOTICE.txt"
Copy-Item -LiteralPath "$taskRoot\docs\迭代进展.md" -Destination "$taskPackage\迭代进展.md"
Write-Output "Packaged development build: $taskPackage\Notepad3.exe"
