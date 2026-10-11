param([string]$Package='artifacts/Notepad3-extension-patch-x64-v82')
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$packageRoot=Join-Path $root $Package
$evidence=Join-Path $root ('artifacts/patch-test-'+[Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $evidence | Out-Null
$checks=New-Object 'System.Collections.Generic.List[string]'
function Assert([bool]$Condition,[string]$Message) { if(!$Condition){throw $Message}; $checks.Add($Message); Write-Output "PASS $Message" }
function Hash([string]$Path) { (Get-FileHash -LiteralPath $Path).Hash }
function Fixture([string]$Name) {
    $folder=Join-Path $evidence $Name; New-Item -ItemType Directory -Path (Join-Path $folder 'lng/zh-CN') -Force | Out-Null
    Copy-Item -LiteralPath (Join-Path $root 'artifacts/baseline/Notepad3.exe') -Destination $folder
    [IO.File]::WriteAllBytes((Join-Path $folder 'Notepad3.ini'),[byte[]](0xFF,0xFE,0x41,0x00))
    [IO.File]::WriteAllText((Join-Path $folder 'user.txt'),'private fixture retained')
    [IO.File]::WriteAllText((Join-Path $folder 'WebView2Loader.dll'),'old loader fixture')
    return $folder
}
function Run-Patch([string]$Target,[bool]$Restore=$false,[bool]$ExpectedSuccess=$true) {
    $arguments=@('-NoProfile','-ExecutionPolicy','Bypass','-File',(Join-Path $packageRoot 'Apply-Patch.ps1'),'-TargetDirectory',$Target,'-NonInteractive')
    if($Restore){$arguments+='-Restore'}
    $log=@(& powershell.exe @arguments 2>&1)
    $exitCode=$LASTEXITCODE
    $log | Out-File -LiteralPath (Join-Path $evidence ('run-'+[Guid]::NewGuid().ToString('N')+'.log')) -Encoding utf8
    Assert (($exitCode -eq 0) -eq $ExpectedSuccess) "Expected patch process success=$ExpectedSuccess (exit $exitCode)"
}
$target=Fixture 'normal installation with spaces'
$originalExe=Hash (Join-Path $target 'Notepad3.exe'); $originalIni=Hash (Join-Path $target 'Notepad3.ini'); $originalLoader=Hash (Join-Path $target 'WebView2Loader.dll'); $userHash=Hash (Join-Path $target 'user.txt')
Run-Patch $target
$manifest=Get-Content -LiteralPath (Join-Path $packageRoot 'manifest.json') -Raw | ConvertFrom-Json
$allMatch=$true
foreach($file in $manifest.files){if((Hash (Join-Path $target $file.path)) -ne $file.sha256){$allMatch=$false}}
Assert $allMatch 'Every installed payload file matches its manifest'
Assert ((Hash (Join-Path $target 'Notepad3.ini')) -eq $originalIni -and (Hash (Join-Path $target 'user.txt')) -eq $userHash) 'User settings and document bytes unchanged by installation'
[IO.File]::WriteAllText((Join-Path $target 'WebView2Loader.dll'),'later user update')
Run-Patch $target $true $false
Assert (([IO.File]::ReadAllText((Join-Path $target 'WebView2Loader.dll'))) -eq 'later user update' -and (Hash (Join-Path $target 'Notepad3.exe')) -eq (Hash (Join-Path $packageRoot 'payload/Notepad3.exe'))) 'Restore conflict rejected before overwriting any installed files'
Copy-Item -LiteralPath (Join-Path $packageRoot 'payload/WebView2Loader.dll') -Destination (Join-Path $target 'WebView2Loader.dll') -Force
$heldRestore=[IO.File]::Open((Join-Path $target 'WebView2Loader.dll'),[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::Read)
try { Run-Patch $target $true $false } finally { $heldRestore.Dispose() }
$restoreRollbackMatch=$true
foreach($file in $manifest.files){if((Hash (Join-Path $target $file.path)) -ne $file.sha256){$restoreRollbackMatch=$false}}
Assert $restoreRollbackMatch 'Restore write failure rolls back to complete patched installation'
Run-Patch $target $true
Assert ((Hash (Join-Path $target 'Notepad3.exe')) -eq $originalExe -and (Hash (Join-Path $target 'WebView2Loader.dll')) -eq $originalLoader) 'Original executable and original loader bytes restored'
Assert (!(Test-Path -LiteralPath (Join-Path $target 'render-assets/np3-render.js'))) 'Patch-added resource file removed on restore'
Assert ((Hash (Join-Path $target 'Notepad3.ini')) -eq $originalIni -and (Hash (Join-Path $target 'user.txt')) -eq $userHash) 'Restore retains user settings and document bytes'
$lockedTarget=Fixture 'locked file rollback'
$lockedOriginal=Hash (Join-Path $lockedTarget 'Notepad3.exe')
$heldFile=[IO.File]::Open((Join-Path $lockedTarget 'Notepad3.exe'),[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::Read)
try { Run-Patch $lockedTarget $false $false } finally { $heldFile.Dispose() }
Assert ((Hash (Join-Path $lockedTarget 'Notepad3.exe')) -eq $lockedOriginal -and !(Test-Path -LiteralPath (Join-Path $lockedTarget 'licenses/MD4C.md'))) 'Mid-deployment locked executable rolls back earlier copied files'
$rollbackState=Get-ChildItem -LiteralPath (Join-Path $lockedTarget '.np3-extension-backups') -Directory | ForEach-Object {Get-Content -LiteralPath (Join-Path $_.FullName 'state.json') -Raw | ConvertFrom-Json}
Assert ($rollbackState.status -eq 'rolled-back') 'Failed installation explicitly records successful rollback'
$lockTarget=Fixture 'concurrent patch lock'
$heldLock=[IO.File]::Open((Join-Path $lockTarget '.np3-extension-patch.lock'),[IO.FileMode]::OpenOrCreate,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None)
try { Run-Patch $lockTarget $false $false } finally { $heldLock.Dispose() }
Assert (!(Test-Path -LiteralPath (Join-Path $lockTarget '.np3-extension-backups'))) 'Concurrent patch lock rejects deployment before backup or replacement'
$badTarget=Fixture 'unsupported x86'
$bytes=New-Object byte[] 128; $bytes[0]=0x4d; $bytes[1]=0x5a; $bytes[60]=64; $bytes[64]=0x50; $bytes[65]=0x45; $bytes[68]=0x4c; $bytes[69]=1
[IO.File]::WriteAllBytes((Join-Path $badTarget 'Notepad3.exe'),$bytes)
Run-Patch $badTarget $false $false
Assert (!(Test-Path -LiteralPath (Join-Path $badTarget '.np3-extension-backups'))) 'Unsupported architecture rejected before creating backups or deploying'
$tamperPackage=Join-Path $evidence 'tamper package'; Copy-Item -LiteralPath $packageRoot -Destination $tamperPackage -Recurse
$packageRoot=$tamperPackage
[IO.File]::AppendAllText((Join-Path $packageRoot 'payload/WebView2Loader.dll'),'tamper')
$tamperTarget=Fixture 'damaged package target'; $tamperOriginal=Hash (Join-Path $tamperTarget 'Notepad3.exe')
Run-Patch $tamperTarget $false $false
Assert ((Hash (Join-Path $tamperTarget 'Notepad3.exe')) -eq $tamperOriginal -and !(Test-Path -LiteralPath (Join-Path $tamperTarget '.np3-extension-backups'))) 'Damaged payload rejected before any deployment or backup'
[pscustomobject]@{checks=$checks.Count;failures=0;evidence=$evidence;passed=@($checks)} | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $evidence 'results.json') -Encoding utf8
Write-Output "RESULT checks=$($checks.Count) failures=0 Evidence: $evidence"
