// Native document ownership and activation. Included by Notepad3.c so the legacy
// save/monitoring flags stay behind one transition boundary during migration.
#define WORKSPACE_DOCUMENT_LIMIT 64
typedef struct WORKSPACE_DOCUMENT {
    sptr_t handle;
    unsigned long long id, version;
    HPATHL path;
    void* encryption;
    bool encryptedRecovery, recoveryConflict;
    char* selection;
    DocLn* folds;
    size_t foldCount;
    FILEVARS vars;
    PEDITLEXER lexer;
    cpi_enc_t encoding;
    DocPos anchor, caret;
    DocLn firstLine;
    int xOffset, zoom, eol, codePage, tabWidth, indent, wrapMode;
    bool forcedDirty, modified, readOnly, readOnlyMode, huge, softLocked, preserveTime, inconsistentEOLs;
    FOCUSEDVIEW_T focused;
    FILE_WATCHING_MODE watchingMode;
    bool monitoringLog, fileChanged, fileDeleted;
    WIN32_FIND_DATA observedFile;
    WCHAR titleExcerpt[MIDSZ_BUFFER];
} WORKSPACE_DOCUMENT;
typedef struct DOCUMENT_WORKSPACE {
    HWND tabs, closeButton;
    int count, active;
    unsigned long long nextId;
    bool transitioning;
    WORKSPACE_DOCUMENT documents[WORKSPACE_DOCUMENT_LIMIT];
} DOCUMENT_WORKSPACE;

static bool _FileLoadSingle(const HPATHL path, FileLoadFlags flags, DocPos position, DocLn line);
static DOCUMENT_WORKSPACE* _Workspace() { return (DOCUMENT_WORKSPACE*)Globals.workspace; }

static void _WorkspaceCapture()
{
    DOCUMENT_WORKSPACE* workspace = _Workspace();
    if (!workspace || workspace->active < 0 || workspace->active >= workspace->count) return;
    WORKSPACE_DOCUMENT* doc = &workspace->documents[workspace->active];
    Path_Reset(doc->path, Path_Get(Paths.CurrentFile));
    Encryption_CaptureContext(doc->encryption);
    doc->vars = Globals.fvCurFile;
    doc->lexer = Style_GetCurrentLexerPtr();
    doc->encoding = Encoding_GetCurrent();
    doc->anchor = SciCall_GetAnchor(); doc->caret = SciCall_GetCurrentPos();
    DocPos const selectionLength = SciCall_GetSelectionSerialized(NULL);
    char* selection = selectionLength > 0 ? AllocMem((size_t)selectionLength + 1, HEAP_ZERO_MEMORY) : NULL;
    if (selection) SciCall_GetSelectionSerialized(selection);
    FreeMem(doc->selection); doc->selection = selection;
    size_t foldCount = 0;
    for (DocLn line = SciCall_ContractedFoldNext(0); line >= 0; line = SciCall_ContractedFoldNext(line + 1)) ++foldCount;
    DocLn* folds = foldCount ? AllocMem(foldCount * sizeof(DocLn), HEAP_ZERO_MEMORY) : NULL;
    if (!foldCount || folds) {
        size_t index = 0;
        for (DocLn line = SciCall_ContractedFoldNext(0); line >= 0; line = SciCall_ContractedFoldNext(line + 1)) folds[index++] = line;
        FreeMem(doc->folds); doc->folds = folds; doc->foldCount = foldCount;
    }
    doc->firstLine = SciCall_GetFirstVisibleLine(); doc->xOffset = SciCall_GetXOffset();
    doc->zoom = SciCall_GetZoom(); doc->eol = SciCall_GetEOLMode(); doc->codePage = SciCall_GetCodePage();
    doc->tabWidth = SciCall_GetTabWidth(); doc->indent = SciCall_GetIndent(); doc->wrapMode = SciCall_GetWrapMode();
    doc->forcedDirty = s_NeedSavingForced; doc->modified = IsSaveNeeded();
    doc->readOnly = SciCall_GetReadOnly(); doc->readOnlyMode = Settings.DocReadOnlyMode;
    doc->huge = Flags.bHugeFileLoadState; doc->softLocked = Flags.bSettingsFileSoftLocked;
    doc->preserveTime = Flags.bPreserveFileModTime; doc->inconsistentEOLs = Globals.bDocHasInconsistentEOLs;
    doc->focused = FocusedView; doc->watchingMode = FileWatching.FileWatchingMode;
    doc->monitoringLog = FileWatching.MonitoringLog;
    doc->observedFile = s_FileChgObsvrData.fdCurFile;
    doc->fileChanged = IsFileChangedFlagSet(); doc->fileDeleted = IsFileDeletedFlagSet();
    StringCchCopy(doc->titleExcerpt, COUNTOF(doc->titleExcerpt), s_wchTitleExcerpt);
}

