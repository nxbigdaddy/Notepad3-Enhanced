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
    _WorkspaceTestCheck(&result, seeded || verified || live || clean || failure, "recognized recovery test mode");
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
