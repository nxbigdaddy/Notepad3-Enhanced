// Native document ownership and activation. Included by Notepad3.c so the legacy
// save/monitoring flags stay behind one transition boundary during migration.
#define WORKSPACE_DOCUMENT_LIMIT 64
typedef struct SPLIT_VIEW_STATE {
    char* selection;
    DocLn* folds;
    size_t foldCount;
    DocPos anchor, caret;
    DocLn firstLine;
    int xOffset, zoom, wrapMode;
    bool valid;
} SPLIT_VIEW_STATE;
typedef struct WORKSPACE_DOCUMENT {
    sptr_t handle;
    unsigned long long id, version;
    HPATHL path;
    void* encryption;
    bool encryptedRecovery, recoveryConflict;
    char* selection;
    SPLIT_VIEW_STATE views[2];
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
    HWND tabs, closeButton, primaryView, splitView, splitGutter;
    int count, active;
    unsigned long long nextId;
    bool transitioning, splitDragging;
    int splitRatio, splitStartRatio;
    RECT splitArea;
    void* recovery;
    void* ai;
    void* batch;
    void* pdfTest;
    WORKSPACE_DOCUMENT documents[WORKSPACE_DOCUMENT_LIMIT];
} DOCUMENT_WORKSPACE;

static bool _WorkspaceEnsure(void);
static void _WorkspaceCapture(void);
static bool _FileLoadSingle(const HPATHL path, FileLoadFlags flags, DocPos position, DocLn line);
static DOCUMENT_WORKSPACE* _Workspace() { return (DOCUMENT_WORKSPACE*)Globals.workspace; }

