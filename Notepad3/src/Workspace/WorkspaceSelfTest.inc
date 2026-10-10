#include <richedit.h>
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
static bool _WorkspaceTestPanelBrush(HWND panel, HWND child, COLORREF background, COLORREF foreground)
{
    HDC dc=CreateCompatibleDC(NULL); if (!dc) return false;
    HBRUSH brush=(HBRUSH)SendMessageW(panel,WM_CTLCOLORSTATIC,(WPARAM)dc,(LPARAM)child);
    LOGBRUSH info={0};
    bool matches=brush && GetObjectW(brush,sizeof(info),&info)==sizeof(info) && info.lbColor==background && GetTextColor(dc)==foreground;
    DeleteDC(dc); return matches;
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
static bool _WorkspaceTestAiReady(void)
{
    DWORD start = GetTickCount();
    while (AiPanel_IsBusy(_WorkspaceAi()) && GetTickCount() - start < 5000) {
        MsgWaitForMultipleObjectsEx(0, NULL, 50, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        MSG message; while (PeekMessageW(&message, NULL, 0, 0, PM_REMOVE)) { TranslateMessage(&message); DispatchMessageW(&message); }
    }
    return !AiPanel_IsBusy(_WorkspaceAi());
}
static bool _WorkspaceTestBatchReady(void)
{
    DWORD start = GetTickCount();
    while (BatchPanel_IsBusy(_WorkspaceBatch()) && GetTickCount() - start < 5000) {
        MsgWaitForMultipleObjectsEx(0, NULL, 50, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        MSG message; while (PeekMessageW(&message, NULL, 0, 0, PM_REMOVE)) { TranslateMessage(&message); DispatchMessageW(&message); }
    }
    return !BatchPanel_IsBusy(_WorkspaceBatch());
}
static bool _WorkspaceTestPreviewReady(bool capture)
{
    DWORD start=GetTickCount();
    while(GetTickCount()-start<15000) {
        if(capture?MarkdownPreview_IsCaptureDone(Globals.hwndMain):MarkdownPreview_IsRendered(Globals.hwndMain)) return true;
        MsgWaitForMultipleObjectsEx(0,NULL,50,QS_ALLINPUT,MWMO_INPUTAVAILABLE); MSG message;
        while(PeekMessageW(&message,NULL,0,0,PM_REMOVE)) { TranslateMessage(&message); DispatchMessageW(&message); }
    }
    return false;
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
    uint64_t const styleVersion = workspace->documents[workspace->active].version;
    SciCall(SCI_STYLESETFORE, 100, RGB(12,34,56));
    SciCall(SCI_STYLESETBACK, 100, RGB(65,43,21));
    SciCall(SCI_STYLESETUNDERLINE, 100, TRUE);
    SciCall(SCI_STYLESETSIZEFRACTIONAL, 100, 1350);
    SciCall(SCI_SETELEMENTCOLOUR, SC_ELEMENT_CARET, 0xff123456);
    SciCall(SCI_INDICSETSTYLE, INDIC_MAX, INDIC_ROUNDBOX); SciCall(SCI_INDICSETFORE, INDIC_MAX, RGB(30,60,90));
    SciCall(SCI_INDICSETALPHA, INDIC_MAX, 75); SciCall(SCI_INDICSETOUTLINEALPHA, INDIC_MAX, 120);
    SciCall(SCI_INDICSETSTROKEWIDTH, INDIC_MAX, 175);
    SciCall(SCI_MARKERDEFINE, 6, SC_MARK_CIRCLE); SciCall(SCI_MARKERSETFORETRANSLUCENT, 6, 0xcc123456);
    SciCall(SCI_MARKERSETBACKTRANSLUCENT, 6, 0xdd654321); SciCall(SCI_MARKERSETBACKSELECTEDTRANSLUCENT, 6, 0xaa456789);
    SciCall(SCI_MARKERSETALPHA, 6, 127); SciCall(SCI_MARKERSETLAYER, 6, SC_LAYER_UNDER_TEXT); SciCall(SCI_MARKERSETSTROKEWIDTH, 6, 225);
    SciCall(SCI_SETFOLDMARGINCOLOUR, TRUE, RGB(33,44,55)); SciCall(SCI_SETFOLDMARGINHICOLOUR, TRUE, RGB(66,77,88));
    SciCall(SCI_SETMARGINBACKN, 1, RGB(90,80,70));

    SendMessageW(Globals.hwndMain, NP3_WM_EDITOR_STYLE_CHANGED, (WPARAM)secondView, 0);
    _WorkspaceTestCheck(&result, Sci_ViewCall(primaryView, SCI_STYLEGETFORE, 100, 0) == RGB(12,34,56) && Sci_ViewCall(primaryView, SCI_STYLEGETBACK, 100, 0) == RGB(65,43,21) && Sci_ViewCall(primaryView, SCI_STYLEGETUNDERLINE, 100, 0) && Sci_ViewCall(primaryView, SCI_STYLEGETSIZEFRACTIONAL, 100, 0) == 1350, "style refresh copies colors underline and fractional size to primary");
    _WorkspaceTestCheck(&result, Sci_ViewCall(primaryView, SCI_GETELEMENTCOLOUR, SC_ELEMENT_CARET, 0) == SciCall(SCI_GETELEMENTCOLOUR, SC_ELEMENT_CARET, 0), "style refresh copies explicit caret element color");
    _WorkspaceTestCheck(&result, Sci_ViewCall(primaryView, SCI_INDICGETSTYLE, INDIC_MAX, 0) == INDIC_ROUNDBOX && Sci_ViewCall(primaryView, SCI_INDICGETFORE, INDIC_MAX, 0) == RGB(30,60,90) && Sci_ViewCall(primaryView, SCI_INDICGETALPHA, INDIC_MAX, 0) == 75 && Sci_ViewCall(primaryView, SCI_INDICGETOUTLINEALPHA, INDIC_MAX, 0) == 120 && Sci_ViewCall(primaryView, SCI_INDICGETSTROKEWIDTH, INDIC_MAX, 0) == 175, "split copies indicator color alpha outline style and stroke width");
    _WorkspaceTestCheck(&result, Sci_ViewCall(primaryView, SCI_MARKERSYMBOLDEFINED, 6, 0) == SC_MARK_CIRCLE && Sci_ViewCall(primaryView, SCI_MARKERGETFORETRANSLUCENT, 6, 0) == SciCall(SCI_MARKERGETFORETRANSLUCENT, 6, 0) && Sci_ViewCall(primaryView, SCI_MARKERGETBACKTRANSLUCENT, 6, 0) == SciCall(SCI_MARKERGETBACKTRANSLUCENT, 6, 0) && Sci_ViewCall(primaryView, SCI_MARKERGETBACKSELECTEDTRANSLUCENT, 6, 0) == SciCall(SCI_MARKERGETBACKSELECTEDTRANSLUCENT, 6, 0) && Sci_ViewCall(primaryView, SCI_MARKERGETALPHA, 6, 0) == 127 && Sci_ViewCall(primaryView, SCI_MARKERGETLAYER, 6, 0) == SC_LAYER_UNDER_TEXT && Sci_ViewCall(primaryView, SCI_MARKERGETSTROKEWIDTH, 6, 0) == 225, "split copies native marker symbol translucent colors alpha layer and stroke");
    _WorkspaceTestCheck(&result, Sci_ViewCall(primaryView, SCI_GETFOLDMARGINCOLOUR, 0, 0) == RGB(33,44,55) && Sci_ViewCall(primaryView, SCI_GETFOLDMARGINHICOLOUR, 0, 0) == RGB(66,77,88) && Sci_ViewCall(primaryView, SCI_GETMARGINBACKN, 1, 0) == RGB(90,80,70), "split copies fold and marker margin colors without copying widths");
    SciCall(SCI_SETFOLDMARGINCOLOUR, FALSE, 0); SciCall(SCI_SETFOLDMARGINHICOLOUR, FALSE, 0);
    SciCall(SCI_RESETELEMENTCOLOUR, SC_ELEMENT_CARET, 0);
    SciCall(SCI_RESETELEMENTCOLOUR, SC_ELEMENT_WHITE_SPACE_BACK, 0);
    Sci_ViewCall(primaryView, SCI_SETELEMENTCOLOUR, SC_ELEMENT_WHITE_SPACE_BACK, 0xff654321);
    SendMessageW(Globals.hwndMain, NP3_WM_EDITOR_STYLE_CHANGED, (WPARAM)secondView, 0);
    _WorkspaceTestCheck(&result, Sci_ViewCall(primaryView, SCI_GETFOLDMARGINCOLOUR, 0, 0) == -1 && Sci_ViewCall(primaryView, SCI_GETFOLDMARGINHICOLOUR, 0, 0) == -1 && SciCall(SCI_MARKERGETSTROKEWIDTH, MARKER_MAX+1, 0) == 0, "fold margin override reset and marker getter bounds");
    _WorkspaceTestCheck(&result, !Sci_ViewCall(primaryView, SCI_GETELEMENTISSET, SC_ELEMENT_WHITE_SPACE_BACK, 0) && Sci_ViewCall(primaryView, SCI_GETELEMENTCOLOUR, SC_ELEMENT_CARET, 0) == SciCall(SCI_GETELEMENTCOLOUR, SC_ELEMENT_CARET, 0), "style refresh restores caret default and clears stale optional element overrides");
    _WorkspaceTestCheck(&result, Sci_ViewCall(primaryView, SCI_GETFIRSTVISIBLELINE, 0, 0) == leftScroll && SciCall_GetFirstVisibleLine() == rightScroll && Sci_ViewCall(primaryView, SCI_GETZOOM, 0, 0) == 1 && SciCall_GetZoom() == 3 && workspace->documents[workspace->active].version == styleVersion && SciCall_CanUndo(), "style refresh preserves independent scroll zoom document version and undo");
    SendMessageW(primaryView, WM_SETFOCUS, 0, 0);
    SciCall(SCI_STYLESETFORE, 100, RGB(98,76,54));
    SendMessageW(Globals.hwndMain, NP3_WM_EDITOR_STYLE_CHANGED, (WPARAM)primaryView, 0);
    _WorkspaceTestCheck(&result, Sci_ViewCall(secondView, SCI_STYLEGETFORE, 100, 0) == RGB(98,76,54), "style refresh routes from primary to secondary");
    SendMessageW(secondView, WM_SETFOCUS, 0, 0);
    Style_ResetCurrentLexer(Globals.hwndEdit);
    _WorkspaceTestCheck(&result, Sci_ViewCall(primaryView, SCI_STYLEGETFORE, STYLE_DEFAULT, 0) == SciCall(SCI_STYLEGETFORE, STYLE_DEFAULT, 0) && Sci_ViewCall(primaryView, SCI_STYLEGETBACK, STYLE_DEFAULT, 0) == SciCall(SCI_STYLEGETBACK, STYLE_DEFAULT, 0), "actual lexer reset refreshes both split default styles");
    Sci_ViewCall(primaryView, SCI_SETSEL, 1, 2); SciCall_SetSel(4, 6);
    Sci_ViewCall(primaryView, SCI_SETFIRSTVISIBLELINE, 10, 0); SciCall_SetFirstVisibleLine(70);
    SendMessageW(Globals.hwndMain, WM_THEMECHANGED, 0, 0);
    _WorkspaceTestCheck(&result, Sci_ViewCall(primaryView, SCI_GETFIRSTVISIBLELINE, 0, 0) == 10 && SciCall_GetFirstVisibleLine() == 70 && Sci_ViewCall(primaryView, SCI_GETZOOM, 0, 0) == 1 && SciCall_GetZoom() == 3 && Sci_ViewCall(primaryView, SCI_GETANCHOR, 0, 0) == 1 && Sci_ViewCall(primaryView, SCI_GETCURRENTPOS, 0, 0) == 2 && SciCall_GetAnchor() == 4 && SciCall_GetCurrentPos() == 6, "actual theme message preserves independent split scroll zoom and selections");
    _WorkspaceTestCheck(&result, Sci_ViewCall(primaryView, SCI_MARKERGETFORETRANSLUCENT, MARKER_NP3_BOOKMARK, 0) == SciCall(SCI_MARKERGETFORETRANSLUCENT, MARKER_NP3_BOOKMARK, 0) && Sci_ViewCall(primaryView, SCI_GETFOLDMARGINCOLOUR, 0, 0) == SciCall(SCI_GETFOLDMARGINCOLOUR, 0, 0) && workspace->documents[workspace->active].version == styleVersion && SciCall_CanUndo(), "actual theme message synchronizes bookmark fold palette and preserves document version undo");
    RECT gutterRect; GetClientRect(workspace->splitGutter,&gutterRect);
    BITMAPINFO gutterInfo = {0}; gutterInfo.bmiHeader.biSize=sizeof(BITMAPINFOHEADER); gutterInfo.bmiHeader.biWidth=max_i(1,gutterRect.right);
    gutterInfo.bmiHeader.biHeight=-4; gutterInfo.bmiHeader.biPlanes=1; gutterInfo.bmiHeader.biBitCount=32; gutterInfo.bmiHeader.biCompression=BI_RGB;
    HDC gutterDc=CreateCompatibleDC(NULL); void* gutterPixels=NULL;
    HBITMAP gutterBitmap=gutterDc ? CreateDIBSection(gutterDc,&gutterInfo,DIB_RGB_COLORS,&gutterPixels,NULL,0) : NULL;
    HGDIOBJ gutterOld=gutterBitmap ? SelectObject(gutterDc,gutterBitmap) : NULL;
    PANEL_COLORS gutterColors; _PanelColors(&gutterColors);
    if (gutterBitmap) SendMessageW(workspace->splitGutter,WM_PRINTCLIENT,(WPARAM)gutterDc,PRF_CLIENT);
    _WorkspaceTestCheck(&result,gutterBitmap && gutterRect.right>=3 && GetPixel(gutterDc,0,1)==gutterColors.panel && GetPixel(gutterDc,gutterRect.right/2,1)==gutterColors.disabled,"split gutter prints current panel background and visible divider pixels");
    DWORD const gutterGdiBefore=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);
    if (gutterBitmap) for (int i=0;i<40;++i) SendMessageW(workspace->splitGutter,WM_ERASEBKGND,(WPARAM)gutterDc,0);
    _WorkspaceTestCheck(&result,gutterBitmap && GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS)==gutterGdiBefore && workspace->documents[workspace->active].version==styleVersion,"repeated gutter erase uses no new GDI objects and does not edit document");
    if (gutterOld) SelectObject(gutterDc,gutterOld); if (gutterBitmap) DeleteObject(gutterBitmap); if (gutterDc) DeleteDC(gutterDc);
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
    _WorkspaceTestLogReady(); _LogFilterRun();
    _WorkspaceTestCheck(&result, _WorkspaceTestLogReady() && ListView_GetItemCount(GetDlgItem(logPanel, 107)) == 1 && _WorkspaceTestText("alpha\r\nA"), "close during log work and reopen loads fresh results without altering editor");
    // Cancel without pumping publication: even a worker that just finished must not publish its old rows.
    _LogFilterRun();
    SendMessageW(logPanel, WM_COMMAND, MAKEWPARAM(106, BN_CLICKED), (LPARAM)GetDlgItem(logPanel, 106));
    bool cancelReady = _WorkspaceTestLogReady(); WCHAR canceledText[256], expectedCanceled[256];
    GetWindowTextW(GetDlgItem(logPanel,108), canceledText, COUNTOF(canceledText)); GetLngString(IDS_MUI_LOG_CANCELED, expectedCanceled, COUNTOF(expectedCanceled));
    _WorkspaceTestCheck(&result, cancelReady && ListView_GetItemCount(GetDlgItem(logPanel,107)) == 0 && wcscmp(canceledText,expectedCanceled) == 0 && IsWindowEnabled(GetDlgItem(logPanel,105)) && !IsWindowEnabled(GetDlgItem(logPanel,106)), "cancel button discards unpublished log results and restores run controls");
    _WorkspaceTestCheck(&result, _WorkspaceTestText("alpha\r\nA") && !IsSaveNeeded(), "canceling log filter preserves source text and saved state");
    _LogFilterRun(); _WorkspaceTestLogReady();
    _WorkspaceTestCheck(&result, ListView_GetItemCount(GetDlgItem(logPanel,107)) == 1, "log filter can rerun after cancellation");
    FileLoad(b, FLF_DontSave, 0, 0); SetWindowTextW(GetDlgItem(logPanel,101),L"beta"); _LogFilterRun(); _WorkspaceTestLogReady();
    HWND closedSourceList=GetDlgItem(logPanel,107); ListView_SetItemState(closedSourceList,0,LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);
    _WorkspaceCloseActive();
    NMHDR closedLocate={closedSourceList,107,NM_RETURN}; SendMessageW(logPanel,WM_NOTIFY,0,(LPARAM)&closedLocate);
    GetWindowTextW(GetDlgItem(logPanel,108),staleText,COUNTOF(staleText));
    _WorkspaceTestCheck(&result, workspace->count == 1 && workspace->active == 0 && wcscmp(staleText,expectedStale) == 0 && _WorkspaceTestText("alpha\r\nA") && !IsSaveNeeded(), "closed log source rejects activation without changing remaining document");
    _LogFilterToggle();
    _AiToggle();
    HWND aiPanel = FindWindowExW(Globals.hwndMain, NULL, L"NP3AiPanel", NULL);
    _WorkspaceTestCheck(&result, AiPanel_IsOpen(workspace->ai) && GetDlgItem(aiPanel, 110) && GetDlgItem(aiPanel, 113) && !IsWindowEnabled(GetDlgItem(aiPanel, 115)), "AI panel exposes four actions and disables apply without a result");
    AI_PANEL_CAPTURE aiRequest = {AI_REWRITE, ""};
    _WorkspaceTestCheck(&result, !_AiRun(&aiRequest) && _WorkspaceTestText("alpha\r\nA") && !AiPanel_IsBusy(workspace->ai), "unconfigured AI never changes source or starts a request");
    SciCall_SetSel(0, 5);
    AI_PANEL_SOURCE aiSource = {workspace->documents[workspace->active].id, workspace->documents[workspace->active].version, 0, 5, 0, 5};
    AI_PANEL_APPLY aiApply = {aiSource, "alpha", 5, "translated", 10};
    SciCall_SetSel(0, 4);
    _WorkspaceTestCheck(&result, !_AiApply(&aiApply) && _WorkspaceTestText("alpha\r\nA"), "AI apply rejects changed selection");
    SciCall_SetSel(0, 5); SciCall_SetReadOnly(true);
    _WorkspaceTestCheck(&result, !_AiApply(&aiApply) && _WorkspaceTestText("alpha\r\nA"), "AI apply rejects read-only document");
    SciCall_SetReadOnly(false);
    aiApply.source.version++;
    _WorkspaceTestCheck(&result, !_AiApply(&aiApply) && _WorkspaceTestText("alpha\r\nA"), "AI apply rejects stale document version");
    aiApply.source = aiSource; aiApply.source.id++;
    _WorkspaceTestCheck(&result, !_AiApply(&aiApply) && _WorkspaceTestText("alpha\r\nA"), "AI apply rejects another source document");
    aiApply.source = aiSource; aiApply.original = "wrong";
    _WorkspaceTestCheck(&result, !_AiApply(&aiApply) && _WorkspaceTestText("alpha\r\nA"), "AI apply independently verifies original bytes");
    aiApply.original = "alpha";
    aiApply.result = "bad\0result"; aiApply.resultLength = 10;
    _WorkspaceTestCheck(&result, !_AiApply(&aiApply) && _WorkspaceTestText("alpha\r\nA") && !IsSaveNeeded(), "AI apply rejects embedded NUL without editing source");
    aiApply.result = "\xc0\xaf"; aiApply.resultLength = 2;
    _WorkspaceTestCheck(&result, !_AiApply(&aiApply) && _WorkspaceTestText("alpha\r\nA"), "AI apply rejects malformed UTF8 before mutation");
    aiApply.result = "x"; aiApply.resultLength = 2 * 1024 * 1024 + 1;
    _WorkspaceTestCheck(&result, !_AiApply(&aiApply) && _WorkspaceTestText("alpha\r\nA"), "AI apply rejects oversized result before reading bytes");
    aiApply.result = "translated"; aiApply.resultLength = 10;
    _WorkspaceTestCheck(&result, _AiApply(&aiApply) && _WorkspaceTestText("translated\r\nA") && IsSaveNeeded(), "confirmed AI replacement updates selected text and dirty state");
    _WorkspaceTestCheck(&result, !_AiApply(&aiApply) && _WorkspaceTestText("translated\r\nA"), "AI result cannot be applied twice");
    SciCall_Undo();
    _WorkspaceTestCheck(&result, _WorkspaceTestText("alpha\r\nA") && !IsSaveNeeded(), "AI replacement is undone in one step");
    FileLoad(b, FLF_DontSave, 0, 0); SciCall_SetSel(0,4);
    AI_PANEL_SOURCE closedAiSource = {workspace->documents[workspace->active].id,workspace->documents[workspace->active].version,0,4,0,4};
    AI_PANEL_APPLY closedAiApply = {closedAiSource,"beta",4,"replacement",11};
    _WorkspaceCloseActive();
    _WorkspaceTestCheck(&result, !_AiApply(&closedAiApply) && workspace->count == 1 && _WorkspaceTestText("alpha\r\nA") && !IsSaveNeeded(), "AI apply rejects closed source without editing another document");
    FileLoad(b, FLF_DontSave, 0, 0); SciCall_SetSel(0,4);
    _WorkspaceTestCheck(&result, workspace->documents[workspace->active].id != closedAiSource.id && !_AiApply(&closedAiApply) && _WorkspaceTestText("beta\r\nB") && !IsSaveNeeded(), "AI old result remains stale after reopening identical source path and bytes");
    _WorkspaceCloseActive();
    _AiToggle(); _AiToggle();
    _WorkspaceTestCheck(&result, AiPanel_IsOpen(workspace->ai) && !AiPanel_IsBusy(workspace->ai) && _WorkspaceTestText("alpha\r\nA"), "AI panel closes and reopens with editor state intact");
    _AiToggle();
    if (Path_IsEmpty(Paths.IniFile)) {
        AI_PANEL_SAVE failedSave = {0}; WCHAR failedProfile[73] = {0}; char* failedKey = NULL;
        StringCchPrintfW(failedSave.settings.endpoint,COUNTOF(failedSave.settings.endpoint),L"https://rollback-test.invalid/%lu-%llu",GetCurrentProcessId(),GetTickCount64());
        StringCchCopyW(failedSave.settings.model,COUNTOF(failedSave.settings.model),L"rollback-test-model"); failedSave.settings.timeoutMs=5000; failedSave.secret="fake-rollback-new";
        bool absent=AiCredential_ServiceProfile(failedSave.settings.endpoint,failedProfile,COUNTOF(failedProfile))==ERROR_SUCCESS && AiCredential_Load(failedProfile,&failedKey)==ERROR_NOT_FOUND;
        AiCredential_Free(failedKey); failedKey=NULL;
        _WorkspaceTestCheck(&result,absent,"AI rollback fixture credential starts absent");
        if(absent) {
            bool rejected=!_AiSave(&failedSave,false);
            _WorkspaceTestCheck(&result,rejected && AiCredential_Load(failedProfile,&failedKey)==ERROR_NOT_FOUND && !Settings2.AIEndpoint[0],"failed AI settings save rolls back newly created key and runtime configuration");
            AiCredential_Free(failedKey); failedKey=NULL;
            if(AiCredential_Save(failedProfile,"fake-rollback-existing")==ERROR_SUCCESS) {
                rejected=!_AiSave(&failedSave,false);
                bool restored=AiCredential_Load(failedProfile,&failedKey)==ERROR_SUCCESS && !strcmp(failedKey,"fake-rollback-existing");
                _WorkspaceTestCheck(&result,rejected && restored,"failed AI settings save restores previous scoped credential");
                AiCredential_Free(failedKey); AiCredential_Delete(failedProfile);
            } else _WorkspaceTestCheck(&result,false,"prepare existing rollback fixture key");
        }
    }
    WCHAR aiUrl[2049] = {0};
    if (GetEnvironmentVariableW(L"NP3_AI_SELFTEST_URL", aiUrl, COUNTOF(aiUrl))) {
        WCHAR profile[73] = {0}; char* key = NULL;
        bool isolatedKey = AiCredential_ServiceProfile(aiUrl, profile, COUNTOF(profile)) == ERROR_SUCCESS && AiCredential_Load(profile, &key) == ERROR_NOT_FOUND;
        AiCredential_Free(key); key = NULL;
        _WorkspaceTestCheck(&result, isolatedKey, "AI fixture service credential is initially absent");
        if (isolatedKey) {
            _AiToggle(); aiPanel = FindWindowExW(Globals.hwndMain, NULL, L"NP3AiPanel", NULL);
            SetWindowTextW(GetDlgItem(aiPanel,101),aiUrl); SetWindowTextW(GetDlgItem(aiPanel,102),L"panel-test-model");
            SetWindowTextW(GetDlgItem(aiPanel,103),L"fake-panel-test-key"); SetDlgItemInt(aiPanel,104,5000,FALSE);
            SendMessageW(aiPanel,WM_COMMAND,MAKEWPARAM(105,BN_CLICKED),0);
            WCHAR savedEndpoint[2049] = {0};
            IniFileGetString(Paths.IniFile,L"Settings2",L"AIEndpoint",L"",savedEndpoint,COUNTOF(savedEndpoint));
            bool savedKey = AiCredential_Load(profile,&key) == ERROR_SUCCESS && strcmp(key,"fake-panel-test-key") == 0;
            AiCredential_Free(key); key = NULL;
            _WorkspaceTestCheck(&result, savedKey && !wcscmp(savedEndpoint,aiUrl) && GetWindowTextLengthW(GetDlgItem(aiPanel,103)) == 0, "AI panel persists service and scoped key with independent readback");
            for (int action=AI_EXPLAIN;action<=AI_GENERATE;++action) {
                SciCall_SetSel(0,5); SetWindowTextW(GetDlgItem(aiPanel,120),action == AI_GENERATE ? L"生成中文" : L"");
                SendMessageW(aiPanel,WM_COMMAND,MAKEWPARAM(110+action,BN_CLICKED),0);
                bool ready=_WorkspaceTestAiReady(); WCHAR output[128] = {0}; GetWindowTextW(GetDlgItem(aiPanel,122),output,COUNTOF(output));
                _WorkspaceTestCheck(&result,ready && !wcscmp(output,L"模拟结果") && _WorkspaceTestText("alpha\r\nA"),"AI action reaches local service and previews result without editing");
                _WorkspaceTestCheck(&result,IsWindowEnabled(GetDlgItem(aiPanel,115)) == (action != AI_EXPLAIN),"AI explain remains view-only while edit results allow manual apply");
                if(action==AI_REWRITE) {
                    PANEL_COLORS aiColors={0}; _PanelColors(&aiColors);
                    CHARFORMAT2W originalFormat = {0}, resultFormat = {0}; originalFormat.cbSize=sizeof(originalFormat); resultFormat.cbSize=sizeof(resultFormat);
                    SendMessageW(GetDlgItem(aiPanel,121),EM_SETSEL,0,1); SendMessageW(GetDlgItem(aiPanel,121),EM_GETCHARFORMAT,SCF_SELECTION,(LPARAM)&originalFormat);
                    SendMessageW(GetDlgItem(aiPanel,122),EM_SETSEL,0,1); SendMessageW(GetDlgItem(aiPanel,122),EM_GETCHARFORMAT,SCF_SELECTION,(LPARAM)&resultFormat);
                    _WorkspaceTestCheck(&result,(originalFormat.dwMask & CFM_BACKCOLOR) && originalFormat.crBackColor==aiColors.removed,"AI original changed range has themed difference highlighting");
                    _WorkspaceTestCheck(&result,(resultFormat.dwMask & CFM_BACKCOLOR) && resultFormat.crBackColor==aiColors.added,"AI result changed range has themed difference highlighting");
                    CHARRANGE diffSelection={0}; SendMessageW(GetDlgItem(aiPanel,122),EM_EXGETSEL,0,(LPARAM)&diffSelection);
                    unsigned long long themeSourceVersion=workspace->documents[workspace->active].version;
                    SendMessageW(aiPanel,NP3_WM_PANEL_REFRESH,0,0);
                    CHARRANGE afterThemeSelection={0}; SendMessageW(GetDlgItem(aiPanel,122),EM_EXGETSEL,0,(LPARAM)&afterThemeSelection);
                    SendMessageW(GetDlgItem(aiPanel,122),EM_GETCHARFORMAT,SCF_SELECTION,(LPARAM)&resultFormat);
                    GetWindowTextW(GetDlgItem(aiPanel,122),output,COUNTOF(output));
                    _WorkspaceTestCheck(&result,!wcscmp(output,L"模拟结果") && diffSelection.cpMin==afterThemeSelection.cpMin && diffSelection.cpMax==afterThemeSelection.cpMax && resultFormat.crBackColor==aiColors.added && resultFormat.crTextColor==aiColors.highlightText,"AI theme refresh retains result selection and themed difference colors");
                    _WorkspaceTestCheck(&result,workspace->documents[workspace->active].version==themeSourceVersion && _WorkspaceTestText("alpha\r\nA"),"AI theme refresh does not edit source document");

                    SendMessageW(aiPanel,WM_COMMAND,MAKEWPARAM(115,BN_CLICKED),0);
                    _WorkspaceTestCheck(&result,_WorkspaceTestText("模拟结果\r\nA") && !IsWindowEnabled(GetDlgItem(aiPanel,115)),"AI preview apply button changes original selection once");
                    SciCall_Undo(); _WorkspaceTestCheck(&result,_WorkspaceTestText("alpha\r\nA") && !IsSaveNeeded(),"actual AI response replacement undoes in one step");
                }
            }
            SciCall_SetSel(0,5); SetWindowTextW(GetDlgItem(aiPanel,120),L"delay");
            SendMessageW(aiPanel,WM_COMMAND,MAKEWPARAM(112,BN_CLICKED),0);
            _WorkspaceTestCheck(&result,AiPanel_IsBusy(workspace->ai) && !IsWindowEnabled(GetDlgItem(aiPanel,105)),"AI running request disables configuration changes");
            DWORD waitStart=GetTickCount(); while(GetTickCount()-waitStart<200) { MsgWaitForMultipleObjectsEx(0,NULL,20,QS_ALLINPUT,MWMO_INPUTAVAILABLE); MSG message; while(PeekMessageW(&message,NULL,0,0,PM_REMOVE)) { TranslateMessage(&message); DispatchMessageW(&message); } }
            SendMessageW(aiPanel,WM_COMMAND,MAKEWPARAM(114,BN_CLICKED),0);
            _WorkspaceTestCheck(&result,_WorkspaceTestAiReady() && _WorkspaceTestText("alpha\r\nA") && !IsWindowEnabled(GetDlgItem(aiPanel,115)),"AI panel cancel finishes promptly without applying delayed response");
            SendMessageW(aiPanel,WM_COMMAND,MAKEWPARAM(106,BN_CLICKED),0);
            _WorkspaceTestCheck(&result,AiCredential_Load(profile,&key) == ERROR_NOT_FOUND && !key,"AI delete-key button removes only fixture service credential");
            AiCredential_Free(key); AiCredential_Delete(profile); _AiToggle();
        }
    }
    if(GetEnvironmentVariableW(L"NP3_MARKDOWN_CHECK_PREVIEW",NULL,0)) {
        const char* markdown="# 原生 Markdown 图表与公式\n\n```mermaid\nflowchart LR\n A[开始] --> B[结束]\n```\n\n行内公式 $E=mc^2$\n\n$$\\frac{1}{2}+\\sum_{i=1}^{n} i$$\n";
        UndoTransActionBegin(); SciCall_SetTargetRange(0,SciCall_GetLength()); Sci_ReplaceTargetTestChgHist((DocPos)strlen(markdown),markdown); EndUndoTransAction();
        MarkdownPreview_Toggle(Globals.hwndMain); bool ready=_WorkspaceTestPreviewReady(false);
        _WorkspaceTestCheck(&result,ready,"actual WebView2 preview completes offline advanced rendering");
        WCHAR capturePath[32768]; StringCchPrintfW(capturePath,COUNTOF(capturePath),L"%s\\preview.png",folder);
        bool capture=ready && MarkdownPreview_Capture(Globals.hwndMain,capturePath) && _WorkspaceTestPreviewReady(true);
        _WorkspaceTestCheck(&result,capture && MarkdownPreview_IsCaptureSuccessful(Globals.hwndMain) && GetFileAttributesW(capturePath)!=INVALID_FILE_ATTRIBUTES,"actual native preview captures PNG after graph and formula layout");
        MarkdownPreview_Toggle(Globals.hwndMain); SciCall_Undo();
        _WorkspaceTestCheck(&result,!MarkdownPreview_IsOpen(Globals.hwndMain) && _WorkspaceTestText("alpha\r\nA") && !IsSaveNeeded(),"advanced preview and capture preserve source undo and saved state");
    }
    // Batch UI commands use only the isolated documents created by this runner.
    _WorkspaceTestCheck(&result, FileLoad(b, FLF_None, 0, 0) && workspace->count == 2 && _WorkspaceTestText("beta\r\nB"), "batch fixture opens second saved document");
    unsigned long long batchAId = workspace->documents[0].id, batchBId = workspace->documents[1].id;
    _BatchToggle(); HWND batchPanel = FindWindowExW(Globals.hwndMain, NULL, L"NP3BatchPanel", NULL);
    _WorkspaceTestCheck(&result, batchPanel && BatchPanel_IsOpen(workspace->batch) && GetDlgItem(batchPanel,120) && GetDlgItem(batchPanel,121) && GetDlgItem(batchPanel,122), "batch native document list and result controls exist");
    SetWindowTextW(GetDlgItem(batchPanel,102),L"a"); SetWindowTextW(GetDlgItem(batchPanel,103),L"X");
    unsigned long long batchVersion = workspace->documents[1].version;
    SendMessageW(batchPanel,WM_COMMAND,MAKEWPARAM(110,BN_CLICKED),0);
    _WorkspaceTestCheck(&result,_WorkspaceTestBatchReady() && ListView_GetItemCount(GetDlgItem(batchPanel,120))==2,"batch background preview publishes both document rows");
    _WorkspaceTestCheck(&result,_WorkspaceTestText("beta\r\nB") && workspace->active==1 && workspace->documents[1].version==batchVersion && !IsSaveNeeded(),"batch preview preserves active document, text, version and saved state");
    WCHAR batchOriginal[128]={0},batchProposed[128]={0};
    GetWindowTextW(GetDlgItem(batchPanel,121),batchOriginal,COUNTOF(batchOriginal)); GetWindowTextW(GetDlgItem(batchPanel,122),batchProposed,COUNTOF(batchProposed));
    _WorkspaceTestCheck(&result,wcsstr(batchOriginal,L"alpha") && wcsstr(batchProposed,L"XlphX"),"batch actual result controls show original and transformed text");
    _WorkspaceTestCheck(&result,ListView_GetCheckState(GetDlgItem(batchPanel,120),0) && ListView_GetCheckState(GetDlgItem(batchPanel,120),1),"batch changed documents are checked for explicit confirmation");
    ListView_SetCheckState(GetDlgItem(batchPanel,120),0,FALSE);
    SendMessageW(batchPanel,WM_COMMAND,MAKEWPARAM(112,BN_CLICKED),0);
    _WorkspaceTestCheck(&result,_WorkspaceTestText("betX\r\nB") && IsSaveNeeded() && workspace->active==1,"batch applies only checked document and restores active tab");
    _WorkspaceSwitch(0); _WorkspaceTestCheck(&result,_WorkspaceTestText("alpha\r\nA") && !IsSaveNeeded(),"batch unchecked document remains unchanged"); _WorkspaceSwitch(1);
    SendMessageW(batchPanel,WM_COMMAND,MAKEWPARAM(112,BN_CLICKED),0);
    _WorkspaceTestCheck(&result,!IsWindowEnabled(GetDlgItem(batchPanel,112)) && _WorkspaceTestText("betX\r\nB"),"batch UI refuses repeated application");
    SciCall_Undo(); _WorkspaceTestCheck(&result,_WorkspaceTestText("beta\r\nB") && !IsSaveNeeded(),"batch replacement is one undo and restores save point");
    SendMessageW(batchPanel,WM_COMMAND,MAKEWPARAM(110,BN_CLICKED),0); _WorkspaceTestBatchReady();
    _WorkspaceSwitch(0); SciCall_AppendText(1,"~"); _WorkspaceSwitch(1);
    SendMessageW(batchPanel,WM_COMMAND,MAKEWPARAM(112,BN_CLICKED),0);
    _WorkspaceTestCheck(&result,_WorkspaceTestText("beta\r\nB") && !IsSaveNeeded(),"batch stale inactive source rejects whole transaction before editing other documents");
    _WorkspaceSwitch(0); _WorkspaceTestCheck(&result,_WorkspaceTestText("alpha\r\nA~"),"batch preserves independent edit made after preview"); SciCall_Undo(); _WorkspaceSwitch(1);
    SendMessageW(batchPanel,WM_COMMAND,MAKEWPARAM(110,BN_CLICKED),0); _WorkspaceTestBatchReady();
    SciCall_SetReadOnly(TRUE); SendMessageW(batchPanel,WM_COMMAND,MAKEWPARAM(112,BN_CLICKED),0);
    _WorkspaceTestCheck(&result,_WorkspaceTestText("beta\r\nB") && !IsSaveNeeded(),"batch apply rechecks current read-only status for entire selection"); SciCall_SetReadOnly(FALSE);
    SetWindowTextW(GetDlgItem(batchPanel,103),L"Y");
    _WorkspaceTestCheck(&result,!IsWindowEnabled(GetDlgItem(batchPanel,112)),"changing batch options invalidates earlier confirmation");
    SendMessageW(batchPanel,WM_COMMAND,MAKEWPARAM(110,BN_CLICKED),0); _WorkspaceTestBatchReady();
    SendMessageW(batchPanel,WM_COMMAND,MAKEWPARAM(112,BN_CLICKED),0);
    _WorkspaceTestCheck(&result,_WorkspaceTestText("betY\r\nB") && IsSaveNeeded(),"batch confirms replacement in second document");
    _WorkspaceSwitch(0); _WorkspaceTestCheck(&result,_WorkspaceTestText("YlphY\r\nA") && IsSaveNeeded(),"batch confirms replacement in first document");
    SciCall_Undo(); _WorkspaceTestCheck(&result,_WorkspaceTestText("alpha\r\nA") && !IsSaveNeeded(),"batch first document has independent single undo"); _WorkspaceSwitch(1);
    SciCall_Undo(); _WorkspaceTestCheck(&result,_WorkspaceTestText("beta\r\nB") && !IsSaveNeeded(),"batch second document has independent single undo");
    BATCH_CHANGE rollbackChanges[2]={{batchAId,workspace->documents[0].version,"alpha\r\nA",8,"changed A",9},{batchBId,workspace->documents[1].version,"beta\r\nB",7,"changed B",9}};
    BATCH_APPLY rollbackRequest={rollbackChanges,2};
    _WorkspaceTestCheck(&result,_BatchApplyTransaction(&rollbackRequest,1)==-1 && _WorkspaceTestText("beta\r\nB") && workspace->active==1 && !IsSaveNeeded(),"batch injected mid-transaction failure rolls back and restores active document");
    _WorkspaceSwitch(0); _WorkspaceTestCheck(&result,_WorkspaceTestText("alpha\r\nA") && !IsSaveNeeded(),"batch rollback independently restores original bytes and save point"); _WorkspaceSwitch(1);
    BATCH_CHANGE emptyChange={batchBId,workspace->documents[1].version,"beta\r\nB",7,"",0}; BATCH_APPLY emptyRequest={&emptyChange,1};
    _WorkspaceTestCheck(&result,_BatchApply(&emptyRequest)==1 && SciCall_GetLength()==0 && IsSaveNeeded(),"batch permits confirmed empty replacement without truncation ambiguity");
    SciCall_Undo(); _WorkspaceTestCheck(&result,_WorkspaceTestText("beta\r\nB") && !IsSaveNeeded(),"batch empty replacement restores all bytes with one undo");
    SendMessageW(batchPanel,WM_COMMAND,MAKEWPARAM(110,BN_CLICKED),0); _WorkspaceTestBatchReady();
    _WorkspaceTestCheck(&result,_WorkspaceCloseActive() && workspace->count==1,"batch source tab may close after preview");
    SendMessageW(batchPanel,WM_COMMAND,MAKEWPARAM(112,BN_CLICKED),0);
    _WorkspaceTestCheck(&result,_WorkspaceTestText("alpha\r\nA") && !IsSaveNeeded(),"batch closed source rejects whole application without editing remaining document");
    _WorkspaceTestCheck(&result,FileLoad(b,FLF_None,0,0) && _WorkspaceTestText("beta\r\nB"),"batch fixture reopens unchanged saved source");
    SendMessageW(batchPanel,WM_COMMAND,MAKEWPARAM(110,BN_CLICKED),0); SendMessageW(batchPanel,WM_COMMAND,MAKEWPARAM(111,BN_CLICKED),0);
    _WorkspaceTestCheck(&result,_WorkspaceTestBatchReady() && _WorkspaceTestText("beta\r\nB") && !IsWindowEnabled(GetDlgItem(batchPanel,112)),"batch cancellation discards proposals without applying");
    _BatchToggle(); _BatchToggle();
    _WorkspaceTestCheck(&result,BatchPanel_IsOpen(workspace->batch) && _WorkspaceTestText("beta\r\nB"),"batch panel closes and reopens without changing editor");
    _BatchToggle(); _WorkspaceCloseActive();
    _WorkspaceTestCheck(&result,workspace->count==1 && _WorkspaceTestText("alpha\r\nA") && !IsSaveNeeded(),"batch regression preserves original saved workspace");
    _BatchToggle(); batchPanel=FindWindowExW(Globals.hwndMain,NULL,L"NP3BatchPanel",NULL);
    _WorkspaceTestCheck(&result,_WorkspaceTestBatchReady() && GetDlgItem(batchPanel,113) && IsWindowEnabled(GetDlgItem(batchPanel,113)),"batch native file chooser button is available");
    SciCall_AppendText(1,"~");
    WCHAR missingBatchFile[32768]; StringCchPrintfW(missingBatchFile,COUNTOF(missingBatchFile),L"%s\\batch-missing-file.txt",folder);
    const WCHAR* missingBatchPaths[]={Path_Get(b),missingBatchFile}; BATCH_FILES batchFiles={missingBatchPaths,2};
    _WorkspaceTestCheck(&result,_BatchOpenFiles(&batchFiles)==0 && workspace->count==1 && _WorkspaceTestText("alpha\r\nA~") && IsSaveNeeded(),"batch selected missing path rejects whole admission before opening valid files");
    const WCHAR* directoryBatchPaths[]={folder}; batchFiles.paths=directoryBatchPaths; batchFiles.count=1;
    _WorkspaceTestCheck(&result,_BatchOpenFiles(&batchFiles)==0 && workspace->count==1,"batch directory selection is rejected without adding a tab");
    batchFiles.count=65; _WorkspaceTestCheck(&result,_BatchOpenFiles(&batchFiles)==0 && workspace->count==1,"batch oversized file selection rejected before reading path array");
    const WCHAR* duplicateBatchPaths[]={Path_Get(b),Path_Get(b),Path_Get(a)}; batchFiles.paths=duplicateBatchPaths; batchFiles.count=3;
    _WorkspaceTestCheck(&result,_BatchOpenFiles(&batchFiles)==1 && workspace->count==2 && workspace->active==0 && _WorkspaceTestText("alpha\r\nA~"),"batch file admission deduplicates selected files and restores original active tab");
    _WorkspaceTestCheck(&result,IsSaveNeeded() && SciCall_CanUndo(),"batch choosing an already open file preserves unsaved edits and undo");
    bool selectedBatchTab=workspace->count==2 && _WorkspaceSwitch(1);
    _WorkspaceTestCheck(&result,selectedBatchTab && _WorkspaceTestText("beta\r\nB") && Encoding_GetCurrent()==CPI_UNICODEBOM && !IsSaveNeeded(),"batch selected UTF16 file retains native decoding and encoding");
    if (selectedBatchTab) _WorkspaceCloseActive(); SciCall_Undo();
    _WorkspaceTestCheck(&result,_WorkspaceTestText("alpha\r\nA") && !IsSaveNeeded(),"batch file admission leaves existing text edit independently undoable");
    PANEL_COLORS themeColors={0}; _PanelColors(&themeColors);
    HWND batchList=GetDlgItem(batchPanel,120);
    _WorkspaceTestCheck(&result,ListView_GetBkColor(batchList)==themeColors.field && ListView_GetTextColor(batchList)==themeColors.text,"batch list uses current panel palette");
    _WorkspaceTestCheck(&result,_WorkspaceTestPanelBrush(batchPanel,GetDlgItem(batchPanel,121),themeColors.field,themeColors.text),"batch read-only preview brush and foreground use current palette");
    SetWindowTextW(GetDlgItem(batchPanel,102),L"a"); SetWindowTextW(GetDlgItem(batchPanel,103),L"主题");
    SendMessageW(batchPanel,WM_COMMAND,MAKEWPARAM(110,BN_CLICKED),0); _WorkspaceTestBatchReady();
    CHARRANGE batchThemeSelection={0}; batchThemeSelection.cpMin=1; batchThemeSelection.cpMax=3;
    SendMessageW(GetDlgItem(batchPanel,121),EM_EXSETSEL,0,(LPARAM)&batchThemeSelection);
    unsigned long long batchThemeVersion=workspace->documents[0].version;
    MsgThemeChanged(Globals.hwndMain,0,0);
    for(int themeIndex=0;themeIndex<5;++themeIndex) PanelTheme_RefreshChildren(Globals.hwndMain);
    DWORD themeGdiBefore=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);
    for(int themeIndex=0;themeIndex<40;++themeIndex) PanelTheme_RefreshChildren(Globals.hwndMain);
    DWORD themeGdiAfter=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);
    CHARRANGE batchThemeAfter={0}; SendMessageW(GetDlgItem(batchPanel,121),EM_EXGETSEL,0,(LPARAM)&batchThemeAfter);
    _WorkspaceTestCheck(&result,ListView_GetItemCount(batchList)==1 && ListView_GetCheckState(batchList,0) && IsWindowEnabled(GetDlgItem(batchPanel,112)) && batchThemeAfter.cpMin==1 && batchThemeAfter.cpMax==3,"batch theme refresh retains proposals, check state, confirmation and selection");
    _WorkspaceTestCheck(&result,themeGdiBefore && themeGdiAfter && themeGdiAfter<=themeGdiBefore+2,"repeated panel theme refresh has bounded GDI object count");
    HFONT panelFont=(HFONT)SendMessageW(GetDlgItem(batchPanel,113),WM_GETFONT,0,0); LOGFONTW panelFontInfo={0};
    NONCLIENTMETRICSW panelMetrics={0}; panelMetrics.cbSize=sizeof(panelMetrics);
    bool panelMetricsReady=SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS,sizeof(panelMetrics),&panelMetrics,0,GetDpiForWindow(Globals.hwndMain));
    _WorkspaceTestCheck(&result,panelFont && panelFont!=(HFONT)GetStockObject(DEFAULT_GUI_FONT) && GetObjectW(panelFont,sizeof(panelFontInfo),&panelFontInfo)==sizeof(panelFontInfo) && panelMetricsReady && panelFontInfo.lfHeight==panelMetrics.lfMessageFont.lfHeight,"panel controls use an owned DPI message font");
    char themeNote[160]; DWORD themeWritten=0;
    StringCchPrintfA(themeNote,COUNTOF(themeNote),"THEME dark=%i panel=%lu field=%lu text=%lu GDI_before=%lu GDI_after=%lu DPI=%u\r\n",themeColors.dark,(unsigned long)themeColors.panel,(unsigned long)themeColors.field,(unsigned long)themeColors.text,(unsigned long)themeGdiBefore,(unsigned long)themeGdiAfter,GetDpiForWindow(Globals.hwndMain));
    WriteFile(result.log,themeNote,(DWORD)strlen(themeNote),&themeWritten,NULL);
    _WorkspaceTestCheck(&result,workspace->documents[0].version==batchThemeVersion && _WorkspaceTestText("alpha\r\nA") && !IsSaveNeeded(),"repeated theme refresh preserves editor version text and saved state");
    _LogFilterToggle(); HWND themedLog=FindWindowExW(Globals.hwndMain,NULL,L"NP3LogFilterPanel",NULL);
    _WorkspaceTestCheck(&result,ListView_GetBkColor(GetDlgItem(themedLog,107))==themeColors.field && ListView_GetTextColor(GetDlgItem(themedLog,107))==themeColors.text,"log filter list receives shared panel theme");
    _LogFilterToggle();
    _FileTreeToggle(); HWND themedTree=FindWindowExW(Globals.hwndMain,NULL,L"NP3WorkspaceFileTree",NULL); HWND themedTreeView=FindWindowExW(themedTree,NULL,WC_TREEVIEWW,NULL);
    _WorkspaceTestCheck(&result,_WorkspaceTestTreeReady() && TreeView_GetBkColor(themedTreeView)==themeColors.field && TreeView_GetTextColor(themedTreeView)==themeColors.text,"file tree receives shared panel theme without losing directory loading");
    _FileTreeToggle(); _BatchToggle();
    Path_Release(a); Path_Release(b);
    char summary[128]; DWORD written;
    StringCchPrintfA(summary, COUNTOF(summary), "RESULT checks=%i failures=%i\r\n", result.checks, result.failures);
    WriteFile(result.log, summary, (DWORD)strlen(summary), &written, NULL);
    CloseHandle(result.log);
    _WorkspaceDispose(); DestroyWindow(Globals.hwndMain);
}
