param(
    [ValidateSet('Release', 'Debug')][string]$Configuration = 'Release',
    [string]$Toolset = 'v143',
    [switch]$SkipVersion
)
$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path $PSScriptRoot -Parent
$taskSource = Join-Path $taskRoot 'Notepad3'
$taskVswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$taskVs = & $taskVswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $taskVs) { throw 'Visual Studio C++ toolchain is not installed yet.' }
$taskMSBuild = Join-Path $taskVs 'MSBuild\Current\Bin\MSBuild.exe'
if (-not (Test-Path (Join-Path $taskSource 'Notepad3.sln'))) { throw 'Notepad3 source is missing.' }
$taskArtifacts = Join-Path $taskRoot 'artifacts'
New-Item -ItemType Directory -Force $taskArtifacts | Out-Null
Push-Location $taskSource
try {
    if (-not $SkipVersion) {
        & pwsh -NoProfile -ExecutionPolicy Bypass -File .\Version.ps1
        if ($LASTEXITCODE -ne 0) { throw 'Version generation failed.' }
    }
    # Language post-build steps share lng/np3lng.dll; serialize projects to avoid races.
    & $taskMSBuild Notepad3.sln /m:1 /restore "/p:Configuration=$Configuration" /p:Platform=x64 "/p:PlatformToolset=$Toolset" /v:quiet /clp:ErrorsOnly "/flp:logfile=$taskArtifacts\build-$Configuration.log;verbosity=normal"
    if ($LASTEXITCODE -ne 0) { throw "Build failed. See artifacts/build-$Configuration.log." }
} finally { Pop-Location }