static bool _SplitIsOpen(void) { DOCUMENT_WORKSPACE* workspace = _Workspace(); return workspace && workspace->splitView; }
static HWND _SplitPrimary(void) { DOCUMENT_WORKSPACE* workspace = _Workspace(); return workspace && workspace->primaryView ? workspace->primaryView : Globals.hwndEdit; }
static void _SplitActivate(HWND view)
{
    DOCUMENT_WORKSPACE* workspace = _Workspace();
    if (!workspace || !workspace->splitView || (view != workspace->primaryView && view != workspace->splitView) || view == Globals.hwndEdit) return;
    Globals.hwndEdit = view; InitScintillaHandle(view);
    UpdateToolbar();
}
static void _SplitCaptureView(HWND view, SPLIT_VIEW_STATE* state)
{
    DocPos length = Sci_ViewCall(view, SCI_GETSELECTIONSERIALIZED, 0, 0);
    char* selection = length > 0 ? AllocMem((size_t)length + 1, HEAP_ZERO_MEMORY) : NULL;
    if (selection) Sci_ViewCall(view, SCI_GETSELECTIONSERIALIZED, 0, (LPARAM)selection);
    FreeMem(state->selection); state->selection = selection;
    state->anchor = Sci_ViewCall(view, SCI_GETANCHOR, 0, 0);
    state->caret = Sci_ViewCall(view, SCI_GETCURRENTPOS, 0, 0);
    state->firstLine = Sci_ViewCall(view, SCI_GETFIRSTVISIBLELINE, 0, 0);
    state->xOffset = (int)Sci_ViewCall(view, SCI_GETXOFFSET, 0, 0);
    state->zoom = (int)Sci_ViewCall(view, SCI_GETZOOM, 0, 0);
    state->wrapMode = (int)Sci_ViewCall(view, SCI_GETWRAPMODE, 0, 0);
    size_t foldCount = 0;
    for (DocLn line = Sci_ViewCall(view, SCI_CONTRACTEDFOLDNEXT, 0, 0); line >= 0; line = Sci_ViewCall(view, SCI_CONTRACTEDFOLDNEXT, line + 1, 0)) ++foldCount;
    DocLn* folds = foldCount ? AllocMem(foldCount * sizeof(DocLn), HEAP_ZERO_MEMORY) : NULL;
    if (!foldCount || folds) {
        size_t index = 0;
        for (DocLn line = Sci_ViewCall(view, SCI_CONTRACTEDFOLDNEXT, 0, 0); line >= 0; line = Sci_ViewCall(view, SCI_CONTRACTEDFOLDNEXT, line + 1, 0)) folds[index++] = line;
        FreeMem(state->folds); state->folds = folds; state->foldCount = foldCount;
    }
    state->valid = true;
}
static void _SplitRestoreView(HWND view, const SPLIT_VIEW_STATE* state, const WORKSPACE_DOCUMENT* doc)
{
    const char* selection = state->valid ? state->selection : doc->selection;
    Sci_ViewCall(view, SCI_FOLDALL, SC_FOLDACTION_EXPAND, 0);
    const DocLn* folds = state->valid ? state->folds : doc->folds;
    size_t foldCount = state->valid ? state->foldCount : doc->foldCount;
    for (size_t i = 0; i < foldCount; ++i) {
        DocLn line = folds[i];
        if (line < Sci_ViewCall(view, SCI_GETLINECOUNT, 0, 0) && (Sci_ViewCall(view, SCI_GETFOLDLEVEL, line, 0) & SC_FOLDLEVELHEADERFLAG))
            Sci_ViewCall(view, SCI_FOLDLINE, line, SC_FOLDACTION_CONTRACT);
    }
    Sci_ViewCall(view, SCI_SETWRAPMODE, state->valid ? state->wrapMode : doc->wrapMode, 0);
    Sci_ViewCall(view, SCI_SETZOOM, state->valid ? state->zoom : doc->zoom, 0);
    if (selection) Sci_ViewCall(view, SCI_SETSELECTIONSERIALIZED, 0, (LPARAM)selection);
    else Sci_ViewCall(view, SCI_SETSEL, state->valid ? state->anchor : doc->anchor, state->valid ? state->caret : doc->caret);
    Sci_ViewCall(view, SCI_SETFIRSTVISIBLELINE, state->valid ? state->firstLine : doc->firstLine, 0);
    Sci_ViewCall(view, SCI_SETXOFFSET, state->valid ? state->xOffset : doc->xOffset, 0);
}
static void _SplitThemeState(bool capture)
{
    DOCUMENT_WORKSPACE* workspace = _Workspace();
    if (!workspace || workspace->transitioning || !workspace->splitView || workspace->active < 0 || workspace->active >= workspace->count) return;
    WORKSPACE_DOCUMENT* doc = &workspace->documents[workspace->active];
    if (Sci_ViewCall(workspace->primaryView, SCI_GETDOCPOINTER, 0, 0) != doc->handle || Sci_ViewCall(workspace->splitView, SCI_GETDOCPOINTER, 0, 0) != doc->handle) return;
    if (capture) {
        _SplitCaptureView(workspace->primaryView, &doc->views[0]);
        _SplitCaptureView(workspace->splitView, &doc->views[1]);
    } else {
        _SplitRestoreView(workspace->primaryView, &doc->views[0], doc);
        _SplitRestoreView(workspace->splitView, &doc->views[1], doc);
    }
}
static void _SplitSyncStyles(void)
{
    DOCUMENT_WORKSPACE* workspace = _Workspace();
    if (!workspace || !workspace->splitView) return;
    HWND other = Globals.hwndEdit == workspace->primaryView ? workspace->splitView : workspace->primaryView;
    const UINT styles[][2] = {
        {SCI_STYLEGETFORE,SCI_STYLESETFORE},{SCI_STYLEGETBACK,SCI_STYLESETBACK},
        {SCI_STYLEGETSIZEFRACTIONAL,SCI_STYLESETSIZEFRACTIONAL},{SCI_STYLEGETWEIGHT,SCI_STYLESETWEIGHT},
        {SCI_STYLEGETITALIC,SCI_STYLESETITALIC},{SCI_STYLEGETUNDERLINE,SCI_STYLESETUNDERLINE},
        {SCI_STYLEGETSTRIKE,SCI_STYLESETSTRIKE},{SCI_STYLEGETEOLFILLED,SCI_STYLESETEOLFILLED},
        {SCI_STYLEGETCASE,SCI_STYLESETCASE},{SCI_STYLEGETCHARACTERSET,SCI_STYLESETCHARACTERSET},
        {SCI_STYLEGETVISIBLE,SCI_STYLESETVISIBLE},{SCI_STYLEGETCHANGEABLE,SCI_STYLESETCHANGEABLE},
        {SCI_STYLEGETHOTSPOT,SCI_STYLESETHOTSPOT},{SCI_STYLEGETCHECKMONOSPACED,SCI_STYLESETCHECKMONOSPACED},
        {SCI_STYLEGETSTRETCH,SCI_STYLESETSTRETCH}
    };
    for (int style = 0; style < 256; ++style) {
        DocPos const length = SciCall(SCI_STYLEGETFONT,style,0);
        if (length>=0 && length<=4096) {
            char* font=AllocMem((size_t)length+1,HEAP_ZERO_MEMORY);
            if (font) { SciCall(SCI_STYLEGETFONT,style,(LPARAM)font); Sci_ViewCall(other,SCI_STYLESETFONT,style,(LPARAM)font); FreeMem(font); }
        }
        for (size_t i=0;i<COUNTOF(styles);++i) Sci_ViewCall(other,styles[i][1],style,SciCall(styles[i][0],style,0));
    }
    const UINT indicators[][2]={{SCI_INDICGETSTYLE,SCI_INDICSETSTYLE},{SCI_INDICGETFORE,SCI_INDICSETFORE},
        {SCI_INDICGETUNDER,SCI_INDICSETUNDER},{SCI_INDICGETHOVERSTYLE,SCI_INDICSETHOVERSTYLE},{SCI_INDICGETHOVERFORE,SCI_INDICSETHOVERFORE},
        {SCI_INDICGETFLAGS,SCI_INDICSETFLAGS},{SCI_INDICGETSTROKEWIDTH,SCI_INDICSETSTROKEWIDTH},
        {SCI_INDICGETALPHA,SCI_INDICSETALPHA},{SCI_INDICGETOUTLINEALPHA,SCI_INDICSETOUTLINEALPHA}};
    for (int indicator=0;indicator<=INDIC_MAX;++indicator)
        for (size_t i=0;i<COUNTOF(indicators);++i) Sci_ViewCall(other,indicators[i][1],indicator,SciCall(indicators[i][0],indicator,0));
    const UINT markers[][2]={{SCI_MARKERGETFORETRANSLUCENT,SCI_MARKERSETFORETRANSLUCENT},
        {SCI_MARKERGETBACKTRANSLUCENT,SCI_MARKERSETBACKTRANSLUCENT},{SCI_MARKERGETBACKSELECTEDTRANSLUCENT,SCI_MARKERSETBACKSELECTEDTRANSLUCENT},
        {SCI_MARKERGETALPHA,SCI_MARKERSETALPHA},{SCI_MARKERGETLAYER,SCI_MARKERSETLAYER},{SCI_MARKERGETSTROKEWIDTH,SCI_MARKERSETSTROKEWIDTH}};
    for (int marker=0;marker<=MARKER_MAX;++marker) {
        // Native shapes only: defining pixmap/image symbols would discard the target image without copying its pixels.
        sptr_t const symbol=SciCall(SCI_MARKERSYMBOLDEFINED,marker,0);
        if (symbol!=SC_MARK_PIXMAP && symbol!=SC_MARK_RGBAIMAGE) Sci_ViewCall(other,SCI_MARKERDEFINE,marker,symbol);
        for (size_t i=0;i<COUNTOF(markers);++i) Sci_ViewCall(other,markers[i][1],marker,SciCall(markers[i][0],marker,0));
    }
    for (int margin=0;margin<SciCall(SCI_GETMARGINS,0,0);++margin)
        Sci_ViewCall(other,SCI_SETMARGINBACKN,margin,SciCall(SCI_GETMARGINBACKN,margin,0));
    sptr_t const foldColor=SciCall(SCI_GETFOLDMARGINCOLOUR,0,0), foldHiColor=SciCall(SCI_GETFOLDMARGINHICOLOUR,0,0);
    Sci_ViewCall(other,SCI_SETFOLDMARGINCOLOUR,foldColor!=-1,foldColor==-1?0:foldColor);
    Sci_ViewCall(other,SCI_SETFOLDMARGINHICOLOUR,foldHiColor!=-1,foldHiColor==-1?0:foldHiColor);
    const int elements[]={SC_ELEMENT_LIST,SC_ELEMENT_LIST_BACK,SC_ELEMENT_LIST_SELECTED,SC_ELEMENT_LIST_SELECTED_BACK,
        SC_ELEMENT_SELECTION_TEXT,SC_ELEMENT_SELECTION_BACK,SC_ELEMENT_SELECTION_ADDITIONAL_TEXT,SC_ELEMENT_SELECTION_ADDITIONAL_BACK,
        SC_ELEMENT_SELECTION_SECONDARY_TEXT,SC_ELEMENT_SELECTION_SECONDARY_BACK,SC_ELEMENT_SELECTION_INACTIVE_TEXT,SC_ELEMENT_SELECTION_INACTIVE_BACK,
        SC_ELEMENT_SELECTION_INACTIVE_ADDITIONAL_TEXT,SC_ELEMENT_SELECTION_INACTIVE_ADDITIONAL_BACK,SC_ELEMENT_CARET,SC_ELEMENT_CARET_ADDITIONAL,
        SC_ELEMENT_CARET_LINE_BACK,SC_ELEMENT_WHITE_SPACE,SC_ELEMENT_WHITE_SPACE_BACK,SC_ELEMENT_HOT_SPOT_ACTIVE,SC_ELEMENT_HOT_SPOT_ACTIVE_BACK,
        SC_ELEMENT_FOLD_LINE,SC_ELEMENT_HIDDEN_LINE};
    for (size_t i=0;i<COUNTOF(elements);++i) {
        if (SciCall(SCI_GETELEMENTISSET,elements[i],0)) Sci_ViewCall(other,SCI_SETELEMENTCOLOUR,elements[i],SciCall(SCI_GETELEMENTCOLOUR,elements[i],0));
        else Sci_ViewCall(other,SCI_RESETELEMENTCOLOUR,elements[i],0);
    }
    const UINT properties[][2]={{SCI_GETSELECTIONLAYER,SCI_SETSELECTIONLAYER},{SCI_GETCARETLINELAYER,SCI_SETCARETLINELAYER},
        {SCI_GETCARETLINEFRAME,SCI_SETCARETLINEFRAME},{SCI_GETCARETLINEVISIBLE,SCI_SETCARETLINEVISIBLE},
        {SCI_GETCARETLINEVISIBLEALWAYS,SCI_SETCARETLINEVISIBLEALWAYS},{SCI_GETCARETLINEHIGHLIGHTSUBLINE,SCI_SETCARETLINEHIGHLIGHTSUBLINE}};
    for (size_t i=0;i<COUNTOF(properties);++i) Sci_ViewCall(other,properties[i][1],SciCall(properties[i][0],0,0),0);
}
static void _SplitStylesChanged(HWND view)
{
    DOCUMENT_WORKSPACE* workspace = _Workspace();
    if (view == Globals.hwndEdit && workspace && !workspace->transitioning && workspace->splitView &&
        Sci_ViewCall(workspace->primaryView, SCI_GETDOCPOINTER, 0, 0) == Sci_ViewCall(workspace->splitView, SCI_GETDOCPOINTER, 0, 0)) _SplitSyncStyles();
}
static void _SplitSync(void)
{
    DOCUMENT_WORKSPACE* workspace = _Workspace();
    if (!workspace || !workspace->splitView) return;
    HWND other = Globals.hwndEdit == workspace->primaryView ? workspace->splitView : workspace->primaryView;
    bool const changed = Sci_ViewCall(other, SCI_GETDOCPOINTER, 0, 0) != SciCall_GetDocPointer();
    if (changed)
        Sci_ViewCall(other, SCI_SETDOCPOINTER, 0, SciCall_GetDocPointer());
    Sci_ViewCall(other, SCI_SETREADONLY, SciCall_GetReadOnly(), 0);
    Sci_ViewCall(other, SCI_SETCODEPAGE, SciCall_GetCodePage(), 0);
    Sci_ViewCall(other, SCI_SETEOLMODE, SciCall_GetEOLMode(), 0);
    Sci_ViewCall(other, SCI_SETTABWIDTH, SciCall_GetTabWidth(), 0);
    Sci_ViewCall(other, SCI_SETINDENT, SciCall_GetIndent(), 0);
    if (!changed) return;
    WORKSPACE_DOCUMENT* doc = &workspace->documents[workspace->active];
    _SplitRestoreView(other, &doc->views[other == workspace->primaryView ? 0 : 1], doc);
    _SplitSyncStyles();
}
static void _SplitRelayout(void)
{
    RECT rect; GetClientRect(Globals.hwndMain, &rect);
    SendMessageW(Globals.hwndMain, WM_SIZE, 0, MAKELPARAM(rect.right, rect.bottom));
}
static LRESULT CALLBACK _SplitGutterProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR id, DWORD_PTR data)
{
    UNREFERENCED_PARAMETER(id); UNREFERENCED_PARAMETER(data);
    DOCUMENT_WORKSPACE* workspace = _Workspace();
    if (!workspace) return DefSubclassProc(window, message, wParam, lParam);
    switch (message) {
    case WM_SETCURSOR: SetCursor(LoadCursorW(NULL, IDC_SIZEWE)); return TRUE;
    case WM_LBUTTONDOWN:
        workspace->splitDragging = true; workspace->splitStartRatio = workspace->splitRatio;
        SetCapture(window); return 0;
    case WM_MOUSEMOVE:
        if (workspace->splitDragging && GetCapture() == window) {
            POINT point = POINTFromLParam(lParam); MapWindowPoints(window, Globals.hwndMain, &point, 1);
            int gap = MulDiv(6, GetDpiForWindow(Globals.hwndMain), 96);
            int usable = max_i(0, workspace->splitArea.right - workspace->splitArea.left - gap);
            int minimum = MulDiv(120, GetDpiForWindow(Globals.hwndMain), 96);
            if (usable >= 2 * minimum) {
                int left = clampi(point.x - workspace->splitArea.left, minimum, usable - minimum);
                workspace->splitRatio = MulDiv(left, 10000, usable); _SplitRelayout();
            }
        }
        return 0;
    case WM_LBUTTONUP:
        workspace->splitDragging = false; if (GetCapture() == window) ReleaseCapture(); return 0;
    case WM_CANCELMODE:
        if (workspace->splitDragging) {
            workspace->splitRatio = workspace->splitStartRatio; workspace->splitDragging = false;
            if (GetCapture() == window) ReleaseCapture(); _SplitRelayout();
        }
        return 0;
    case WM_CAPTURECHANGED: workspace->splitDragging = false; return 0;
    case WM_PAINT: {
        PAINTSTRUCT paint; HDC dc = BeginPaint(window, &paint);
        HBRUSH brush = CreateSolidBrush((COLORREF)SciCall(SCI_STYLEGETBACK, STYLE_DEFAULT, 0));
        FillRect(dc, &paint.rcPaint, brush); DeleteObject(brush); EndPaint(window, &paint); return 0;
    }
    case WM_NCDESTROY: RemoveWindowSubclass(window, _SplitGutterProc, 0); break;
    }
    return DefSubclassProc(window, message, wParam, lParam);
}
static void _SplitToggle(void)
{
    if (!_WorkspaceEnsure()) return;
    DOCUMENT_WORKSPACE* workspace = _Workspace();
    if (workspace->splitView) {
        _WorkspaceCapture();
        HWND closing = workspace->splitView;
        _SplitActivate(workspace->primaryView); workspace->splitView = NULL;
        DestroyWindow(workspace->splitGutter); workspace->splitGutter = NULL;
        DestroyWindow(closing); SetFocus(Globals.hwndEdit);
    } else {
        _WorkspaceCapture();
        workspace->primaryView = Globals.hwndEdit;
        HWND view = CreateWindowExW(WS_EX_CLIENTEDGE, L"Scintilla", NULL, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
            0, 0, 0, 0, Globals.hwndMain, (HMENU)IDC_EDITOR_SPLIT, Globals.hInstance, NULL);
        if (!view) return;
        workspace->splitGutter = CreateWindowExW(0, L"STATIC", NULL, WS_CHILD | WS_VISIBLE | SS_NOTIFY,
            0, 0, 0, 0, Globals.hwndMain, NULL, Globals.hInstance, NULL);
        if (!workspace->splitGutter) { DestroyWindow(view); return; }
        SetWindowSubclass(workspace->splitGutter, _SplitGutterProc, 0, 0);
        if (!workspace->splitRatio) workspace->splitRatio = 5000;
        workspace->splitView = view;
        Globals.hwndEdit = view; InitScintillaHandle(view); _InitializeSciEditCtrl(view);
        Globals.hwndEdit = workspace->primaryView; InitScintillaHandle(Globals.hwndEdit);
        _SplitSync();
    }
    RECT rect; GetClientRect(Globals.hwndMain, &rect);
    SendMessageW(Globals.hwndMain, WM_SIZE, 0, MAKELPARAM(rect.right, rect.bottom));
}
static int _SplitLayout(int x, int y, int width, int height)
{
    DOCUMENT_WORKSPACE* workspace = _Workspace();
    if (!workspace || !workspace->splitView) return 0;
    workspace->splitArea = (RECT){x, y, x + width, y + height};
    int gap = min_i(width, MulDiv(6, GetDpiForWindow(Globals.hwndMain), 96));
    int usable = max_i(0, width - gap), minimum = MulDiv(120, GetDpiForWindow(Globals.hwndMain), 96);
    int leftWidth = usable >= 2 * minimum ? clampi(MulDiv(usable, workspace->splitRatio, 10000), minimum, usable - minimum) : usable / 2;
    int rightWidth = usable - leftWidth;
    MoveWindow(workspace->splitGutter, x + leftWidth, y, gap, height, TRUE);
    MoveWindow(workspace->splitView, x + leftWidth + gap, y, rightWidth, height, TRUE);
    return rightWidth + gap;
}