static void _WorkspaceRefresh()
{
    DOCUMENT_WORKSPACE* workspace = _Workspace();
    if (!workspace || workspace->transitioning || !IsWindow(workspace->tabs)) return;
    int const shown = TabCtrl_GetItemCount(workspace->tabs);
    for (int i = shown; i > workspace->count; --i) TabCtrl_DeleteItem(workspace->tabs, i - 1);
    for (int i = 0; i < workspace->count; ++i) {
        WORKSPACE_DOCUMENT* doc = &workspace->documents[i];
        bool const current = i == workspace->active;
        HPATHL path = current ? Paths.CurrentFile : doc->path;
        WCHAR label[MAX_PATH_EXPLICIT] = { 0 };
        GetLngString(IDS_MUI_UNTITLED, label, COUNTOF(label));
        if (Path_IsNotEmpty(path)) Path_GetDisplayName(label, COUNTOF(label), path, NULL, true);
        if (current ? IsSaveNeeded() : doc->modified) StringCchCat(label, COUNTOF(label), L" *");
        TCITEMW item = { 0 }; item.mask = TCIF_TEXT; item.pszText = label;
        if (i >= shown) TabCtrl_InsertItem(workspace->tabs, i, &item);
        else TabCtrl_SetItem(workspace->tabs, i, &item);
    }
    TabCtrl_SetCurSel(workspace->tabs, workspace->active);
}

static bool _WorkspaceEnsure()
{
    if (_Workspace()) return true;
    DOCUMENT_WORKSPACE* workspace = AllocMem(sizeof(DOCUMENT_WORKSPACE), HEAP_ZERO_MEMORY);
    if (!workspace) return false;
    WORKSPACE_DOCUMENT* doc = &workspace->documents[0];
    doc->path = Path_Allocate(NULL); doc->encryption = Encryption_CreateContext();
    if (!doc->path || !doc->encryption) {
        Path_Release(doc->path); Encryption_FreeContext(doc->encryption); FreeMem(workspace); return false;
    }
    doc->handle = SciCall_GetDocPointer(); SciCall_AddRefDocument(doc->handle);
    workspace->count = 1;
    doc->id = ++workspace->nextId;
    INITCOMMONCONTROLSEX init = { sizeof(init), ICC_TAB_CLASSES };
    InitCommonControlsEx(&init);
    workspace->tabs = CreateWindowExW(0, WC_TABCONTROLW, L"", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | TCS_SINGLELINE | TCS_FOCUSNEVER,
        0, 0, 0, 0, Globals.hwndMain, (HMENU)IDC_WORKSPACE_TABS, Globals.hInstance, NULL);
    workspace->closeButton = CreateWindowExW(0, L"BUTTON", L"×", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        0, 0, 0, 0, Globals.hwndMain, (HMENU)IDM_FILE_CLOSE_TAB, Globals.hInstance, NULL);
    if (!workspace->tabs || !workspace->closeButton) {
        if (workspace->tabs) DestroyWindow(workspace->tabs);
        if (workspace->closeButton) DestroyWindow(workspace->closeButton);
        SciCall_ReleaseDocument(doc->handle); Path_Release(doc->path);
        Encryption_FreeContext(doc->encryption); FreeMem(workspace); return false;
    }
    SendMessage(workspace->tabs, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), FALSE);
    SendMessage(workspace->closeButton, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), FALSE);
    Globals.workspace = workspace;
    _WorkspaceCapture(); _WorkspaceRefresh();
    RECT rect; GetClientRect(Globals.hwndMain, &rect);
    SendMessage(Globals.hwndMain, WM_SIZE, 0, MAKELPARAM(rect.right, rect.bottom));
    return true;
}

static int _WorkspaceLayout(int x, int y, int width)
{
    DOCUMENT_WORKSPACE* workspace = _Workspace();
    if (!workspace) return 0;
    int const height = MulDiv(28, GetDpiForWindow(Globals.hwndMain), 96);
    MoveWindow(workspace->tabs, x, y, max_i(0, width - height), height, TRUE);
    MoveWindow(workspace->closeButton, x + max_i(0, width - height), y, min_i(width, height), height, TRUE);
    return height;
}

