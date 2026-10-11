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
    if (_Workspace()) _Workspace()->pdfTest = NULL;
    FreeMem(test);
    if (IsWindow(Globals.hwndMain)) { _WorkspaceDispose(); DestroyWindow(Globals.hwndMain); }
}
static void _MarkdownPdfTestStartLocked(PDF_TEST* test)
{
    test->locked = Path_CreateFile(test->target, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    _WorkspaceTestCheck(&test->result, IS_VALID_HANDLE(test->locked), "lock existing PDF against replacement");
    test->phase = 1;
    uint32_t startError = 0;
    bool const started = _MarkdownExportSnapshot(test->target, &startError, true);
    _WorkspaceTestCheck(&test->result, started, "start PDF replacement failure test");
    if (!started) _MarkdownPdfTestFinish(test);
}
static void _MarkdownPdfSelfTestPrinting(HWND window)
{
    DOCUMENT_WORKSPACE* workspace=_Workspace(); PDF_TEST* test=workspace?workspace->pdfTest:NULL;
    if(test) { char note[100]; DWORD written; StringCchPrintfA(note,COUNTOF(note),"PRINT submission phase=%i window=%p\r\n",test->phase,window); WriteFile(test->result.log,note,(DWORD)strlen(note),&written,NULL); }
    if(test && test->phase==3) {
        _WorkspaceTestCheck(&test->result,window && MarkdownPdf_IsBusy(Globals.hwndMain),"main close observes actual submitted PDF print");
        SciCall_Undo();
        _WorkspaceTestCheck(&test->result,!IsSaveNeeded(),"main close fixture restores source savepoint before normal close");
        HWND parent=Globals.hwndMain;
        SendMessageW(parent,WM_CLOSE,0,0);
        _WorkspaceTestCheck(&test->result,!IsWindow(parent) && !_Workspace(),"normal main WM_CLOSE destroys window and disposes workspace during submitted print");
        _MarkdownPdfTestFinish(test); return;
    }
    if(!test || test->phase!=2) return;
    _WorkspaceTestCheck(&test->result,window && MarkdownPdf_IsBusy(Globals.hwndMain),"printing cancellation observes actual submitted PrintToPdf task");
    _WorkspaceTestCheck(&test->result,MarkdownPdf_Cancel(Globals.hwndMain),"printing cancellation destroys its active task window");
    _WorkspaceTestCheck(&test->result,!MarkdownPdf_IsBusy(Globals.hwndMain) && IsSaveNeeded() && SciCall_CanUndo(),"closing submitted print task preserves source undo and releases busy state");
    SciCall_Undo();
    _WorkspaceTestCheck(&test->result,!IsSaveNeeded() && SciCall_GetDocPointer()==test->document,"printing cancellation retains original source savepoint and document binding");
    _MarkdownPdfTestStartLocked(test);
}
static bool _MarkdownPdfSelfTestDone(uint32_t error)
{
    DOCUMENT_WORKSPACE* workspace = _Workspace();
    PDF_TEST* test = workspace ? workspace->pdfTest : NULL;
    if (!test) return false;
    if(test->phase<0) {
        char note[96]; DWORD written;
        StringCchPrintfA(note,COUNTOF(note),"EXPECTED FAILURE error=%08lx\r\n",(unsigned long)error);
        WriteFile(test->result.log,note,(DWORD)strlen(note),&written,NULL);
        WCHAR expectedText[32];
        GetEnvironmentVariableW(L"NP3_PDF_SELFTEST_FAILURE",expectedText,COUNTOF(expectedText));
        uint32_t expected=(uint32_t)wcstoul(expectedText,NULL,16);
        _WorkspaceTestCheck(&test->result,error==expected && error!=0,"missing runtime or loader reports expected real failure code");
        _WorkspaceTestCheck(&test->result,!MarkdownPdf_IsBusy(Globals.hwndMain) && Path_IsExistingFile(test->target),"failed initialization releases busy state and retains prior output");
        _WorkspaceTestCheck(&test->result,SciCall_GetDocPointer()==test->document && workspace->documents[workspace->active].version==test->version && !IsSaveNeeded(),"initialization failure preserves source document version and savepoint");
        if(test->phase==-1) {
            test->phase=-2; uint32_t startError=0;
            bool started=_MarkdownExportSnapshot(test->target,&startError,true);
            _WorkspaceTestCheck(&test->result,started,"initialization failure permits independent retry");
            if(!started) _MarkdownPdfTestFinish(test);
        } else _MarkdownPdfTestFinish(test);
        return true;
    }
    if (test->phase == 0) {
        _WorkspaceTestCheck(&test->result, !error && Path_IsExistingFile(test->target), "actual WebView2 PDF export completed");
        _WorkspaceTestCheck(&test->result, SciCall_GetDocPointer() == test->document && workspace->documents[workspace->active].version > test->version && IsSaveNeeded() && SciCall_CanUndo(), "asynchronous PDF export leaves later editing intact");
        SciCall_Undo();
        _WorkspaceTestCheck(&test->result, !IsSaveNeeded(), "later edit remains independently undoable");
        if (error) { _MarkdownPdfTestFinish(test); return true; }
        HPATHL baseline = Path_Copy(test->directory); Path_Append(baseline, L"baseline.pdf");
        _WorkspaceTestCheck(&test->result, _MarkdownPdfTestCopy(test->target, baseline), "retain successful PDF baseline"); Path_Release(baseline);
        const char* canceled="\r\nCANCELLED OUTPUT MUST NOT APPEAR";
        SciCall_AppendText(strlen(canceled),canceled);
        uint32_t cancelError=0;
        bool cancelStarted=_MarkdownExportSnapshot(test->target,&cancelError,true);
        HWND canceledJob=FindWindowExW(Globals.hwndMain,NULL,L"NP3MarkdownPdfJob",NULL);
        _WorkspaceTestCheck(&test->result,cancelStarted && canceledJob && MarkdownPdf_IsBusy(Globals.hwndMain),"cancellation fixture starts actual pending WebView PDF replacement");
        _WorkspaceTestCheck(&test->result,MarkdownPdf_Cancel(Globals.hwndMain),"initial cancellation successfully destroys its own active task window");
        _WorkspaceTestCheck(&test->result,!MarkdownPdf_IsBusy(Globals.hwndMain) && SciCall_GetDocPointer()==test->document && IsSaveNeeded() && SciCall_CanUndo(),"destroying export job releases busy state and preserves edited source undo");
        SciCall_Undo();
        _WorkspaceTestCheck(&test->result,!IsSaveNeeded() && Path_IsExistingFile(test->target),"canceled replacement leaves original export and source savepoint available");
        SciCall_AppendText(strlen(canceled),canceled); test->phase=2;
        uint32_t startError = 0;
        bool const started = _MarkdownExportSnapshot(test->target, &startError, true);
        _WorkspaceTestCheck(&test->result, started, "start replacement awaiting actual print submission for cancellation");
        if (!started) _MarkdownPdfTestFinish(test);
    } else if(test->phase==2 || test->phase==3) {
        char failure[120]; StringCchPrintfA(failure,COUNTOF(failure),"printing fixture must reach submission before completion error=%08lx",(unsigned long)error);
        _WorkspaceTestCheck(&test->result,false,failure); _MarkdownPdfTestFinish(test);
    } else {
        _WorkspaceTestCheck(&test->result, error != 0 && Path_IsExistingFile(test->target), "locked PDF replacement reports failure and retains output");
        WCHAR closeMode[8];
        if(GetEnvironmentVariableW(L"NP3_PDF_SELFTEST_CLOSE",closeMode,COUNTOF(closeMode))) {
            if(IS_VALID_HANDLE(test->locked)) { CloseHandle(test->locked); test->locked=NULL; }
            const char* canceled="\r\nCANCELLED OUTPUT MUST NOT APPEAR";
            SciCall_AppendText(strlen(canceled),canceled); test->phase=3;
            uint32_t startError=0;
            bool started=_MarkdownExportSnapshot(test->target,&startError,true);
            _WorkspaceTestCheck(&test->result,started,"start replacement for main-window close during print");
            if(!started) _MarkdownPdfTestFinish(test);
        } else _MarkdownPdfTestFinish(test);
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
    if(GetEnvironmentVariableW(L"NP3_PDF_SELFTEST_FAILURE",NULL,0)) {
        test->phase=-1;
        bool started=_MarkdownExportSnapshot(test->target,&error,true);
        _WorkspaceTestCheck(&test->result,started,"start actual export with isolated missing runtime or loader");
        if(!started) _MarkdownPdfTestFinish(test);
        return;
    }
    bool const started = _MarkdownExportSnapshot(test->target, &error, true);
    _WorkspaceTestCheck(&test->result, started && MarkdownPdf_IsBusy(Globals.hwndMain), "start independent PDF snapshot job");
    if (!started) { _MarkdownPdfTestFinish(test); return; }
    _WorkspaceTestCheck(&test->result, !_MarkdownExportSnapshot(test->target, &error, true) && error == ERROR_BUSY, "overlapping PDF export rejected");
    const char* later = "\r\nAFTER SNAPSHOT SHOULD NOT APPEAR";
    SciCall_AppendText(strlen(later), later);
}
