param([string]$SourcePackage='artifacts/markdown-divider-theme-x64-v82',[ValidatePattern('^[a-zA-Z0-9_-]+$')][string]$PackageName='Notepad3-extension-patch-x64-v82')
$ErrorActionPreference='Stop'
$taskRoot=Split-Path $PSScriptRoot -Parent
$source=[IO.Path]::GetFullPath((Join-Path $taskRoot $SourcePackage))
$output=Join-Path $taskRoot ('artifacts/'+$PackageName)
if(Test-Path -LiteralPath $output){throw 'Choose a new package name; existing output will not be overwritten.'}
New-Item -ItemType Directory -Path (Join-Path $output 'payload') -Force | Out-Null
foreach($name in @('Notepad3.exe','WebView2Loader.dll','lng','render-assets','licenses')) {
    Copy-Item -LiteralPath (Join-Path $source $name) -Destination (Join-Path $output 'payload') -Recurse
}
$script=[IO.File]::ReadAllText((Join-Path $PSScriptRoot 'patch/Apply-Patch.ps1'))
[IO.File]::WriteAllText((Join-Path $output 'Apply-Patch.ps1'),$script,[Text.UTF8Encoding]::new($true))
$payloadRoot=Join-Path $output 'payload'
$files=@(Get-ChildItem -LiteralPath $payloadRoot -Recurse -File | Sort-Object FullName | ForEach-Object {
    [pscustomobject]@{path=$_.FullName.Substring($payloadRoot.Length+1);sha256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash}
})
[pscustomobject]@{version=$PackageName;architecture='x64';files=$files} | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $output 'manifest.json') -Encoding UTF8
$launcher=@'
@echo off
setlocal
cd /d "%~dp0"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Apply-Patch.ps1" %*
set "patch_result=%errorlevel%"
if not "%patch_result%"=="0" echo Patch operation failed. For Program Files installations, run this file as administrator.
pause
exit /b %patch_result%
'@
[IO.File]::WriteAllText((Join-Path $output 'Install.cmd'),$launcher.Replace("`n","`r`n"),[Text.Encoding]::ASCII)
[IO.File]::WriteAllText((Join-Path $output 'Restore.cmd'),$launcher.Replace('Apply-Patch.ps1" %*','Apply-Patch.ps1" -Restore %*').Replace("`n","`r`n"),[Text.Encoding]::ASCII)
Copy-Item -LiteralPath (Join-Path $taskRoot 'docs/补丁升级.md') -Destination (Join-Path $output '使用说明.md')
Compress-Archive -LiteralPath $output -DestinationPath ($output+'.zip') -CompressionLevel Optimal
Write-Output "Patch ZIP: $output.zip"