static void _WorkspaceRestore(int index)
{
    DOCUMENT_WORKSPACE* workspace = _Workspace();
    WORKSPACE_DOCUMENT* doc = &workspace->documents[index];
    workspace->active = index;
    SciCall_SetDocPointer(doc->handle);
    Path_Reset(Paths.CurrentFile, Path_Get(doc->path));
    Globals.fvCurFile = doc->vars; Globals.bDocHasInconsistentEOLs = doc->inconsistentEOLs;
    Encoding_Current(doc->encoding); Encoding_Forced(CPI_NONE); Encoding_SrcWeak(CPI_NONE);
    Encryption_RestoreContext(doc->encryption);
    Flags.bHugeFileLoadState = doc->huge; Flags.bSettingsFileSoftLocked = doc->softLocked;
    Flags.bPreserveFileModTime = doc->preserveTime; Settings.DocReadOnlyMode = doc->readOnlyMode;
    s_NeedSavingForced = doc->forcedDirty;
    StringCchCopy(s_wchTitleExcerpt, COUNTOF(s_wchTitleExcerpt), doc->titleExcerpt);
    FocusedView.HideNonMatchedLines = false;
    if (doc->lexer) Style_SetLexer(Globals.hwndEdit, doc->lexer);
    else Style_SetLexerFromFile(Globals.hwndEdit, Paths.CurrentFile);
    SciCall_SetCodePage(doc->codePage); SciCall_SetEOLMode(doc->eol);
    SciCall_SetTabWidth(doc->tabWidth); SciCall_SetIndent(doc->indent); SciCall_SetWrapMode(doc->wrapMode);
    SciCall_SetZoom(doc->zoom); SciCall_SetReadOnly(doc->readOnly);
    if (doc->foldCount) {
        Sci_ColouriseAll();
        for (size_t i = 0; i < doc->foldCount; ++i) {
            DocLn const line = doc->folds[i];
            if (line < SciCall_GetLineCount() && (SciCall_GetFoldLevel(line) & SC_FOLDLEVELHEADERFLAG)) SciCall_FoldLine(line, SC_FOLDACTION_CONTRACT);
        }
    }
    if (doc->focused.HideNonMatchedLines) EditToggleView(Globals.hwndEdit);
    if (doc->selection) SciCall_SetSelectionSerialized(doc->selection);
    else SciCall_SetSel(doc->anchor, doc->caret);
    SciCall_SetFirstVisibleLine(doc->firstLine); SciCall_SetXOffset(doc->xOffset);
    FileWatching.FileWatchingMode = doc->watchingMode; FileWatching.MonitoringLog = doc->monitoringLog;
    FileWatching.LogRotateRetryCount = 0;
    SciCall_SetEndAtLastLine(doc->monitoringLog ? false : !Settings.ScrollPastEOF);
    EditBookmarkResetNavigation(); ResetFileObservationData(true); InstallFileWatching(true);
    if (Path_IsNotEmpty(Paths.CurrentFile)) {
        InterlockedIncrement(&s_FileChgObsvrData.iObservationGeneration);
        s_FileChgObsvrData.fdCurFile = doc->observedFile;
        InterlockedIncrement(&s_FileChgObsvrData.iObservationGeneration);
        if (doc->fileChanged && IS_VALID_HANDLE(s_FileChgObsvrData.hEventFileChanged)) SetEvent(s_FileChgObsvrData.hEventFileChanged);
        if (doc->fileDeleted && IS_VALID_HANDLE(s_FileChgObsvrData.hEventFileDeleted)) SetEvent(s_FileChgObsvrData.hEventFileDeleted);
        RaiseFlagIfCurrentFileChanged();
    }
    _SetEnumWindowsItems(Globals.hwndMain); UpdateSaveSettingsCmds();
    if (IsSaveNeeded()) AutoSaveStart(true);
    MarkdownPreview_Changed(Globals.hwndMain);
}

static bool _WorkspaceSwitch(int index)
{
    DOCUMENT_WORKSPACE* workspace = _Workspace();
    if (!workspace || workspace->transitioning || index < 0 || index >= workspace->count) return false;
    if (index == workspace->active) return true;
    workspace->transitioning = true;
    _WorkspaceCapture();
    AutoSaveStop(); InstallFileWatching(false); KillTimer(Globals.hwndMain, ID_LOGROTATETIMER);
    while (SciCall_GetUndoSequence() > 0) SciCall_EndUndoAction();
    if (FocusedView.HideNonMatchedLines) EditToggleView(Globals.hwndEdit);
    _WorkspaceRestore(index);
    workspace->transitioning = false;
    _WorkspaceRefresh(); UpdateToolbar_Now(Globals.hwndMain); UpdateMargins(true);
    SetFocus(Globals.hwndEdit);
    return true;
}

