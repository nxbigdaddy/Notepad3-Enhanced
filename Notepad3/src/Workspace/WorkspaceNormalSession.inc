// Normal sessions contain paths and view state, never discarded buffer text.
static bool _WorkspaceNormalSave(const HPATHL path, uint32_t* error)
{
    DOCUMENT_WORKSPACE* workspace = _Workspace();
    _WorkspaceCapture();
    NP3_SESSION session = Session_Create();
    if (!session) { *error = ERROR_NOT_ENOUGH_MEMORY; return false; }
    bool success = true;
    uint32_t active = 0;
    for (int i = 0; success && i < workspace->count; ++i) {
        WORKSPACE_DOCUMENT* doc = &workspace->documents[i];
        if (Path_IsEmpty(doc->path)) continue;
        uint64_t* folds = doc->foldCount ? AllocMem(doc->foldCount * sizeof(uint64_t), HEAP_ZERO_MEMORY) : NULL;
        if (doc->foldCount && !folds) { success = false; break; }
        for (size_t j = 0; j < doc->foldCount; ++j) folds[j] = (uint64_t)doc->folds[j];
        NP3_SESSION_DOCUMENT entry = { 0 };
        entry.path = Path_Get(doc->path); entry.flags = NP3_SESSION_DISK_BACKED;
        entry.encoding = doc->encoding; entry.eol = doc->eol; entry.codePage = doc->codePage;
        entry.anchor = doc->anchor; entry.caret = doc->caret; entry.firstLine = doc->firstLine;
        entry.selection = doc->selection; entry.selectionLength = doc->selection ? strlen(doc->selection) : 0;
        entry.folds = folds; entry.foldCount = doc->foldCount;
        entry.xOffset = doc->xOffset; entry.zoom = doc->zoom;
        entry.tabWidth = doc->tabWidth; entry.indent = doc->indent; entry.wrapMode = doc->wrapMode;
        if (doc->readOnly) entry.flags |= NP3_SESSION_READONLY;
        if (i == workspace->active) active = (uint32_t)Session_Count(session);
        success = Session_Add(session, &entry) != 0;
        FreeMem(folds);
    }
    // An explicit empty session supersedes an earlier workspace on next launch.
    if (success && !Session_Count(session)) {
        NP3_SESSION_DOCUMENT empty = { 0 }; empty.encoding = CPI_UTF8;
        empty.codePage = SC_CP_UTF8; empty.tabWidth = 4; empty.indent = 4;
        success = Session_Add(session, &empty) != 0;
    }
    if (success) success = Session_SetActive(session, active) && Session_Save(session, Path_Get(path), error);
    else *error = ERROR_INVALID_DATA;
    Session_Free(session); return success;
}
static bool _WorkspaceNormalLoad(const HPATHL path, uint32_t* error)
{
    NP3_SESSION session = NULL;
    if (!Session_Load(Path_Get(path), &session, error)) return false;
    // Validate the entire archive before opening any file.
    for (size_t i = 0; i < Session_Count(session); ++i) {
        NP3_SESSION_DOCUMENT entry = { 0 }; Session_Get(session, i, &entry);
        bool const empty = !entry.path[0] && !entry.textLength && !entry.flags;
        if (!empty && !(entry.flags & NP3_SESSION_DISK_BACKED)) {
            Session_Free(session); *error = ERROR_INVALID_DATA; return false;
        }
    }
    DOCUMENT_WORKSPACE* workspace = _Workspace();
    int const original = workspace->active;
    bool const explicitDocument = Path_IsNotEmpty(Paths.CurrentFile) || SciCall_GetLength() != 0 || IsSaveNeeded();
    int selected = -1;
    bool success = true;
    for (size_t i = 0; i < Session_Count(session); ++i) {
        NP3_SESSION_DOCUMENT entry = { 0 }; Session_Get(session, i, &entry);
        if (!(entry.flags & NP3_SESSION_DISK_BACKED)) continue;
        HPATHL file = Path_Allocate(entry.path);
        bool existing = false;
        for (int j = 0; j < workspace->count; ++j)
            if (Path_IsNotEmpty(workspace->documents[j].path) && Path_StrgComparePath(file, workspace->documents[j].path, Paths.WorkingDirectory, true) == 0) { existing = true; break; }
        // Preserve explicit incoming documents and avoid applying stale view state.
        if (existing) { Path_Release(file); continue; }
        if (!Path_IsExistingFile(file) || !FileLoad(file, FLF_DontSave, 0, 0)) {
            Path_Release(file); *error = ERROR_FILE_NOT_FOUND; success = false; continue;
        }
        Path_Release(file);
        DocPos const length = SciCall_GetLength();
        SciCall_SetSel((DocPos)(entry.anchor < (uint64_t)length ? entry.anchor : (uint64_t)length), (DocPos)(entry.caret < (uint64_t)length ? entry.caret : (uint64_t)length));
        // Scintilla clamps serialized selections to the freshly loaded document.
        if (entry.selectionLength) SciCall_SetSelectionSerialized(entry.selection);
        SciCall_SetZoom(entry.zoom); SciCall_SetTabWidth(entry.tabWidth); SciCall_SetIndent(entry.indent); SciCall_SetWrapMode(entry.wrapMode);
        Sci_ColouriseAll();
        for (size_t j = 0; j < entry.foldCount; ++j)
            if (entry.folds[j] < (uint64_t)SciCall_GetLineCount() && (SciCall_GetFoldLevel((DocLn)entry.folds[j]) & SC_FOLDLEVELHEADERFLAG)) SciCall_FoldLine((DocLn)entry.folds[j], SC_FOLDACTION_CONTRACT);
        SciCall_SetFirstVisibleLine((DocLn)(entry.firstLine < (uint64_t)SciCall_GetLineCount() ? entry.firstLine : (uint64_t)(SciCall_GetLineCount() - 1)));
        SciCall_SetXOffset(entry.xOffset);
        if (entry.flags & NP3_SESSION_READONLY) { SciCall_SetReadOnly(true); Settings.DocReadOnlyMode = true; }
        _WorkspaceCapture(); _WorkspaceRefresh();
        if (i == Session_Active(session)) selected = workspace->active;
    }
    if (explicitDocument) _WorkspaceSwitch(original);
    else if (selected >= 0) _WorkspaceSwitch(selected);
    Session_Free(session); return success;
}
