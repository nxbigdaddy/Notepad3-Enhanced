param([string]$Executable='Notepad3/Bin/Release_x64_v143/Notepad3.exe')
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$exe=[IO.Path]::GetFullPath((Join-Path $root $Executable))
$fixture=Join-Path $root ('artifacts/history-hooks-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $fixture | Out-Null
[IO.File]::WriteAllText((Join-Path $fixture 'workspace-test.marker'),'isolated history regression')
$document=Join-Path $fixture '中文.md'
[IO.File]::WriteAllText($document,'history fixture',[Text.UTF8Encoding]::new($false))
[IO.File]::WriteAllText((Join-Path $fixture 'other.txt'),'owned history removal fixture',[Text.UTF8Encoding]::new($false))
foreach($stage in @('enabled','disabled','recent-off','low','high','clear')){
    $ini=Join-Path $fixture ($stage+'.ini')
    $enabled=if($stage -eq 'disabled'){'false'}else{'true'}
    $recent=if($stage -eq 'recent-off'){'false'}else{'true'}
    $maximum=if($stage -eq 'low'){0}elseif($stage -eq 'high'){10001}else{500}
    [IO.File]::WriteAllText($ini,"[Settings]`r`nSaveSettings=false`r`nSaveRecentFiles=$recent`r`n[Settings2]`r`nReopenWorkspace=false`r`nHistoryEnabled=$enabled`r`nHistoryMaximum=$maximum`r`n",[Text.UTF8Encoding]::new($true))
    $oldFolder=$env:NP3_WORKSPACE_SELFTEST_DIR;$oldStage=$env:NP3_HISTORY_TEST
    try{
        $env:NP3_WORKSPACE_SELFTEST_DIR=$fixture;$env:NP3_HISTORY_TEST=$stage
        $process=Start-Process -FilePath $exe -ArgumentList @('/n','/f',('"'+$ini+'"')) -WindowStyle Hidden -PassThru
    }finally{
        if($null -eq $oldFolder){Remove-Item Env:NP3_WORKSPACE_SELFTEST_DIR -ErrorAction SilentlyContinue}else{$env:NP3_WORKSPACE_SELFTEST_DIR=$oldFolder}
        if($null -eq $oldStage){Remove-Item Env:NP3_HISTORY_TEST -ErrorAction SilentlyContinue}else{$env:NP3_HISTORY_TEST=$oldStage}
    }
    if(!$process.WaitForExit(60000)){throw "History test still running, PID $($process.Id); inspect $fixture"}
    $log=[IO.File]::ReadAllText((Join-Path $fixture 'workspace-test.log'))
    [IO.File]::WriteAllText((Join-Path $fixture ($stage+'.log')),$log)
    if($process.ExitCode -ne 0 -or $log -notmatch 'RESULT checks=\d+ failures=0'){throw $log}
    if($stage -in @('disabled','recent-off')){
        if(Test-Path -LiteralPath ($ini+'.history')){throw 'Disabled recording created history'}
    }elseif($stage -eq 'clear'){
        & (Join-Path $root 'artifacts/history_service_test.exe') --empty ($ini+'.history')
        if($LASTEXITCODE){throw 'Independent cleared history readback failed'}
    }else{
        & (Join-Path $root 'artifacts/history_service_test.exe') --read ($ini+'.history') $document 2
        if($LASTEXITCODE){throw 'Independent process history readback failed'}
        if($stage -eq 'enabled'){
            & (Join-Path $root 'artifacts/history_service_test.exe') --pinned ($ini+'.history') $document 1
            if($LASTEXITCODE){throw 'Independent persisted pin readback failed'}
        }
    }
    if([IO.File]::ReadAllText($document) -ne 'history fixture'){throw 'History changed source document'}
    if([IO.File]::ReadAllText((Join-Path $fixture 'other.txt')) -ne 'owned history removal fixture'){throw 'Removing history changed source file'}
    Write-Output "$stage $($log.Trim().Split("`n")[-1])"
}
Write-Output "Independent readback and source preservation passed: $fixture"
