// Development-only opt-in test driven by NP3_WORKSPACE_SELFTEST_DIR. The runner
// creates an isolated marker and fixtures; this code never picks user documents.
typedef struct WORKSPACE_TEST_RESULT { HANDLE log; int checks, failures; } WORKSPACE_TEST_RESULT;
static void _WorkspaceTestCheck(WORKSPACE_TEST_RESULT* result, bool condition, const char* label)
{
    ++result->checks;
    if (!condition) ++result->failures;
    char line[256]; DWORD written;
    StringCchPrintfA(line, COUNTOF(line), "%s %s\r\n", condition ? "PASS" : "FAIL", label);
    WriteFile(result->log, line, (DWORD)strlen(line), &written, NULL);
}
static bool _WorkspaceTestText(const char* expected)
{
    char text[128] = { 0 };
    SciCall_GetText(COUNTOF(text), text);
    return strcmp(text, expected) == 0;
}
static void _WorkspaceSelfTest()
{
    WCHAR folder[MAX_PATH_EXPLICIT];
    DWORD const length = GetEnvironmentVariableW(L"NP3_WORKSPACE_SELFTEST_DIR", folder, COUNTOF(folder));
    if (!length || length >= COUNTOF(folder)) return;
    HPATHL marker = Path_Allocate(folder); Path_Append(marker, L"workspace-test.marker");
    if (!Path_IsExistingFile(marker)) { Path_Release(marker); return; }
    Path_Release(marker);
    HPATHL logPath = Path_Allocate(folder); Path_Append(logPath, L"workspace-test.log");
    WORKSPACE_TEST_RESULT result = { 0 };
    result.log = Path_CreateFile(logPath, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    Path_Release(logPath);
    if (!IS_VALID_HANDLE(result.log)) return;
    DOCUMENT_WORKSPACE* workspace = _Workspace();
    bool const isolated = workspace && workspace->count == 1 && Path_IsEmpty(Paths.CurrentFile) && !IsSaveNeeded() && SciCall_GetLength() == 0;
    _WorkspaceTestCheck(&result, isolated, "isolated empty startup");
    if (!isolated) { CloseHandle(result.log); return; }
    HPATHL a = Path_Allocate(folder); Path_Append(a, L"a.txt");
    HPATHL b = Path_Allocate(folder); Path_Append(b, L"b.txt");
    _WorkspaceTestCheck(&result, FileLoad(a, FLF_None, 0, 0), "load A");
    _WorkspaceTestCheck(&result, workspace->count == 1 && _WorkspaceTestText("alpha\r\n"), "reuse empty tab");
    _WorkspaceTestCheck(&result, TabCtrl_GetItemCount(workspace->tabs) == 1 && IsWindow(workspace->closeButton), "native tab and close controls");
    WCHAR label[128] = { 0 }; TCITEMW tab = { 0 }; tab.mask = TCIF_TEXT; tab.pszText = label; tab.cchTextMax = COUNTOF(label);
    TabCtrl_GetItem(workspace->tabs, 0, &tab);
    _WorkspaceTestCheck(&result, wcscmp(label, L"a.txt") == 0, "tab uses filename label");
    sptr_t const handleA = SciCall_GetDocPointer();
    unsigned long long const idA = workspace->documents[0].id;
    Encoding_Current(CPI_UTF8SIGN); SciCall_SetEOLMode(SC_EOL_CRLF);
    SciCall_AppendText(1, "A"); SciCall_SetSel(1, 3);
    _WorkspaceTestCheck(&result, IsSaveNeeded() && SciCall_CanUndo(), "A dirty and undoable");
    _WorkspaceTestCheck(&result, FileLoad(b, FLF_None, 0, 0), "load B without saving A");
    _WorkspaceTestCheck(&result, workspace->count == 2 && SciCall_GetDocPointer() != handleA, "separate document handles");
    _WorkspaceTestCheck(&result, workspace->documents[1].id != idA, "stable document IDs");
    _WorkspaceTestCheck(&result, Encoding_GetCurrent() == CPI_UNICODEBOM && _WorkspaceTestText("beta\r\n"), "B UTF16 decoding");
    _WorkspaceTestCheck(&result, workspace->documents[0].modified, "inactive A remains dirty");
    _WorkspaceTestCheck(&result, SendMessage(Globals.hwndMain, WM_FILECHANGEDNOTIFY, (WPARAM)idA, 0) == 0 && _WorkspaceTestText("beta\r\n"), "stale file notification ignored");
    _WorkspaceTestCheck(&result, _WorkspaceOwnsPath(a) && _WorkspaceOwnsPath(b), "inactive snapshot paths remain owned");
    SciCall_AppendText(1, "B"); SciCall_Undo();
    _WorkspaceTestCheck(&result, _WorkspaceTestText("beta\r\n"), "B independent undo");
    SciCall_Redo();
    _WorkspaceTestCheck(&result, _WorkspaceTestText("beta\r\nB"), "B redo");
    SciCall_SetReadOnly(true);
    TabCtrl_SetCurSel(workspace->tabs, 0);
    NMHDR selection = { workspace->tabs, IDC_WORKSPACE_TABS, TCN_SELCHANGE };
    SendMessage(Globals.hwndMain, WM_NOTIFY, IDC_WORKSPACE_TABS, (LPARAM)&selection);
    _WorkspaceTestCheck(&result, workspace->active == 0, "native tab selection activates A");
    _WorkspaceTestCheck(&result, SciCall_GetDocPointer() == handleA && _WorkspaceTestText("alpha\r\nA"), "A content and handle preserved");
    _WorkspaceTestCheck(&result, Encoding_GetCurrent() == CPI_UTF8SIGN && SciCall_GetEOLMode() == SC_EOL_CRLF, "A encoding and EOL restored");
    _WorkspaceTestCheck(&result, !SciCall_GetReadOnly() && SciCall_GetAnchor() == 1 && SciCall_GetCurrentPos() == 3, "A read-only and selection restored");
    SciCall_Undo(); _WorkspaceTestCheck(&result, _WorkspaceTestText("alpha\r\n"), "A undo unaffected by B");
    SciCall_Redo(); _WorkspaceTestCheck(&result, _WorkspaceTestText("alpha\r\nA"), "A redo");
    SciCall_SetSelection(2, 1); SciCall_AddSelection(6, 5);
    _WorkspaceSwitch(1); _WorkspaceSwitch(0);
    _WorkspaceTestCheck(&result, SciCall_GetSelections() == 2 && SciCall_GetSelectionNAnchor(1) == 5 && SciCall_GetSelectionNCaret(1) == 6, "multiple selections preserved");
    SciCall_SetSel(1, 3);
    _WorkspaceTestCheck(&result, FileSave(FSF_SaveAlways), "save A");
    _WorkspaceTestCheck(&result, !IsSaveNeeded() && Path_StrgComparePath(Paths.CurrentFile, a, Paths.WorkingDirectory, true) == 0, "A save state and path");
    _WorkspaceSwitch(1);
    _WorkspaceTestCheck(&result, SciCall_GetReadOnly() && IsSaveNeeded() && _WorkspaceTestText("beta\r\nB"), "B state retained after A save");
    SciCall_SetReadOnly(false);
    _WorkspaceTestCheck(&result, FileSave(FSF_SaveAlways), "save B");
    _WorkspaceTestCheck(&result, FileLoad(a, FLF_None, 0, 0), "open existing A activates tab");
    _WorkspaceTestCheck(&result, workspace->count == 2 && workspace->active == 0 && SciCall_GetDocPointer() == handleA, "duplicate open does not add tab");
    FileWatching.FileWatchingMode = FWM_DONT_CARE; InstallFileWatching(false);
    HPATHL c = Path_Allocate(folder); Path_Append(c, L"c.txt");
    _WorkspaceTestCheck(&result, FileLoad(c, FLF_None, 0, 0) && workspace->count == 3, "third document");
    FileWatching.FileWatchingMode = FWM_DONT_CARE; InstallFileWatching(false);
    _WorkspaceSwitch(0);
    HANDLE external = Path_CreateFile(c, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    DWORD externalWritten;
    if (IS_VALID_HANDLE(external)) { WriteFile(external, "changed\r\n", 9, &externalWritten, NULL); CloseHandle(external); }
    _WorkspaceSwitch(2);
    _WorkspaceTestCheck(&result, IsFileChangedFlagSet() && _WorkspaceTestText("gamma\r\n"), "inactive external modification detected without replacing buffer");
    external = Path_CreateFile(c, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (IS_VALID_HANDLE(external)) { WriteFile(external, "gamma\r\n", 7, &externalWritten, NULL); CloseHandle(external); }
    ResetFileObservationData(true);
    _WorkspaceTestCheck(&result, _WorkspaceCloseActive() && workspace->count == 2, "close third document");
    Path_Release(c);
    _WorkspaceSwitch(1);
    SendWMCommand(Globals.hwndMain, IDM_FILE_CLOSE_TAB);
    _WorkspaceTestCheck(&result, workspace->count == 1, "close-tab command routed");
    _WorkspaceTestCheck(&result, workspace->count == 1 && _WorkspaceTestText("alpha\r\nA"), "closing B preserves A");
    HPATHL folded = Path_Allocate(folder); Path_Append(folded, L"fold.json");
    _WorkspaceTestCheck(&result, FileLoad(folded, FLF_None, 0, 0), "load folding fixture");
    SciCall_SetProperty("fold", "1"); Sci_ColouriseAll();
    _WorkspaceTestCheck(&result, (SciCall_GetFoldLevel(0) & SC_FOLDLEVELHEADERFLAG) != 0, "lexer produces folding header");
    SciCall_FoldLine(0, SC_FOLDACTION_CONTRACT);
    _WorkspaceTestCheck(&result, !SciCall_GetFoldExpanded(0) && !SciCall_GetLineVisible(1), "collapse fixture");
    _WorkspaceSwitch(0); _WorkspaceSwitch(1);
    _WorkspaceTestCheck(&result, !SciCall_GetFoldExpanded(0) && !SciCall_GetLineVisible(1), "fold state restored after tab switch");
    SciCall_FoldLine(0, SC_FOLDACTION_EXPAND);
    _WorkspaceSwitch(0); _WorkspaceSwitch(1);
    _WorkspaceTestCheck(&result, SciCall_GetFoldExpanded(0) && SciCall_GetLineVisible(1), "expanded state remains expanded");
    _WorkspaceCloseActive(); Path_Release(folded);
    HPATHL sessionFile = Path_Allocate(folder); Path_Append(sessionFile, L"workspace.np3session");
    uint32_t sessionError = 0;
    sptr_t const originalHandle = SciCall_GetDocPointer();
    SciCall_AppendText(1, "~");
    _WorkspaceTestCheck(&result, _WorkspaceSessionSave(sessionFile, &sessionError), "capture unsaved workspace session");
    _WorkspaceTestCheck(&result, SciCall_GetDocPointer() == originalHandle && _WorkspaceTestText("alpha\r\nA~"), "session capture keeps active document unchanged");
    SciCall_Undo();
    _WorkspaceTestCheck(&result, _WorkspaceTestText("alpha\r\nA") && !IsSaveNeeded(), "session capture keeps undo and savepoint");
    _WorkspaceTestCheck(&result, _WorkspaceSessionLoad(sessionFile, &sessionError), "restore protected workspace session");
    _WorkspaceTestCheck(&result, workspace->count == 2 && _WorkspaceTestText("alpha\r\nA~") && Encoding_GetCurrent() == CPI_UTF8SIGN && IsSaveNeeded(), "restored unsaved content and encoding");
    _WorkspaceSwitch(0);
    _WorkspaceTestCheck(&result, SciCall_GetDocPointer() == originalHandle && _WorkspaceTestText("alpha\r\nA"), "import retains original tab");
    _WorkspaceReleaseDocument(&workspace->documents[1]); workspace->count = 1; _WorkspaceRefresh();
    HANDLE brokenSession = Path_CreateFile(sessionFile, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    DWORD corruptWritten;
    if (IS_VALID_HANDLE(brokenSession)) { WriteFile(brokenSession, "broken", 6, &corruptWritten, NULL); CloseHandle(brokenSession); }
    _WorkspaceTestCheck(&result, !_WorkspaceSessionLoad(sessionFile, &sessionError) && workspace->count == 1 && SciCall_GetDocPointer() == originalHandle && _WorkspaceTestText("alpha\r\nA"), "corrupt session preserves complete current workspace");
    Path_Release(sessionFile);
    HPATHL exported = Path_Allocate(folder); Path_Append(exported, L"buffer-export.html");
    const char* exportHeading = "\r\n# 导出验证\r\n";
    SciCall_AppendText(strlen(exportHeading), exportHeading);
    unsigned long long const exportVersion = workspace->documents[workspace->active].version;
    _WorkspaceTestCheck(&result, _MarkdownExportSave(exported, &sessionError) && Path_IsExistingFile(exported), "export current unsaved buffer as Markdown HTML");
    _WorkspaceTestCheck(&result, SciCall_GetDocPointer() == originalHandle && workspace->documents[workspace->active].version == exportVersion && IsSaveNeeded() && SciCall_CanUndo(), "export preserves document version dirty state and undo");
    _WorkspaceTestCheck(&result, !_MarkdownExportSave(a, &sessionError) && sessionError == ERROR_SHARING_VIOLATION, "export cannot overwrite an open source document");
    SciCall_Undo();
    _WorkspaceTestCheck(&result, _WorkspaceTestText("alpha\r\nA") && !IsSaveNeeded(), "export buffer change remains one undoable edit");
    Path_Release(exported);
    _WorkspaceTestCheck(&result, _WorkspaceCanClose(FSF_Ask), "all saved tabs can close");
    Path_Release(a); Path_Release(b);
    char summary[128]; DWORD written;
    StringCchPrintfA(summary, COUNTOF(summary), "RESULT checks=%i failures=%i\r\n", result.checks, result.failures);
    WriteFile(result.log, summary, (DWORD)strlen(summary), &written, NULL);
    CloseHandle(result.log);
    _WorkspaceDispose(); DestroyWindow(Globals.hwndMain);
}
