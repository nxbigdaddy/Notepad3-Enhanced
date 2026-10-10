#include "SessionStoreC.h"

static bool _WorkspaceSessionAddDocument(NP3_SESSION session, NP3_SESSION_DOCUMENT* entry, const WORKSPACE_DOCUMENT* doc)
{
    uint64_t* folds[2] = {NULL,NULL};
    bool success = true;
    for (int i = 0; i < 2 && success; ++i) {
        const SPLIT_VIEW_STATE* state = &doc->views[i];
        NP3_SESSION_VIEW* view = &entry->views[i];
        view->valid = state->valid;
        if (!state->valid) continue;
        view->selection = state->selection; view->selectionLength = state->selection ? strlen(state->selection) : 0;
        view->anchor = state->anchor; view->caret = state->caret; view->firstLine = state->firstLine;
        view->xOffset = state->xOffset; view->zoom = state->zoom; view->wrapMode = state->wrapMode;
        folds[i] = state->foldCount ? AllocMem(state->foldCount * sizeof(uint64_t), HEAP_ZERO_MEMORY) : NULL;
        if (state->foldCount && !folds[i]) { success = false; break; }
        for (size_t j = 0; j < state->foldCount; ++j) folds[i][j] = (uint64_t)state->folds[j];
        view->folds = folds[i]; view->foldCount = state->foldCount;
    }
    if (success) success = Session_Add(session, entry) != 0;
    FreeMem(folds[0]); FreeMem(folds[1]); return success;
}
static bool _WorkspaceSessionCopyViews(WORKSPACE_DOCUMENT* doc, const NP3_SESSION_DOCUMENT* entry)
{
    for (int i = 0; i < 2; ++i) {
        const NP3_SESSION_VIEW* input = &entry->views[i];
        SPLIT_VIEW_STATE state = {0};
        if (!input->valid) continue;
        if (input->selectionLength) {
            state.selection = AllocMem(input->selectionLength + 1, HEAP_ZERO_MEMORY);
            if (!state.selection) return false;
            CopyMemory(state.selection, input->selection, input->selectionLength);
        }
        if (input->foldCount) {
            state.folds = AllocMem(input->foldCount * sizeof(DocLn), HEAP_ZERO_MEMORY);
            if (!state.folds) { FreeMem(state.selection); return false; }
            for (size_t j = 0; j < input->foldCount; ++j) state.folds[j] = (DocLn)input->folds[j];
        }
        state.foldCount = input->foldCount; state.anchor = (DocPos)input->anchor; state.caret = (DocPos)input->caret;
        state.firstLine = (DocLn)input->firstLine; state.xOffset = input->xOffset; state.zoom = input->zoom;
        state.wrapMode = input->wrapMode; state.valid = true;
        FreeMem(doc->views[i].selection); FreeMem(doc->views[i].folds); doc->views[i] = state;
    }
    return true;
}
static bool _WorkspaceSessionCaptureLayout(NP3_SESSION session)
{
    DOCUMENT_WORKSPACE* workspace = _Workspace();
    return Session_SetLayout(session, workspace->splitView != NULL, workspace->splitView && Globals.hwndEdit == workspace->splitView,
        workspace->splitRatio > 0 && workspace->splitRatio < 10000 ? workspace->splitRatio : 5000) != 0;
}
static void _WorkspaceSessionRestoreLayout(NP3_SESSION session, bool replace)
{
    DOCUMENT_WORKSPACE* workspace = _Workspace();
    uint32_t split = 0, active = 0, ratio = 5000;
    if (!Session_GetLayout(session, &split, &active, &ratio)) return;
    if (!split) { if (replace && workspace->splitView) _SplitToggle(); return; }
    workspace->splitRatio = (int)ratio;
    if (!workspace->splitView) _SplitToggle();
    if (!workspace->splitView) return;
    WORKSPACE_DOCUMENT* doc = &workspace->documents[workspace->active];
    _SplitRestoreView(workspace->primaryView, &doc->views[0], doc);
    _SplitRestoreView(workspace->splitView, &doc->views[1], doc);
    _SplitActivate(active ? workspace->splitView : workspace->primaryView);
    _SplitRelayout();
}


