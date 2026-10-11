param(
    [string]$TargetDirectory,
    [switch]$Restore,
    [string]$BackupDirectory,
    [switch]$NonInteractive
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Safe-Path([string]$Root, [string]$Relative) {
    if ([string]::IsNullOrWhiteSpace($Relative) -or [IO.Path]::IsPathRooted($Relative) -or $Relative.Contains(':')) { throw "Invalid relative path: $Relative" }
    $rootPath = [IO.Path]::GetFullPath($Root).TrimEnd('\')
    $path = [IO.Path]::GetFullPath((Join-Path $rootPath $Relative))
    if (!$path.StartsWith($rootPath + '\', [StringComparison]::OrdinalIgnoreCase)) { throw "Path escapes directory: $Relative" }
    $part = $path
    while ($part) {
        if (Test-Path -LiteralPath $part) {
            if ((Get-Item -LiteralPath $part -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Links/junctions are not supported: $part" }
        }
        $parent = [IO.Path]::GetDirectoryName($part)
        if ($parent -eq $part) { break }
        $part = $parent
    }
    return $path
}
function File-Hash([string]$Path) { return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash }
function Check-Idle([string]$Executable) {
    foreach ($process in @(Get-Process -Name Notepad3 -ErrorAction SilentlyContinue)) {
        try { $processPath = $process.Path } catch { throw 'Cannot inspect a running Notepad3. Close all Notepad3 windows first.' }
        if (!$processPath) { throw 'Cannot inspect a running Notepad3. Close all Notepad3 windows first.' }
        if ([string]::Equals($processPath, $Executable, [StringComparison]::OrdinalIgnoreCase)) { throw 'Please save your files and close this Notepad3 before applying/restoring the patch.' }
    }
}
function Check-X64([string]$Executable) {
    $stream = [IO.File]::OpenRead($Executable)
    $reader = New-Object IO.BinaryReader($stream)
    try {
        if ($stream.Length -lt 64 -or $reader.ReadUInt16() -ne 0x5A4D) { throw 'Not a Windows executable.' }
        $stream.Position = 0x3C; $offset = $reader.ReadInt32()
        if ($offset -lt 64 -or $offset -gt $stream.Length - 6) { throw 'Invalid executable header.' }
        $stream.Position = $offset
        if ($reader.ReadUInt32() -ne 0x00004550 -or $reader.ReadUInt16() -ne 0x8664) { throw 'This patch supports x64 Notepad3 only. A 32-bit or ARM executable cannot be replaced.' }
    } finally { $reader.Dispose(); $stream.Dispose() }
}
function Select-Target {
    $candidates = @()
    foreach ($base in @($env:ProgramFiles, ${env:ProgramFiles(x86)}, $PSScriptRoot)) {
        if ($base) { $candidates += $base; $candidates += (Join-Path $base 'Notepad3') }
    }
    foreach ($key in @('HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\Notepad3_is1', 'HKCU:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\Notepad3_is1')) {
        $entry = Get-ItemProperty -LiteralPath $key -ErrorAction SilentlyContinue
        if ($entry -and $entry.PSObject.Properties['InstallLocation'] -and $entry.InstallLocation) { $candidates += $entry.InstallLocation }
    }
    $found = @($candidates | Where-Object { Test-Path -LiteralPath (Join-Path $_ 'Notepad3.exe') -PathType Leaf } | ForEach-Object { [IO.Path]::GetFullPath($_).TrimEnd('\') } | Select-Object -Unique)
    if ($NonInteractive) { throw 'Specify -TargetDirectory when using -NonInteractive.' }
    Add-Type -AssemblyName System.Windows.Forms
    $dialog = New-Object Windows.Forms.FolderBrowserDialog
    $dialog.Description = 'Select the existing Notepad3 folder (containing Notepad3.exe). / 选择原 Notepad3 安装目录'
    $dialog.ShowNewFolderButton = $false
    if ($found.Count) { $dialog.SelectedPath = $found[0] }
    try {
        if ($dialog.ShowDialog() -ne [Windows.Forms.DialogResult]::OK) { throw 'Canceled; no files changed.' }
        return $dialog.SelectedPath
    } finally { $dialog.Dispose() }
}
function Restore-Files($Records, [string]$Target, [string]$Backup) {
    foreach ($record in $Records) {
        $destination = Safe-Path $Target $record.path
        if ($record.existed) {
            if ((Test-Path -LiteralPath $destination -PathType Leaf) -and (File-Hash $destination) -eq $record.originalHash) { continue }
            $original = Safe-Path (Join-Path $Backup 'original') $record.path
            New-Item -ItemType Directory -Force -Path ([IO.Path]::GetDirectoryName($destination)) | Out-Null
            Copy-Item -LiteralPath $original -Destination $destination -Force
            if ((File-Hash $destination) -ne $record.originalHash) { throw "Restore verification failed: $($record.path)" }
        } elseif (Test-Path -LiteralPath $destination -PathType Leaf) {
            Remove-Item -LiteralPath $destination -Force
        }
    }
}

if (!$TargetDirectory) { $TargetDirectory = Select-Target }
$TargetDirectory = [IO.Path]::GetFullPath($TargetDirectory).TrimEnd('\')
$targetExe = Safe-Path $TargetDirectory 'Notepad3.exe'
if (!(Test-Path -LiteralPath $targetExe -PathType Leaf)) { throw 'The chosen directory does not contain Notepad3.exe.' }
Check-Idle $targetExe
Check-X64 $targetExe
$lockPath = Safe-Path $TargetDirectory '.np3-extension-patch.lock'
try { $patchLock = [IO.File]::Open($lockPath, [IO.FileMode]::OpenOrCreate, [IO.FileAccess]::ReadWrite, [IO.FileShare]::None) }
catch { throw 'Cannot acquire patch lock. Another patch may be running, or this directory needs administrator access.' }
try {
$backupRoot = Safe-Path $TargetDirectory '.np3-extension-backups'

if ($Restore) {
    if (!$BackupDirectory) {
        $available = @(Get-ChildItem -LiteralPath $backupRoot -Directory -ErrorAction SilentlyContinue | Where-Object { Test-Path -LiteralPath (Join-Path $_.FullName 'state.json') } | Sort-Object Name -Descending)
        foreach ($candidate in $available) {
            $candidateState = Get-Content -LiteralPath (Join-Path $candidate.FullName 'state.json') -Raw | ConvertFrom-Json
            if ($candidateState.status -eq 'applied') { $BackupDirectory = $candidate.FullName; break }
        }
    }
    if (!$BackupDirectory) { throw 'No applied patch backup was found.' }
    $BackupDirectory = [IO.Path]::GetFullPath($BackupDirectory)
    if (!$BackupDirectory.StartsWith($backupRoot + '\', [StringComparison]::OrdinalIgnoreCase)) { throw 'Backup must belong to this Notepad3 directory.' }
    $stateFile = Safe-Path $BackupDirectory 'state.json'
    $state = Get-Content -LiteralPath $stateFile -Raw | ConvertFrom-Json
    if ($state.status -ne 'applied' -or ![string]::Equals($state.target,$TargetDirectory,[StringComparison]::OrdinalIgnoreCase)) { throw 'Backup is not an applied patch for this directory.' }
    foreach ($record in $state.files) {
        $destination = Safe-Path $TargetDirectory $record.path
        if (!(Test-Path -LiteralPath $destination -PathType Leaf) -or (File-Hash $destination) -ne $record.patchedHash) { throw "File changed since patching; restore stopped without overwriting it: $($record.path)" }
        if ($record.existed -and (File-Hash (Safe-Path (Join-Path $BackupDirectory 'original') $record.path)) -ne $record.originalHash) { throw "Backup verification failed: $($record.path)" }
    }
    Check-Idle $targetExe
    $restoreStage = Safe-Path $BackupDirectory ('restore-stage-' + [Guid]::NewGuid().ToString('N'))
    $installedRecords = @()
    foreach ($record in $state.files) {
        $saved = Safe-Path (Join-Path $restoreStage 'original') $record.path
        New-Item -ItemType Directory -Force -Path ([IO.Path]::GetDirectoryName($saved)) | Out-Null
        Copy-Item -LiteralPath (Safe-Path $TargetDirectory $record.path) -Destination $saved
        if ((File-Hash $saved) -ne $record.patchedHash) { throw "Restore staging verification failed: $($record.path)" }
        $installedRecords += [pscustomobject]@{path=$record.path;existed=$true;originalHash=$record.patchedHash}
    }
    try { Restore-Files $state.files $TargetDirectory $BackupDirectory }
    catch {
        $restoreFailure = $_
        try { Restore-Files $installedRecords $TargetDirectory $restoreStage }
        catch { Write-Warning "Restore rollback failed; retain both backup and restore stage: $restoreStage. $($_.Exception.Message)" }
        throw $restoreFailure
    }
    $state.status = 'restored'; $state | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $stateFile -Encoding UTF8
    Write-Output "Original version restored. Your INI settings were retained. Backup: $BackupDirectory"
    return
}

$manifest = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'manifest.json') -Raw | ConvertFrom-Json
if ($manifest.architecture -ne 'x64' -or @($manifest.files).Count -eq 0) { throw 'Invalid patch manifest.' }
$seen = @{}
foreach ($file in $manifest.files) {
    if ($seen.ContainsKey($file.path)) { throw 'Duplicate payload path.' }; $seen[$file.path] = $true
    if ($file.path -match '(?i)(^|[\\/])[^\\/]*\.ini$' -or $file.path -match '^(?i)\.np3-extension-backups[\\/]') { throw 'Patch payload may not contain settings or backups.' }
    $source = Safe-Path (Join-Path $PSScriptRoot 'payload') $file.path
    $null = Safe-Path $TargetDirectory $file.path
    if (!(Test-Path -LiteralPath $source -PathType Leaf) -or (File-Hash $source) -ne $file.sha256) { throw "Patch verification failed: $($file.path)" }
}
if (!$seen.ContainsKey('Notepad3.exe')) { throw 'Patch executable missing.' }
Check-X64 (Join-Path $PSScriptRoot 'payload\Notepad3.exe')
$backupName = (Get-Date -Format 'yyyyMMdd-HHmmss-fffffff') + '-' + [Guid]::NewGuid().ToString('N')
$backup = Safe-Path $backupRoot $backupName
New-Item -ItemType Directory -Path $backup -Force | Out-Null
$records = @()
foreach ($file in $manifest.files) {
    $destination = Safe-Path $TargetDirectory $file.path
    if (Test-Path -LiteralPath $destination -PathType Container) { throw "File path is a directory: $($file.path)" }
    $existed = Test-Path -LiteralPath $destination -PathType Leaf
    $originalHash = $null
    if ($existed) {
        $original = Safe-Path (Join-Path $backup 'original') $file.path
        New-Item -ItemType Directory -Force -Path ([IO.Path]::GetDirectoryName($original)) | Out-Null
        Copy-Item -LiteralPath $destination -Destination $original
        $originalHash = File-Hash $destination
        if ((File-Hash $original) -ne $originalHash) { throw "Backup verification failed: $($file.path)" }
    }
    $records += [pscustomobject]@{path=$file.path;existed=$existed;originalHash=$originalHash;patchedHash=$file.sha256}
}
$state = [pscustomobject]@{target=$TargetDirectory;status='prepared';package=$manifest.version;files=$records}
$stateFile = Join-Path $backup 'state.json'
$state | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $stateFile -Encoding UTF8
try {
    Check-Idle $targetExe
    foreach ($file in $manifest.files) {
        $destination = Safe-Path $TargetDirectory $file.path
        New-Item -ItemType Directory -Force -Path ([IO.Path]::GetDirectoryName($destination)) | Out-Null
        Copy-Item -LiteralPath (Safe-Path (Join-Path $PSScriptRoot 'payload') $file.path) -Destination $destination -Force
        if ((File-Hash $destination) -ne $file.sha256) { throw "Installed file verification failed: $($file.path)" }
    }
    $state.status = 'applied'; $state | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $stateFile -Encoding UTF8
} catch {
    $failure = $_
    try { Restore-Files $records $TargetDirectory $backup; $state.status = 'rolled-back' }
    catch { $state.status = 'rollback-failed'; Write-Warning "Automatic restore failed. Preserve this backup: $backup. $($_.Exception.Message)" }
    $state | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $stateFile -Encoding UTF8
    throw $failure
}
Write-Output "Patch applied. Start Notepad3 using your original shortcut. INI settings retained. Backup: $backup"
Write-Output 'Markdown preview needs Microsoft Edge WebView2 Runtime. AI needs your own configured service.'
} finally { $patchLock.Dispose() }
