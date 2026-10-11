param([string]$PatchZip='artifacts/Notepad3-extension-patch-x64-v82-final.zip',[string]$OutputName='Notepad3-扩展更新包-x64-v82.exe')
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$archive=Join-Path $root $PatchZip
$output=Join-Path (Join-Path $root 'artifacts') $OutputName
if([IO.Path]::GetFileName($OutputName) -ne $OutputName){throw 'OutputName must be a filename.'}
if(Test-Path -LiteralPath $output){throw 'Output already exists.'}
$compiler=Join-Path $env:WINDIR 'Microsoft.NET/Framework64/v4.0.30319/csc.exe'
& $compiler /nologo /target:winexe /platform:x64 /optimize+ /utf8output "/out:$output" "/resource:$archive,Payload.zip" "/win32manifest:$(Join-Path $PSScriptRoot 'patch/UpdateLauncher.manifest')" /reference:System.Windows.Forms.dll /reference:System.Drawing.dll /reference:System.IO.Compression.dll /reference:System.IO.Compression.FileSystem.dll (Join-Path $PSScriptRoot 'patch/UpdateLauncher.cs')
if($LASTEXITCODE){throw 'Update launcher compilation failed.'}
Write-Output "Single-file updater: $output"
