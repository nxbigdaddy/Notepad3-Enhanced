typedef struct PDF_TEST {
    WORKSPACE_TEST_RESULT result;
    HPATHL directory, target;
    HANDLE locked;
    int phase;
    sptr_t document;
    unsigned long long version;
} PDF_TEST;
static bool _MarkdownPdfTestCopy(const HPATHL source, const HPATHL destination)
{
    HANDLE input = Path_CreateFile(source, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    HANDLE output = Path_CreateFile(destination, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    bool success = IS_VALID_HANDLE(input) && IS_VALID_HANDLE(output);
    char buffer[4096]; DWORD read = 0, written = 0;
    while (success) {
        if (!ReadFile(input, buffer, sizeof(buffer), &read, NULL)) { success = false; break; }
        if (!read) break;
        success = WriteFile(output, buffer, read, &written, NULL) && written == read;
    }
    if (IS_VALID_HANDLE(input)) CloseHandle(input);
    if (IS_VALID_HANDLE(output)) { if (!FlushFileBuffers(output)) success = false; CloseHandle(output); }
    return success;
}
static void _MarkdownPdfTestFinish(PDF_TEST* test)
{
    char summary[128]; DWORD written;
    StringCchPrintfA(summary, COUNTOF(summary), "RESULT checks=%d failures=%d\r\n", test->result.checks, test->result.failures);
    WriteFile(test->result.log, summary, (DWORD)strlen(summary), &written, NULL);
    CloseHandle(test->result.log);
    if (IS_VALID_HANDLE(test->locked)) CloseHandle(test->locked);
    Path_Release(test->directory); Path_Release(test->target);
    _Workspace()->pdfTest = NULL; FreeMem(test);
    _WorkspaceDispose(); DestroyWindow(Globals.hwndMain);
}
static bool _MarkdownPdfSelfTestDone(uint32_t error)
{
    DOCUMENT_WORKSPACE* workspace = _Workspace();
    PDF_TEST* test = workspace ? workspace->pdfTest : NULL;
    if (!test) return false;
    if (test->phase == 0) {
        _WorkspaceTestCheck(&test->result, !error && Path_IsExistingFile(test->target), "actual WebView2 PDF export completed");
        _WorkspaceTestCheck(&test->result, SciCall_GetDocPointer() == test->document && workspace->documents[workspace->active].version > test->version && IsSaveNeeded() && SciCall_CanUndo(), "asynchronous PDF export leaves later editing intact");
        SciCall_Undo();
        _WorkspaceTestCheck(&test->result, !IsSaveNeeded(), "later edit remains independently undoable");
        if (error) { _MarkdownPdfTestFinish(test); return true; }
        HPATHL baseline = Path_Copy(test->directory); Path_Append(baseline, L"baseline.pdf");
        _WorkspaceTestCheck(&test->result, _MarkdownPdfTestCopy(test->target, baseline), "retain successful PDF baseline"); Path_Release(baseline);
        test->locked = Path_CreateFile(test->target, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        _WorkspaceTestCheck(&test->result, IS_VALID_HANDLE(test->locked), "lock existing PDF against replacement");
        test->phase = 1;
        uint32_t startError = 0;
        bool const started = _MarkdownExportSnapshot(test->target, &startError, true);
        _WorkspaceTestCheck(&test->result, started, "start PDF replacement failure test");
        if (!started) _MarkdownPdfTestFinish(test);
    } else {
        _WorkspaceTestCheck(&test->result, error != 0 && Path_IsExistingFile(test->target), "locked PDF replacement reports failure and retains output");
        _MarkdownPdfTestFinish(test);
    }
    return true;
}
static void _MarkdownPdfSelfTest()
{
    WCHAR folder[MAX_PATH_EXPLICIT];
    DWORD length = GetEnvironmentVariableW(L"NP3_PDF_SELFTEST_DIR", folder, COUNTOF(folder));
    if (!length || length >= COUNTOF(folder)) return;
    HPATHL marker = Path_Allocate(folder); Path_Append(marker, L"pdf-test.marker");
    bool const allowed = Path_IsExistingFile(marker); Path_Release(marker);
    if (!allowed || !_WorkspaceEnsure()) return;
    PDF_TEST* test = AllocMem(sizeof(*test), HEAP_ZERO_MEMORY);
    if (!test) return;
    test->directory = Path_Allocate(folder); test->target = Path_Allocate(folder); Path_Append(test->target, L"export.pdf");
    HPATHL log = Path_Allocate(folder); Path_Append(log, L"pdf-test.log");
    test->result.log = Path_CreateFile(log, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL); Path_Release(log);
    if (!IS_VALID_HANDLE(test->result.log)) { Path_Release(test->directory); Path_Release(test->target); FreeMem(test); return; }
    _Workspace()->pdfTest = test;
    _WorkspaceTestCheck(&test->result, _Workspace()->count == 1 && !SciCall_GetLength() && !IsSaveNeeded(), "isolated PDF test startup");
    HPATHL source = Path_Allocate(folder); Path_Append(source, L"source.md");
    _WorkspaceTestCheck(&test->result, FileLoad(source, FLF_DontSave, 0, 0), "load Markdown PDF fixture");
    uint32_t error = 0;
    _WorkspaceTestCheck(&test->result, !_MarkdownExportSnapshot(source, &error, true) && error == ERROR_SHARING_VIOLATION, "PDF export cannot overwrite source document");
    Path_Release(source);
    test->document = SciCall_GetDocPointer(); test->version = _Workspace()->documents[_Workspace()->active].version;
    bool const started = _MarkdownExportSnapshot(test->target, &error, true);
    _WorkspaceTestCheck(&test->result, started && MarkdownPdf_IsBusy(Globals.hwndMain), "start independent PDF snapshot job");
    if (!started) { _MarkdownPdfTestFinish(test); return; }
    _WorkspaceTestCheck(&test->result, !_MarkdownExportSnapshot(test->target, &error, true) && error == ERROR_BUSY, "overlapping PDF export rejected");
    const char* later = "\r\nAFTER SNAPSHOT SHOULD NOT APPEAR";
    SciCall_AppendText(strlen(later), later);
}