static void _WorkspaceCapture()
{
    DOCUMENT_WORKSPACE* workspace = _Workspace();
    if (!workspace || workspace->active < 0 || workspace->active >= workspace->count) return;
    WORKSPACE_DOCUMENT* doc = &workspace->documents[workspace->active];
    if (workspace->splitView) {
        if (Sci_ViewCall(workspace->primaryView, SCI_GETDOCPOINTER, 0, 0) == doc->handle)
            _SplitCaptureView(workspace->primaryView, &doc->views[0]);
        if (Sci_ViewCall(workspace->splitView, SCI_GETDOCPOINTER, 0, 0) == doc->handle)
            _SplitCaptureView(workspace->splitView, &doc->views[1]);
    }
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
    _SplitSync();
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
    if (workspace->splitView) {
        _SplitSync();
        _SplitRestoreView(workspace->primaryView, &doc->views[0], doc);
        _SplitRestoreView(workspace->splitView, &doc->views[1], doc);
    }
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
    Encryption_FreeContext(doc->encryption); FreeMem(doc->selection); FreeMem(doc->views[0].selection); FreeMem(doc->views[1].selection); FreeMem(doc->views[0].folds); FreeMem(doc->views[1].folds); FreeMem(doc->folds); SecureZeroMemory(doc, sizeof(*doc));
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
    if (workspace->splitView) _SplitToggle();
    AiPanel_Destroy(workspace->ai); workspace->ai = NULL;
    BatchPanel_Destroy(workspace->batch); workspace->batch = NULL;
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
