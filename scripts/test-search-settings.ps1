param([string]$Executable='Notepad3/Bin/Release_x64_v143/Notepad3.exe')
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$exe=[IO.Path]::GetFullPath((Join-Path $root $Executable))
$fixture=Join-Path $root ('artifacts/search-settings-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $fixture | Out-Null
[IO.File]::WriteAllText((Join-Path $fixture 'workspace-test.marker'),'isolated search settings',[Text.UTF8Encoding]::new($false))
[IO.File]::WriteAllText((Join-Path $fixture 'fixture.md'),'fixture',[Text.UTF8Encoding]::new($false))
function Run-Stage($stage,$ini){
    $oldFolder=$env:NP3_WORKSPACE_SELFTEST_DIR;$oldStage=$env:NP3_SEARCH_SETTINGS_TEST
    try{
        $env:NP3_WORKSPACE_SELFTEST_DIR=$fixture;$env:NP3_SEARCH_SETTINGS_TEST=$stage
        $process=Start-Process -FilePath $exe -ArgumentList @('/n','/f',('"'+$ini+'"')) -WindowStyle Hidden -PassThru
    }finally{
        if($null -eq $oldFolder){Remove-Item Env:NP3_WORKSPACE_SELFTEST_DIR -ErrorAction SilentlyContinue}else{$env:NP3_WORKSPACE_SELFTEST_DIR=$oldFolder}
        if($null -eq $oldStage){Remove-Item Env:NP3_SEARCH_SETTINGS_TEST -ErrorAction SilentlyContinue}else{$env:NP3_SEARCH_SETTINGS_TEST=$oldStage}
    }
    if(!$process.WaitForExit(60000)){throw "Search settings stage still running, PID $($process.Id). Inspect $fixture before retry."}
    $log=[IO.File]::ReadAllText((Join-Path $fixture 'workspace-test.log'))
    [IO.File]::WriteAllText((Join-Path $fixture ($stage+'-'+[IO.Path]::GetFileName($ini)+'.log')),$log)
    if($process.ExitCode -ne 0 -or $log -notmatch 'RESULT checks=\d+ failures=0'){throw $log}
    Write-Output $log
}
function New-Ini($name,$save,$flags){
    $ini=Join-Path $fixture ($name+'.ini')
    [IO.File]::WriteAllText($ini,"[Settings]`r`nSaveSettings=$save`r`n[Settings2]`r`nReopenWorkspace=false`r`nSearchFlags=$flags`r`n",[Text.UTF8Encoding]::new($true))
    return $ini
}
$ini=New-Ini 'persist' 'true' 1
Run-Stage 'write' $ini
$text=[IO.File]::ReadAllText($ini)
if($text -notmatch '(?m)^SearchTypes=\*\.md;\*\.txt\s*$' -or $text -notmatch '(?m)^SearchExcluded=缓存;\.git\s*$' -or $text -notmatch '(?m)^SearchFlags=14\s*$'){throw 'Independent persisted INI readback mismatch'}
Run-Stage 'read' $ini
$ini=New-Ini 'disabled' 'false' 1
Run-Stage 'write' $ini
$text=[IO.File]::ReadAllText($ini)
if($text -match '(?m)^SearchTypes=' -or $text -notmatch '(?m)^SearchFlags=1\s*$'){throw 'Disabled automatic settings persistence changed disk'}
Run-Stage 'defaults' $ini
$ini=New-Ini 'clamped' 'false' 99
Run-Stage 'high' $ini
Write-Output "Independent persistence, disabled save and flags bounds verified: $fixture"