static void _WorkspaceReleaseDocument(WORKSPACE_DOCUMENT* doc)
{
    SciCall_ReleaseDocument(doc->handle); Path_Release(doc->path);
    Encryption_FreeContext(doc->encryption); FreeMem(doc->selection); FreeMem(doc->folds); SecureZeroMemory(doc, sizeof(*doc));
}
static bool _WorkspaceCloseActive()
{
    DOCUMENT_WORKSPACE* workspace = _Workspace();
    if (!workspace || workspace->transitioning || !FileSave(FSF_Ask)) return false;
    AutoSaveStop(); InstallFileWatching(false); KillTimer(Globals.hwndMain, ID_LOGROTATETIMER);
    workspace->transitioning = true;
    if (workspace->count == 1) {
        HPATHL empty = Path_Allocate(NULL);
        bool const result = _FileLoadSingle(empty, FLF_New | FLF_DontSave, 0, 0);
        Path_Release(empty); _WorkspaceCapture(); workspace->transitioning = false;
        _WorkspaceRefresh(); SetFocus(Globals.hwndEdit); return result;
    }
    int const removed = workspace->active;
    if (FocusedView.HideNonMatchedLines) EditToggleView(Globals.hwndEdit);
    _WorkspaceReleaseDocument(&workspace->documents[removed]);
    --workspace->count;
    for (int i = removed; i < workspace->count; ++i) workspace->documents[i] = workspace->documents[i + 1];
    SecureZeroMemory(&workspace->documents[workspace->count], sizeof(WORKSPACE_DOCUMENT));
    _WorkspaceRestore(min_i(removed, workspace->count - 1));
    workspace->transitioning = false;
    _WorkspaceRefresh(); UpdateToolbar_Now(Globals.hwndMain); UpdateMargins(true);
    SetFocus(Globals.hwndEdit);
    return true;
}
static bool _WorkspaceCanClose(FileSaveFlags flags)
{
    DOCUMENT_WORKSPACE* workspace = _Workspace();
    if (!workspace) return FileSave(flags);
    int const original = workspace->active;
    for (int i = 0; i < workspace->count; ++i) {
        if (!_WorkspaceSwitch(i) || !FileSave(flags)) { _WorkspaceSwitch(original); return false; }
    }
    _WorkspaceSwitch(original);
    return true;
}
static void _WorkspaceDispose()
{
    DOCUMENT_WORKSPACE* workspace = _Workspace();
    if (!workspace) return;
    for (int i = 0; i < workspace->count; ++i) _WorkspaceReleaseDocument(&workspace->documents[i]);
    Globals.workspace = NULL; FreeMem(workspace);
}

