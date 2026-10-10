// Only entered with an explicit mode and a marker-validated isolated directory.
static void _WorkspaceRecoveryTest()
{
    WCHAR mode[32];
    DWORD const length = GetEnvironmentVariableW(L"NP3_RECOVERY_TEST_MODE", mode, COUNTOF(mode));
    if (!length || length >= COUNTOF(mode)) return;
    DOCUMENT_WORKSPACE* workspace = _Workspace();
    if (!workspace || !workspace->recovery) return;
    WORKSPACE_RECOVERY* recovery = workspace->recovery;
    // A mode alone must never operate on the normal user's recovery directory.
    if (!GetEnvironmentVariableW(L"NP3_RECOVERY_TEST_DIR", NULL, 0)) return;
    HPATHL logPath = Path_Copy(recovery->directory); Path_Append(logPath, mode);
    Path_RenameExtension(logPath, L".log");
    WORKSPACE_TEST_RESULT result = { 0 };
    result.log = Path_CreateFile(logPath, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    Path_Release(logPath);
    if (!IS_VALID_HANDLE(result.log)) return;
    bool const seeded = wcscmp(mode, L"seed") == 0;
    bool const verified = wcscmp(mode, L"verify") == 0;
    bool const live = wcscmp(mode, L"live") == 0;
    bool const clean = wcscmp(mode, L"clean") == 0;
    bool const failure = wcscmp(mode, L"failure") == 0;
    bool const normalSeed = wcscmp(mode, L"normal-seed") == 0;
    bool const normalVerify = wcscmp(mode, L"normal-verify") == 0;
    bool const normalExplicit = wcscmp(mode, L"normal-explicit") == 0;
    bool const normalMissing = wcscmp(mode, L"normal-missing") == 0;
    bool const normalDisabled = wcscmp(mode, L"normal-disabled") == 0;
    _WorkspaceTestCheck(&result, seeded || verified || live || clean || failure || normalSeed || normalVerify || normalExplicit || normalMissing || normalDisabled, "recognized recovery test mode");
    if (seeded) {
        _WorkspaceTestCheck(&result, workspace->count == 1 && SciCall_GetLength() == 0, "seed starts isolated");
        SciCall_AppendText(15, "unsaved alpha\r\n"); Encoding_Current(CPI_UTF8SIGN);
        HPATHL empty = Path_Allocate(NULL);
        _WorkspaceTestCheck(&result, FileLoad(empty, FLF_New | FLF_DontSave, 0, 0), "second unsaved document");
        Path_Release(empty);
        SciCall_AppendText(14, "unsaved beta\r\n"); Encoding_Current(CPI_UNICODE);
        _WorkspaceTestCheck(&result, _WorkspaceRecoveryHarvest(recovery, 10000), "initial background snapshot completed");
        _WorkspaceRecoveryTimer(NULL, 0, 0, 0);
        _WorkspaceTestCheck(&result, recovery->job && _WorkspaceRecoveryHarvest(recovery, 10000) && Path_IsExistingFile(recovery->file), "two-document background snapshot persisted");
    } else if (verified) {
        _WorkspaceTestCheck(&result, workspace->count == 2, "crash restored both unsaved documents");
        _WorkspaceTestCheck(&result, _WorkspaceSwitch(0) && _WorkspaceTestText("unsaved alpha\r\n"), "alpha content restored");
        _WorkspaceTestCheck(&result, Encoding_GetCurrent() == CPI_UTF8SIGN && IsSaveNeeded(), "alpha encoding and unsaved status restored");
        _WorkspaceTestCheck(&result, _WorkspaceSwitch(1) && _WorkspaceTestText("unsaved beta\r\n"), "beta content restored");
        _WorkspaceTestCheck(&result, Encoding_GetCurrent() == CPI_UNICODE && IsSaveNeeded(), "beta encoding and unsaved status restored");
        _WorkspaceTestCheck(&result, _WorkspaceRecoveryHarvest(recovery, 10000) && Path_IsExistingFile(recovery->file), "recovery transferred to current owner");
    } else if (normalSeed) {
        HPATHL a = Path_Copy(recovery->directory); Path_Append(a, L"normal-a.txt");
        HPATHL b = Path_Copy(recovery->directory); Path_Append(b, L"normal-b.txt");
        _WorkspaceTestCheck(&result, FileLoad(a, FLF_DontSave, 0, 0), "open normal-session A");
        Encoding_Current(CPI_UTF8SIGN); SciCall_AppendText(5, "SAVED");
        _WorkspaceTestCheck(&result, FileSave(FSF_None), "save normal-session A change");
        SciCall_SetZoom(3); SciCall_SetSel(1, 4); _WorkspaceCapture();
        _WorkspaceTestCheck(&result, FileLoad(b, FLF_DontSave, 0, 0), "open normal-session B");
        SciCall_AppendText(7, "DISCARD");
        HPATHL empty = Path_Allocate(NULL);
        _WorkspaceTestCheck(&result, FileLoad(empty, FLF_New | FLF_DontSave, 0, 0), "new discarded untitled document");
        SciCall_AppendText(17, "UNTITLED DISCARD!"); Path_Release(empty);
        _WorkspaceSwitch(0);
        uint32_t error = 0; NP3_SESSION normal = NULL;
        _WorkspaceTestCheck(&result, _WorkspaceNormalSave(recovery->normalPath, &error) && Session_Load(Path_Get(recovery->normalPath), &normal, &error), "normal metadata persisted");
        _WorkspaceTestCheck(&result, Session_Count(normal) == 2 && Session_Active(normal) == 0, "normal archive excludes discarded untitled tab");
        NP3_SESSION_DOCUMENT entry = { 0 }; Session_Get(normal, 1, &entry);
        _WorkspaceTestCheck(&result, entry.textLength == 0 && (entry.flags & NP3_SESSION_DISK_BACKED), "discarded named buffer text absent from archive");
        _WorkspaceTestCheck(&result, !_WorkspaceSessionLoad(recovery->normalPath, &error) && workspace->count == 3 && _WorkspaceTestText("alpha\r\nSAVED"), "metadata archive cannot import as empty recovered buffers");
        Session_Free(normal); Path_Release(a); Path_Release(b);
    } else if (normalDisabled) {
        _WorkspaceTestCheck(&result, !Settings2.ReopenWorkspace, "reopen setting loaded from isolated INI");
        _WorkspaceTestCheck(&result, workspace->count == 1 && SciCall_GetLength() == 0, "disabled reopening leaves empty workspace despite earlier archive");
        _WorkspaceTestCheck(&result, workspace->recovery != NULL, "disabled normal reopening retains crash backup service");
    } else if (normalMissing) {
        _WorkspaceTestCheck(&result, workspace->count == 2 && recovery->errorReports > 0, "missing normal-session file reported while available files reopen");
        _WorkspaceTestCheck(&result, _WorkspaceSwitch(1) && _WorkspaceTestText("x") && !IsSaveNeeded(), "externally shortened disk file loaded without stale buffer text");
        _WorkspaceTestCheck(&result, SciCall_GetCurrentPos() <= 1 && SciCall_GetAnchor() <= 1 && SciCall_GetZoom() == 3, "selection clamped to shortened document");
    } else if (normalVerify || normalExplicit) {
        int const offset = normalExplicit ? 1 : 0;
        _WorkspaceTestCheck(&result, workspace->count == 2 + offset, "normal session tab count restored");
        if (normalExplicit) _WorkspaceTestCheck(&result, workspace->active == 0 && _WorkspaceTestText("incoming\r\n"), "explicit incoming document stays active");
        else _WorkspaceTestCheck(&result, workspace->active == 0, "normal active tab restored");
        _WorkspaceTestCheck(&result, _WorkspaceSwitch(offset) && _WorkspaceTestText("alpha\r\nSAVED") && !IsSaveNeeded(), "saved A reopens from disk without dirty state");
        _WorkspaceTestCheck(&result, Encoding_GetCurrent() == CPI_UTF8SIGN && SciCall_GetZoom() == 3 && SciCall_GetAnchor() == 1 && SciCall_GetCurrentPos() == 4, "normal A encoding and view restored");
        _WorkspaceTestCheck(&result, _WorkspaceSwitch(offset + 1) && _WorkspaceTestText("beta\r\n") && !IsSaveNeeded(), "discarded B text not resurrected");
    } else if (failure) {
        _WorkspaceTestCheck(&result, recovery->errorReports > 0, "corrupt orphan recovery reported");
        _WorkspaceTestCheck(&result, _WorkspaceRecoveryHarvest(recovery, 10000), "initial backup completed");
        HANDLE locked = Path_CreateFile(recovery->file, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        _WorkspaceTestCheck(&result, IS_VALID_HANDLE(locked), "lock backup against replacement");
        unsigned int const before = recovery->errorReports;
        _WorkspaceRecoveryTimer(NULL, 0, 0, 0);
        _WorkspaceTestCheck(&result, _WorkspaceRecoveryHarvest(recovery, 10000) && recovery->lastError != 0 && recovery->errorReports == before + 1, "background write error reported once");
        _WorkspaceRecoveryTimer(NULL, 0, 0, 0);
        _WorkspaceTestCheck(&result, _WorkspaceRecoveryHarvest(recovery, 10000) && recovery->errorReports == before + 1, "repeated write error does not repeat warning");
        if (IS_VALID_HANDLE(locked)) CloseHandle(locked);
        NP3_SESSION prior = NULL; uint32_t error = 0;
        _WorkspaceTestCheck(&result, Session_Load(Path_Get(recovery->file), &prior, &error) != 0, "failed write retains valid earlier snapshot");
        Session_Free(prior);
        _WorkspaceRecoveryTimer(NULL, 0, 0, 0);
        _WorkspaceTestCheck(&result, _WorkspaceRecoveryHarvest(recovery, 10000) && recovery->lastError == 0, "successful retry clears failure state");
    } else if (live || clean) {
        _WorkspaceTestCheck(&result, workspace->count == 1 && SciCall_GetLength() == 0, live ? "live owner backup not imported" : "normal discard not resurrected");
    }
    char summary[128]; DWORD written;
    StringCchPrintfA(summary, COUNTOF(summary), "RESULT checks=%d failures=%d\r\n", result.checks, result.failures);
    WriteFile(result.log, summary, (DWORD)strlen(summary), &written, NULL); FlushFileBuffers(result.log); CloseHandle(result.log);
    if (seeded && !result.failures) {
        HPATHL ready = Path_Copy(recovery->directory); Path_Append(ready, L"seed.ready");
        HANDLE marker = Path_CreateFile(ready, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (IS_VALID_HANDLE(marker)) CloseHandle(marker);
        Path_Release(ready);
        return; // runner terminates only its own seed process to simulate a crash
    }
    _WorkspaceRecoveryClose();
    _WorkspaceDispose(); DestroyWindow(Globals.hwndMain);
}
