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
static bool _WorkspaceTestTreeReady()
{
    DWORD const start = GetTickCount();
    while (FileTree_IsLoading(Globals.hwndMain) && GetTickCount() - start < 5000) {
        MsgWaitForMultipleObjectsEx(0, NULL, 50, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        MSG message;
        while (PeekMessageW(&message, NULL, 0, 0, PM_REMOVE)) { TranslateMessage(&message); DispatchMessageW(&message); }
    }
    return !FileTree_IsLoading(Globals.hwndMain);
}
static bool _WorkspaceTestLogReady(void)
{
    DWORD start = GetTickCount();
    while (LogPanel_IsBusy(Globals.hwndMain) && GetTickCount() - start < 5000) {
        MsgWaitForMultipleObjectsEx(0, NULL, 50, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        MSG message;
        while (PeekMessageW(&message, NULL, 0, 0, PM_REMOVE)) { TranslateMessage(&message); DispatchMessageW(&message); }
    }
    return !LogPanel_IsBusy(Globals.hwndMain);
}
static HTREEITEM _WorkspaceTestTreeChild(HWND tree, HTREEITEM parent, const WCHAR* name)
{
    for (HTREEITEM item = TreeView_GetChild(tree, parent); item; item = TreeView_GetNextSibling(tree, item)) {
        WCHAR label[256]; TVITEMW entry = { 0 }; entry.mask = TVIF_TEXT; entry.hItem = item; entry.pszText = label; entry.cchTextMax = COUNTOF(label);
        if (TreeView_GetItem(tree, &entry) && wcscmp(label, name) == 0) return item;
    }
    return NULL;
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
    const char* invalidJson = "{\r\n\"键\": 1,\r\n}";
    SciCall_SelectAll(); SciCall_ReplaceSel(invalidJson);
    unsigned long long const checkVersion = workspace->documents[workspace->active].version;
    SciCall_SetReadOnly(true);
    _WorkspaceTestCheck(&result, _ConfigCheck(CONFIG_JSON, false) == 1 && SciCall_LineFromPosition(SciCall_GetSelectionStart()) == 2, "read-only JSON check locates actual syntax error");
    _WorkspaceTestCheck(&result, _WorkspaceTestText(invalidJson) && workspace->documents[workspace->active].version == checkVersion, "configuration check retains text and document version");
    SciCall_SetReadOnly(false);
    _WorkspaceTestCheck(&result, SciCall_CanUndo(), "configuration check retains undo history after unlocking");
    SciCall_Undo();
    _WorkspaceTestCheck(&result, _WorkspaceTestText("alpha\r\nA") && !IsSaveNeeded(), "configuration check creates no extra undo edit");
    const char* invalidIni = "[section]\r\nmissing-equals";
    SciCall_SelectAll(); SciCall_ReplaceSel(invalidIni);
    _WorkspaceTestCheck(&result, _ConfigCheck(CONFIG_INI, false) == 1 && SciCall_GetSelectionStart() == 11 && SciCall_GetColumn(SciCall_GetSelectionStart()) == 0, "INI check locates error in editor byte coordinates");
    SciCall_Undo();
    SciCall_SelectAll(); SciCall_ReplaceSel("[section]\r\nkey=value");
    _WorkspaceTestCheck(&result, _ConfigCheck(CONFIG_INI, false) == 0, "valid INI structure accepted in actual editor");
    SciCall_Undo();
    _WorkspaceTestCheck(&result, _WorkspaceCanClose(FSF_Ask), "all saved tabs can close");
    _SplitToggle();
    HWND primaryView = workspace->primaryView, secondView = workspace->splitView;
    _WorkspaceTestCheck(&result, secondView && Sci_ViewCall(secondView, SCI_GETDOCPOINTER, 0, 0) == originalHandle, "split view shares actual Scintilla document");
    _SplitLayout(0, 0, 800, 400);
    int initialRatio = workspace->splitRatio;
    SendMessageW(workspace->splitGutter, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(2, 2));
    SendMessageW(workspace->splitGutter, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(100, 2));
    _WorkspaceTestCheck(&result, workspace->splitRatio > initialRatio && workspace->splitDragging, "split gutter drag changes view width ratio");
    SendMessageW(workspace->splitGutter, WM_CANCELMODE, 0, 0);
    _WorkspaceTestCheck(&result, workspace->splitRatio == initialRatio && !workspace->splitDragging && GetCapture() != workspace->splitGutter, "canceling split drag restores ratio and releases capture");
    _SplitLayout(0, 0, 800, 400);
    SendMessageW(workspace->splitGutter, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(2, 2));
    SendMessageW(workspace->splitGutter, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(-30000, 2));
    SendMessageW(workspace->splitGutter, WM_LBUTTONUP, 0, 0);
    _WorkspaceTestCheck(&result, workspace->splitRatio > 0 && workspace->splitRatio < initialRatio && !workspace->splitDragging, "split drag clamps extreme positions to minimum view width");
    workspace->splitRatio = initialRatio;
    _WorkspaceTestCheck(&result, _SplitLayout(0, 0, 0, 0) == 0 && _SplitLayout(0, 0, 100, 20) <= 100, "split layout tolerates zero and narrow editor space");
    _SplitRelayout();
    SciCall_SetSel(0, 0); Sci_ViewCall(secondView, SCI_SETSEL, 3, 3);
    _WorkspaceTestCheck(&result, SciCall_GetCurrentPos() == 0 && Sci_ViewCall(secondView, SCI_GETCURRENTPOS, 0, 0) == 3, "split selections remain independent");
    SendMessageW(secondView, WM_SETFOCUS, 0, 0);
    _WorkspaceTestCheck(&result, Globals.hwndEdit == secondView && g_hwndEditWindow == secondView, "split focus routes editor commands to second view");
    unsigned long long beforeSplitEdit = workspace->documents[workspace->active].version;
    SciCall_AppendText(1, "+");
    _WorkspaceTestCheck(&result, Sci_ViewCall(primaryView, SCI_GETLENGTH, 0, 0) == 9 && IsSaveNeeded(), "second view edit updates shared primary content and dirty state");
    _WorkspaceTestCheck(&result, workspace->documents[workspace->active].version == beforeSplitEdit + 1, "shared document edit produces one version increment");
    SendMessageW(primaryView, WM_SETFOCUS, 0, 0); SciCall_Undo();
    _WorkspaceTestCheck(&result, _WorkspaceTestText("alpha\r\nA") && !IsSaveNeeded() && Sci_ViewCall(secondView, SCI_GETLENGTH, 0, 0) == 8, "primary undo reverses second view edit in shared history");
    SciCall_SetReadOnly(true); _SplitSync();
    _WorkspaceTestCheck(&result, Sci_ViewCall(secondView, SCI_GETREADONLY, 0, 0), "split read-only state protects both views");
    SciCall_SetReadOnly(false); _SplitSync();
    SciCall_SetSel(1, 2); Sci_ViewCall(secondView, SCI_SETSEL, 4, 6);
    SciCall_SetZoom(1); Sci_ViewCall(secondView, SCI_SETZOOM, 3, 0);
    SendMessageW(secondView, WM_SETFOCUS, 0, 0);
    SciCall_AppendText(1, "+");
    _WorkspaceTestCheck(&result, FileSave(FSF_None) && !IsSaveNeeded(), "saving edited right view writes its active source document");
    SciCall_Undo(); FileSave(FSF_None);
    _WorkspaceTestCheck(&result, _WorkspaceTestText("alpha\r\nA") && !IsSaveNeeded(), "undo and resave from right view restores original saved bytes");
    Sci_ViewCall(primaryView, SCI_SETSEL, 1, 2); SciCall_SetSel(4, 6);
    _WorkspaceTestCheck(&result, FileLoad(b, FLF_DontSave, 0, 0) && _WorkspaceTestText("beta\r\nB") && Sci_ViewCall(primaryView, SCI_GETDOCPOINTER, 0, 0) == SciCall_GetDocPointer(), "opening another tab while split focused updates both views");
    _WorkspaceSwitch(0);
    _WorkspaceTestCheck(&result, _WorkspaceTestText("alpha\r\nA") && Sci_ViewCall(primaryView, SCI_GETDOCPOINTER, 0, 0) == originalHandle, "split tab switch restores original shared document");
    _WorkspaceTestCheck(&result, Sci_ViewCall(primaryView, SCI_GETANCHOR, 0, 0) == 1 && Sci_ViewCall(primaryView, SCI_GETCURRENTPOS, 0, 0) == 2 && SciCall_GetAnchor() == 4 && SciCall_GetCurrentPos() == 6, "tab return restores each split selection separately");
    _WorkspaceTestCheck(&result, Sci_ViewCall(primaryView, SCI_GETZOOM, 0, 0) == 1 && SciCall_GetZoom() == 3, "tab return restores independent split zoom levels");
    char longSplitText[8192] = {0};
    for (int line = 0; line < 180; ++line) StringCchCatA(longSplitText, COUNTOF(longSplitText), "\r\n0123456789012345678901234567890123456789");
    SciCall_AppendText(strlen(longSplitText), longSplitText);
    Sci_ViewCall(primaryView, SCI_SETWRAPMODE, SC_WRAP_NONE, 0); SciCall_SetWrapMode(SC_WRAP_NONE);
    Sci_ViewCall(primaryView, SCI_SETFIRSTVISIBLELINE, 10, 0); SciCall_SetFirstVisibleLine(70);
    DocLn leftScroll = Sci_ViewCall(primaryView, SCI_GETFIRSTVISIBLELINE, 0, 0), rightScroll = SciCall_GetFirstVisibleLine();
    _WorkspaceSwitch(1); _WorkspaceSwitch(0);
    _WorkspaceTestCheck(&result, leftScroll != rightScroll && Sci_ViewCall(primaryView, SCI_GETFIRSTVISIBLELINE, 0, 0) == leftScroll && SciCall_GetFirstVisibleLine() == rightScroll, "long document restores independent split scroll positions");
    SciCall_Undo();
    Sci_ViewCall(primaryView, SCI_SETSEL, 1, 2); SciCall_SetSel(4, 6);
    _WorkspaceSwitch(1); _WorkspaceCloseActive();
    _SplitToggle();
    _WorkspaceTestCheck(&result, !workspace->splitView && Globals.hwndEdit == primaryView && _WorkspaceTestText("alpha\r\nA") && !IsSaveNeeded(), "closing focused split retains document and activates primary view");
    _SplitToggle();
    _WorkspaceTestCheck(&result, Sci_ViewCall(workspace->splitView, SCI_GETANCHOR, 0, 0) == 4 && Sci_ViewCall(workspace->splitView, SCI_GETCURRENTPOS, 0, 0) == 6, "reopening split retains right view selection");
    _SplitToggle();
    _SplitToggle();
    HPATHL splitFoldFile = Path_Allocate(folder); Path_Append(splitFoldFile, L"fold.json");
    bool splitFoldLoaded = FileLoad(splitFoldFile, FLF_DontSave, 0, 0); Path_Release(splitFoldFile);
    Sci_ColouriseAll();
    SciCall_FoldLine(1, SC_FOLDACTION_CONTRACT);
    Sci_ViewCall(workspace->splitView, SCI_FOLDLINE, 1, SC_FOLDACTION_EXPAND);
    _WorkspaceTestCheck(&result, splitFoldLoaded && !SciCall_GetFoldExpanded(1) && Sci_ViewCall(workspace->splitView, SCI_GETFOLDEXPANDED, 1, 0), "split views permit opposite fold states in shared document");
    _WorkspaceSwitch(0); _WorkspaceSwitch(1);
    _WorkspaceTestCheck(&result, !SciCall_GetFoldExpanded(1) && Sci_ViewCall(workspace->splitView, SCI_GETFOLDEXPANDED, 1, 0), "tab return restores independent left and right folds");
    _SplitToggle(); _SplitToggle();
    _WorkspaceTestCheck(&result, !SciCall_GetFoldExpanded(1) && Sci_ViewCall(workspace->splitView, SCI_GETFOLDEXPANDED, 1, 0), "reopening split restores its expanded state beside folded primary");
    _SplitToggle(); _WorkspaceCloseActive();
    _FileTreeToggle();
    _WorkspaceTestCheck(&result, FileTree_IsOpen(Globals.hwndMain) && _WorkspaceTestTreeReady(), "native file tree loads asynchronously");
    HWND panel = FindWindowExW(Globals.hwndMain, NULL, L"NP3WorkspaceFileTree", NULL);
    HWND tree = FindWindowExW(panel, NULL, WC_TREEVIEWW, NULL);
    HTREEITEM root = TreeView_GetRoot(tree);
    HTREEITEM folderItem = _WorkspaceTestTreeChild(tree, root, L"nested");
    _WorkspaceTestCheck(&result, folderItem && !TreeView_GetChild(tree, folderItem), "directory children remain lazy before expansion");
    TreeView_Expand(tree, folderItem, TVE_EXPAND);
    _WorkspaceTestCheck(&result, _WorkspaceTestTreeReady() && _WorkspaceTestTreeChild(tree, folderItem, L"child.txt"), "expanding directory loads its children");
    HTREEITEM queuedFolders[12] = {0};
    for (int i = 0; i < 12; ++i) {
        wchar_t queuedLabel[32]; StringCchPrintfW(queuedLabel, COUNTOF(queuedLabel), L"queued-%02i", i);
        queuedFolders[i] = _WorkspaceTestTreeChild(tree, root, queuedLabel);
        TreeView_Expand(tree, queuedFolders[i], TVE_EXPAND);
    }
    bool queuedComplete = _WorkspaceTestTreeReady();
    for (int i = 0; i < 12; ++i) queuedComplete = queuedComplete && queuedFolders[i] && _WorkspaceTestTreeChild(tree, queuedFolders[i], L"child.txt");
    _WorkspaceTestCheck(&result, queuedComplete, "all concurrent directory expansions eventually populate");
    HTREEITEM largeItem = _WorkspaceTestTreeChild(tree, root, L"large");
    TreeView_Expand(tree, largeItem, TVE_EXPAND);
    bool firstPageReady = _WorkspaceTestTreeReady();
    int firstPageCount = 0; HTREEITEM moreItem = NULL;
    for (HTREEITEM item = TreeView_GetChild(tree, largeItem); item; item = TreeView_GetNextSibling(tree, item)) { ++firstPageCount; moreItem = item; }
    _WorkspaceTestCheck(&result, firstPageReady && firstPageCount == 5001, "large directory shows bounded first page and continuation action");
    HTREEITEM retainedItem = TreeView_GetChild(tree, largeItem);
    TreeView_SelectItem(tree, moreItem);
    NMHDR nextPage = {tree, 0, NM_RETURN}; SendMessageW(panel, WM_NOTIFY, 0, (LPARAM)&nextPage);
    bool allPagesReady = _WorkspaceTestTreeReady();
    bool seenFiles[5012] = {false}; int fileCount = 0; bool uniqueFiles = true;
    for (HTREEITEM item = TreeView_GetChild(tree, largeItem); item; item = TreeView_GetNextSibling(tree, item)) {
        wchar_t fileLabel[64]; TVITEMW entry = {0}; entry.hItem = item; entry.mask = TVIF_TEXT; entry.pszText = fileLabel; entry.cchTextMax = COUNTOF(fileLabel);
        TreeView_GetItem(tree, &entry);
        wchar_t* suffix = NULL; long number = wcstol(fileLabel + 5, &suffix, 10);
        if (wcsncmp(fileLabel, L"item-", 5) || wcscmp(suffix, L".txt") || number < 0 || number >= 5012 || seenFiles[number]) uniqueFiles = false;
        else seenFiles[number] = true;
        ++fileCount;
    }
    _WorkspaceTestCheck(&result, allPagesReady && uniqueFiles && fileCount == 5012, "continuation returns every file exactly once and removes action at end");
    _WorkspaceTestCheck(&result, retainedItem == TreeView_GetChild(tree, largeItem), "continuation preserves existing tree nodes");
    HTREEITEM fileItem = _WorkspaceTestTreeChild(tree, root, L"b.txt");
    SciCall_AppendText(1, "+");
    TreeView_SelectItem(tree, fileItem);
    NMHDR activate = { tree, 0, NM_RETURN }; SendMessageW(panel, WM_NOTIFY, 0, (LPARAM)&activate);
    _WorkspaceTestCheck(&result, workspace->count == 2 && _WorkspaceTestText("beta\r\nB"), "file-tree activation opens file in another tab");
    _WorkspaceSwitch(0);
    _WorkspaceTestCheck(&result, _WorkspaceTestText("alpha\r\nA+") && IsSaveNeeded(), "file-tree opening preserves unsaved source tab");
    SciCall_Undo(); _WorkspaceSwitch(1); _WorkspaceCloseActive();
    _WorkspaceTestCheck(&result, !FileTree_SetRoot(Globals.hwndMain, L"?:\\invalid") && TreeView_GetRoot(tree) == root, "invalid folder leaves current tree intact");
    _FileTreeToggle();
    _WorkspaceTestCheck(&result, !FileTree_IsOpen(Globals.hwndMain), "file tree closes independently of documents");
    _FileTreeToggle();
    bool refreshed = FileTree_SetRoot(Globals.hwndMain, folder) && FileTree_SetRoot(Globals.hwndMain, folder) && _WorkspaceTestTreeReady();
    panel = FindWindowExW(Globals.hwndMain, NULL, L"NP3WorkspaceFileTree", NULL);
    tree = FindWindowExW(panel, NULL, WC_TREEVIEWW, NULL);
    _WorkspaceTestCheck(&result, refreshed && _WorkspaceTestTreeChild(tree, TreeView_GetRoot(tree), L"nested"), "repeated root refresh discards stale work and loads replacement");
    FileTree_SetRoot(Globals.hwndMain, folder);
    _FileTreeToggle(); // Close before dispatching the pending read result.
    _FileTreeToggle();
    _WorkspaceTestCheck(&result, _WorkspaceTestTreeReady() && _WorkspaceTestText("alpha\r\nA") && !IsSaveNeeded(), "close during read and reopen preserves editor and loads fresh tree");
    _FileTreeToggle();
    SciCall_BeginUndoAction();
    const char* logLines = "\r\nERROR 中文\r\ninfo\r\nERROR end";
    SciCall_AppendText(strlen(logLines), logLines); SciCall_EndUndoAction();
    unsigned long long logVersion = workspace->documents[workspace->active].version;
    _LogFilterToggle();
    HWND logPanel = FindWindowExW(Globals.hwndMain, NULL, L"NP3LogFilterPanel", NULL);
    HWND logList = GetDlgItem(logPanel, 107);
    SetWindowTextW(GetDlgItem(logPanel, 101), L"ERROR");
    SendMessageW(logPanel, WM_COMMAND, MAKEWPARAM(105, BN_CLICKED), (LPARAM)GetDlgItem(logPanel, 105));
    _WorkspaceTestCheck(&result, LogPanel_IsOpen(Globals.hwndMain) && _WorkspaceTestLogReady() && ListView_GetItemCount(logList) == 2, "native log panel asynchronously filters unsaved buffer");
    _WorkspaceTestCheck(&result, workspace->documents[workspace->active].version == logVersion && IsSaveNeeded(), "log filtering retains document version and dirty state");
    FileLoad(b, FLF_DontSave, 0, 0);
    ListView_SetItemState(logList, 0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    NMHDR locate = {logList, 107, NM_RETURN}; SendMessageW(logPanel, WM_NOTIFY, 0, (LPARAM)&locate);
    _WorkspaceTestCheck(&result, workspace->active == 0 && SciCall_GetSelectionStart() == 10 && SciCall_GetSelectionEnd() == 15, "log result activation returns to source tab and selects actual match");
    SciCall_BeginUndoAction(); SciCall_AppendText(1, "!"); SciCall_EndUndoAction();
    _WorkspaceSwitch(1);
    SendMessageW(logPanel, WM_NOTIFY, 0, (LPARAM)&locate);
    WCHAR staleText[256], expectedStale[256]; GetWindowTextW(GetDlgItem(logPanel, 108), staleText, COUNTOF(staleText));
    GetLngString(IDS_MUI_LOG_STALE, expectedStale, COUNTOF(expectedStale));
    _WorkspaceTestCheck(&result, workspace->active == 1 && wcscmp(staleText, expectedStale) == 0, "changed source rejects stale log offsets without switching tabs");
    _WorkspaceCloseActive(); SciCall_Undo(); SciCall_Undo();
    _WorkspaceTestCheck(&result, _WorkspaceTestText("alpha\r\nA") && !IsSaveNeeded(), "log filtering and locating preserve original undo history");
    SetWindowTextW(GetDlgItem(logPanel, 101), L"["); SendMessageW(GetDlgItem(logPanel, 102), BM_SETCHECK, BST_CHECKED, 0);
    _LogFilterRun();
    WCHAR patternError[256], expectedPattern[256];
    bool errorReady = _WorkspaceTestLogReady(); GetWindowTextW(GetDlgItem(logPanel, 108), patternError, COUNTOF(patternError));
    GetLngString(IDS_MUI_LOG_PATTERN_ERROR, expectedPattern, COUNTOF(expectedPattern));
    _WorkspaceTestCheck(&result, errorReady && ListView_GetItemCount(logList) == 0 && wcsncmp(patternError, expectedPattern, wcslen(expectedPattern)) == 0, "log panel reports invalid regex and clears old results");
    SetWindowTextW(GetDlgItem(logPanel, 101), L"alpha"); SendMessageW(GetDlgItem(logPanel, 102), BM_SETCHECK, BST_UNCHECKED, 0);
    _LogFilterRun(); _LogFilterToggle(); _LogFilterToggle();
    logPanel = FindWindowExW(Globals.hwndMain, NULL, L"NP3LogFilterPanel", NULL); SetWindowTextW(GetDlgItem(logPanel, 101), L"alpha");
    _LogFilterRun();
    _WorkspaceTestCheck(&result, _WorkspaceTestLogReady() && ListView_GetItemCount(GetDlgItem(logPanel, 107)) == 1 && _WorkspaceTestText("alpha\r\nA"), "close during log work and reopen loads fresh results without altering editor");
    _LogFilterToggle();
    Path_Release(a); Path_Release(b);
    char summary[128]; DWORD written;
    StringCchPrintfA(summary, COUNTOF(summary), "RESULT checks=%i failures=%i\r\n", result.checks, result.failures);
    WriteFile(result.log, summary, (DWORD)strlen(summary), &written, NULL);
    CloseHandle(result.log);
    _WorkspaceDispose(); DestroyWindow(Globals.hwndMain);
}
