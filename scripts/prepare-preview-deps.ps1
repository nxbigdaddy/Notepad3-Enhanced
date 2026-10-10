$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path $PSScriptRoot -Parent
$taskVendor = Join-Path $taskRoot 'Notepad3\src\Markdown\vendor'
New-Item -ItemType Directory -Force "$taskVendor\md4c", "$taskVendor\webview2" | Out-Null
$taskMd = Join-Path $taskRoot '.cache\md4c\md4c-release-0.5.2'
foreach ($taskName in @('md4c.c','md4c.h','md4c-html.c','md4c-html.h','entity.c','entity.h')) {
    Copy-Item -LiteralPath "$taskMd\src\$taskName" -Destination "$taskVendor\md4c\$taskName"
}
Copy-Item -LiteralPath "$taskMd\LICENSE.md" -Destination "$taskVendor\md4c\LICENSE.md"
$taskWeb = Join-Path $taskRoot '.cache\webview2'
Copy-Item -LiteralPath "$taskWeb\build\native\include\WebView2.h" -Destination "$taskVendor\webview2\WebView2.h"
foreach ($taskArch in @('x64','x86','arm64')) {
    New-Item -ItemType Directory -Force "$taskVendor\webview2\$taskArch" | Out-Null
    Copy-Item -LiteralPath "$taskWeb\build\native\$taskArch\WebView2Loader.dll" -Destination "$taskVendor\webview2\$taskArch\WebView2Loader.dll"
}
Get-ChildItem -LiteralPath $taskWeb -File | Where-Object { $_.Name -match 'license|notice' } | Copy-Item -Destination "$taskVendor\webview2"