static HWND _WorkspaceSessionReader()
{
    return CreateWindowExW(0, L"Scintilla", L"", WS_CHILD, 0, 0, 0, 0,
        Globals.hwndMain, NULL, Globals.hInstance, NULL);
}
static NP3_SESSION _WorkspaceSessionCapture(uint32_t* error)
{
    DOCUMENT_WORKSPACE* workspace = _Workspace();
    if (!workspace || workspace->transitioning) { *error = ERROR_BUSY; return NULL; }
    _WorkspaceCapture();
    NP3_SESSION session = Session_Create();
    HWND reader = _WorkspaceSessionReader();
    bool success = session && reader;
    for (int i = 0; success && i < workspace->count; ++i) {
        WORKSPACE_DOCUMENT* doc = &workspace->documents[i];
        Sci_ViewCall(reader, SCI_SETDOCPOINTER, 0, doc->handle);
        DocPos const length = (DocPos)Sci_ViewCall(reader, SCI_GETLENGTH, 0, 0);
        if (length < 0 || length > 32 * 1024 * 1024) { success = false; break; }
        uint64_t* folds = doc->foldCount ? AllocMem(doc->foldCount * sizeof(uint64_t), HEAP_ZERO_MEMORY) : NULL;
        if (doc->foldCount && !folds) { success = false; break; }
        for (size_t j = 0; j < doc->foldCount; ++j) folds[j] = (uint64_t)doc->folds[j];
        NP3_SESSION_DOCUMENT entry = { 0 };
        entry.path = Path_Get(doc->path);
        entry.text = (const char*)Sci_ViewCall(reader, SCI_GETRANGEPOINTER, 0, length);
        entry.textLength = (size_t)length; entry.encoding = doc->encoding; entry.eol = doc->eol;
        entry.selection = doc->selection; entry.selectionLength = doc->selection ? strlen(doc->selection) : 0;
        entry.folds = folds; entry.foldCount = doc->foldCount;
        entry.anchor = doc->anchor; entry.caret = doc->caret; entry.firstLine = doc->firstLine;
        entry.xOffset = doc->xOffset; entry.zoom = doc->zoom; entry.codePage = doc->codePage;
        entry.tabWidth = doc->tabWidth; entry.indent = doc->indent; entry.wrapMode = doc->wrapMode;
        if (doc->modified) entry.flags |= NP3_SESSION_MODIFIED;
        if (doc->readOnly) entry.flags |= NP3_SESSION_READONLY;
        Encryption_RestoreContext(doc->encryption);
        if (IsEncryptionRequired() || doc->encryptedRecovery) entry.flags |= NP3_SESSION_ENCRYPTED_SOURCE;
        success = _WorkspaceSessionAddDocument(session, &entry, doc);
        FreeMem(folds);
    }
    // Snapshot reading never activates another editor view or changes its undo.
    Encryption_RestoreContext(workspace->documents[workspace->active].encryption);
    if (reader) DestroyWindow(reader);
    if (success) success = Session_SetActive(session, workspace->active) && _WorkspaceSessionCaptureLayout(session);
    else *error = ERROR_INVALID_DATA;
    if (!success) { Session_Free(session); return NULL; }
    return session;
}
static bool _WorkspaceSessionSave(const HPATHL path, uint32_t* error)
{
    NP3_SESSION session = _WorkspaceSessionCapture(error);
    if (!session) return false;
    bool const success = Session_Save(session, Path_Get(path), error) != 0;
    Session_Free(session);
    return success;
}
static bool _WorkspaceSessionLoad(const HPATHL path, uint32_t* error)
{
    DOCUMENT_WORKSPACE* workspace = _Workspace();
    if (!workspace || workspace->transitioning) { *error = ERROR_BUSY; return false; }
    NP3_SESSION session = NULL;
    if (!Session_Load(Path_Get(path), &session, error)) return false;
    int const count = (int)Session_Count(session);
    bool const replaceEmpty = workspace->count == 1 && Path_IsEmpty(Paths.CurrentFile) && SciCall_GetLength() == 0 && !IsSaveNeeded();
    int const offset = replaceEmpty ? 0 : workspace->count;
    if (count > WORKSPACE_DOCUMENT_LIMIT - offset) { Session_Free(session); *error = ERROR_TOO_MANY_OPEN_FILES; return false; }
    WORKSPACE_DOCUMENT* candidates = AllocMem(sizeof(WORKSPACE_DOCUMENT) * count, HEAP_ZERO_MEMORY);
    HWND reader = _WorkspaceSessionReader();
    bool success = candidates && reader;
    for (int i = 0; success && i < count; ++i) {
        NP3_SESSION_DOCUMENT entry = { 0 };
        success = Session_Get(session, i, &entry) && !(entry.flags & NP3_SESSION_DISK_BACKED) && Encoding_IsValid((cpi_enc_t)entry.encoding);
        if (!success) break;
        success = entry.codePage == 0 || entry.codePage == SC_CP_UTF8 || entry.codePage == 932 || entry.codePage == 936 || entry.codePage == 949 || entry.codePage == 950 || entry.codePage == 1361;
        if (!success) break;
        WORKSPACE_DOCUMENT* doc = &candidates[i];
        doc->path = Path_Allocate(entry.path); doc->encryption = Encryption_CreateContext();
        doc->handle = (sptr_t)Sci_ViewCall(reader, SCI_CREATEDOCUMENT, 0, SC_DOCUMENTOPTION_DEFAULT);
        if (!doc->path || !doc->encryption || !doc->handle) { success = false; break; }
        Sci_ViewCall(reader, SCI_SETDOCPOINTER, 0, doc->handle);
        Sci_ViewCall(reader, SCI_SETCODEPAGE, entry.codePage, 0);
        Sci_ViewCall(reader, SCI_SETUNDOCOLLECTION, FALSE, 0);
        Sci_ViewCall(reader, SCI_APPENDTEXT, entry.textLength, (LPARAM)entry.text);
        if ((size_t)Sci_ViewCall(reader, SCI_GETLENGTH, 0, 0) != entry.textLength || Sci_ViewCall(reader, SCI_GETSTATUS, 0, 0) != SC_STATUS_OK) { success = false; break; }
        Sci_ViewCall(reader, SCI_SETSAVEPOINT, 0, 0);
        Sci_ViewCall(reader, SCI_SETUNDOCOLLECTION, TRUE, 0);
        doc->encoding = (cpi_enc_t)entry.encoding; doc->eol = entry.eol; doc->codePage = entry.codePage;
        FileVars_GetFromData(entry.text, entry.textLength, &doc->vars);
        doc->tabWidth = entry.tabWidth; doc->indent = entry.indent; doc->wrapMode = entry.wrapMode;
        doc->vars.iTabWidth = doc->tabWidth; doc->vars.iIndentWidth = doc->indent; doc->vars.bWordWrap = doc->wrapMode != SC_WRAP_NONE;
        doc->anchor = (DocPos)entry.anchor; doc->caret = (DocPos)entry.caret;
        doc->firstLine = (DocLn)entry.firstLine; doc->xOffset = entry.xOffset; doc->zoom = entry.zoom;
        doc->encryptedRecovery = (entry.flags & NP3_SESSION_ENCRYPTED_SOURCE) != 0;
        doc->readOnly = (entry.flags & NP3_SESSION_READONLY) != 0 || doc->encryptedRecovery;
        doc->readOnlyMode = doc->readOnly;
        // Recovered text is authoritative until the user saves it, even if its
        // original file has disappeared or changed since the snapshot.
        doc->forcedDirty = true; doc->modified = true;
        doc->recoveryConflict = Path_IsNotEmpty(doc->path);
        doc->watchingMode = FWM_DONT_CARE;
        if (entry.selectionLength) {
            doc->selection = AllocMem(entry.selectionLength + 1, HEAP_ZERO_MEMORY);
            if (!doc->selection) { success = false; break; }
            CopyMemory(doc->selection, entry.selection, entry.selectionLength);
        }
        if (entry.foldCount) {
            doc->folds = AllocMem(entry.foldCount * sizeof(DocLn), HEAP_ZERO_MEMORY);
            if (!doc->folds) { success = false; break; }
            doc->foldCount = entry.foldCount;
            for (size_t j = 0; j < entry.foldCount; ++j) doc->folds[j] = (DocLn)entry.folds[j];
        }
        if (!_WorkspaceSessionCopyViews(doc, &entry)) { success = false; break; }
        if (Path_IsNotEmpty(doc->path)) Path_GetFileAttributesEx(doc->path, GetFileExInfoStandard, &doc->observedFile);
    }
    if (reader) DestroyWindow(reader);
    if (success) {
        _WorkspaceCapture(); AutoSaveStop(); InstallFileWatching(false);
        workspace->transitioning = true;
        if (FocusedView.HideNonMatchedLines) EditToggleView(Globals.hwndEdit);
        if (replaceEmpty) _WorkspaceReleaseDocument(&workspace->documents[0]);
        for (int i = 0; i < count; ++i) {
            candidates[i].id = ++workspace->nextId;
            workspace->documents[offset + i] = candidates[i];
            SecureZeroMemory(&candidates[i], sizeof(WORKSPACE_DOCUMENT));
        }
        workspace->count = offset + count;
        _WorkspaceRestore(offset + (int)Session_Active(session));
        workspace->transitioning = false;
        _WorkspaceRefresh(); UpdateToolbar_Now(Globals.hwndMain); UpdateMargins(true);
        _WorkspaceSessionRestoreLayout(session, replaceEmpty);
        SetFocus(Globals.hwndEdit); *error = ERROR_SUCCESS;
    } else {
        if (candidates) for (int i = 0; i < count; ++i) {
            if (candidates[i].handle) _WorkspaceReleaseDocument(&candidates[i]);
            else { Path_Release(candidates[i].path); Encryption_FreeContext(candidates[i].encryption); }
        }
        *error = ERROR_INVALID_DATA;
    }
    FreeMem(candidates); Session_Free(session); return success;
}
static bool _WorkspaceSessionWriteAllowed(const HPATHL destination)
{
    DOCUMENT_WORKSPACE* workspace = _Workspace();
    if (!workspace) return true;
    WORKSPACE_DOCUMENT* doc = &workspace->documents[workspace->active];
    if (doc->encryptedRecovery) {
        if (!GetFileKey(Globals.hwndEdit) || !IsEncryptionRequired()) return false;
        Encryption_CaptureContext(doc->encryption); doc->encryptedRecovery = false;
    }
    if (doc->recoveryConflict && Path_IsExistingFile(destination) && Path_StrgComparePath(destination, doc->path, Paths.WorkingDirectory, true) == 0) {
        return IsYesOkay(InfoBoxLng(MB_YESNO | MB_ICONWARNING, NULL, IDS_MUI_SESSION_OVERWRITE));
    }
    return true;
}
static void _WorkspaceSessionWriteDone(const HPATHL destination)
{
    DOCUMENT_WORKSPACE* workspace = _Workspace();
    if (workspace && Path_StrgComparePath(destination, Paths.CurrentFile, Paths.WorkingDirectory, true) == 0) workspace->documents[workspace->active].recoveryConflict = false;
}
static void _WorkspaceSessionCommand(bool save)
{
    HPATHL path = Path_Allocate(L"workspace.np3session");
    bool const selected = save ? SaveFileDlg(Globals.hwndMain, path, Paths.WorkingDirectory) : OpenFileDlg(Globals.hwndMain, path, Paths.WorkingDirectory);
    if (selected) {
        uint32_t error = 0;
        bool const success = save ? _WorkspaceSessionSave(path, &error) : _WorkspaceSessionLoad(path, &error);
        if (!success) InfoBoxLng(MB_ICONWARNING, NULL, IDS_MUI_SESSION_ERROR, error);
    }
    Path_Release(path);
}