bool FileLoad(const HPATHL path, const FileLoadFlags flags, const DocPos position, const DocLn line)
{
    if (!_WorkspaceEnsure()) { InfoBoxLng(MB_ICONWARNING, NULL, IDS_MUI_WORKSPACE_LIMIT); return false; }
    DOCUMENT_WORKSPACE* workspace = _Workspace();
    if (flags & FLF_Reload) {
        bool const result = _FileLoadSingle(path, flags, position, line);
        _WorkspaceCapture(); _WorkspaceRefresh(); return result;
    }
    HPATHL resolved = Path_Copy(path);
    if (!resolved) return false;
    if (!(flags & FLF_New)) {
        if (Path_IsEmpty(resolved) && !OpenFileDlg(Globals.hwndMain, resolved, resolved)) { Path_Release(resolved); return false; }
        Path_NormalizeEx(resolved, Paths.WorkingDirectory, true, Flags.bSearchPathIfRelative);
        _WorkspaceCapture();
        for (int i = 0; i < workspace->count; ++i) {
            if (Path_IsNotEmpty(workspace->documents[i].path) && Path_StrgComparePath(resolved, workspace->documents[i].path, Paths.WorkingDirectory, true) == 0) {
                Path_Release(resolved); return _WorkspaceSwitch(i);
            }
        }
    }
    bool const reuseEmpty = workspace->count == 1 && Path_IsEmpty(Paths.CurrentFile) && SciCall_GetLength() == 0 && !IsSaveNeeded();
    if (reuseEmpty) {
        bool const result = _FileLoadSingle(resolved, flags | FLF_DontSave, position, line);
        Path_Release(resolved); _WorkspaceCapture(); _WorkspaceRefresh(); return result;
    }
    if (workspace->count >= WORKSPACE_DOCUMENT_LIMIT) {
        Path_Release(resolved); InfoBoxLng(MB_ICONWARNING, NULL, IDS_MUI_WORKSPACE_LIMIT); return false;
    }
    WORKSPACE_DOCUMENT* doc = &workspace->documents[workspace->count];
    doc->path = Path_Allocate(NULL); doc->encryption = Encryption_CreateContext();
    doc->handle = SciCall_CreateDocument(0, SC_DOCUMENTOPTION_DEFAULT);
    if (!doc->path || !doc->encryption || !doc->handle) {
        if (doc->handle) SciCall_ReleaseDocument(doc->handle);
        Path_Release(doc->path); Encryption_FreeContext(doc->encryption); SecureZeroMemory(doc, sizeof(*doc));
        Path_Release(resolved); InfoBoxLng(MB_ICONWARNING, NULL, IDS_MUI_WORKSPACE_LIMIT); return false;
    }
    doc->id = ++workspace->nextId;
    int const previous = workspace->active;
    workspace->transitioning = true;
    _WorkspaceCapture();
    AutoSaveStop(); InstallFileWatching(false); KillTimer(Globals.hwndMain, ID_LOGROTATETIMER);
    if (FocusedView.HideNonMatchedLines) EditToggleView(Globals.hwndEdit);
    workspace->active = workspace->count++;
    SciCall_SetDocPointer(doc->handle); SciCall_SetReadOnly(false);
    Path_Empty(Paths.CurrentFile, false); Settings.DocReadOnlyMode = false;
    Flags.bSettingsFileSoftLocked = false; Flags.bHugeFileLoadState = false; s_NeedSavingForced = false;
    Encoding_Current(Settings.DefaultEncoding);
    bool const result = _FileLoadSingle(resolved, flags | FLF_DontSave, position, line);
    Path_Release(resolved);
    if (result) _WorkspaceCapture();
    else {
        _WorkspaceReleaseDocument(doc); --workspace->count; _WorkspaceRestore(previous);
    }
    workspace->transitioning = false;
    _WorkspaceRefresh(); UpdateToolbar_Now(Globals.hwndMain);
    return result;
}

static void _WorkspaceCycle(int direction)
{
    DOCUMENT_WORKSPACE* workspace = _Workspace();
    if (workspace && workspace->count > 1) _WorkspaceSwitch((workspace->active + direction + workspace->count) % workspace->count);
}

static void _WorkspaceEdited()
{
    DOCUMENT_WORKSPACE* workspace = _Workspace();
    if (workspace && workspace->active >= 0 && workspace->active < workspace->count) ++workspace->documents[workspace->active].version;
}
static void _WorkspaceDocumentKey(WCHAR* key, size_t capacity)
{
    DOCUMENT_WORKSPACE* workspace = _Workspace();
    if (workspace) StringCchPrintfW(key, capacity, L"document-%llu", workspace->documents[workspace->active].id);
    else StringCchCopyW(key, capacity, Path_Get(Paths.CurrentFile));
}

static bool _WorkspaceOwnsPath(const HPATHL path)
{
    DOCUMENT_WORKSPACE* workspace = _Workspace();
    if (Path_IsEmpty(path)) return false;
    if (Path_StrgComparePath(path, Paths.CurrentFile, Paths.WorkingDirectory, true) == 0) return true;
    if (workspace) for (int i = 0; i < workspace->count; ++i) {
        if (Path_IsNotEmpty(workspace->documents[i].path) && Path_StrgComparePath(path, workspace->documents[i].path, Paths.WorkingDirectory, true) == 0) return true;
    }
    return false;
}
static void _WorkspaceAutoSaveAll(FileSaveFlags flags)
{
    DOCUMENT_WORKSPACE* workspace = _Workspace();
    if (!workspace) { AutoSaveDoWork(flags); return; }
    int const original = workspace->active;
    for (int i = 0; i < workspace->count; ++i) {
        if (_WorkspaceSwitch(i)) AutoSaveDoWork(flags);
    }
    _WorkspaceSwitch(original);
}

static UINT_PTR _WorkspaceActiveId()
{
    DOCUMENT_WORKSPACE* workspace = _Workspace();
    return workspace ? (UINT_PTR)workspace->documents[workspace->active].id : 0;
}
