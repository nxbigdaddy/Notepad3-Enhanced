static void* _WorkspaceBatch(void) { DOCUMENT_WORKSPACE* w = _Workspace(); return w ? w->batch : NULL; }
static int _BatchLayout(int x, int y, int width, int height) { return BatchPanel_Layout(_WorkspaceBatch(), x, y, width, height); }
static void _BatchToggle(void)
{
    if (!_WorkspaceEnsure()) return;
    DOCUMENT_WORKSPACE* w = _Workspace();
    if (!w->batch) w->batch = BatchPanel_Create();
    WCHAR strings[BP_COUNT][256]; const WCHAR* labels[BP_COUNT];
    for (int i = 0; i < BP_COUNT; ++i) { GetLngString(IDS_MUI_BATCH_REPLACE + i, strings[i], COUNTOF(strings[i])); labels[i] = strings[i]; }
    BatchPanel_Toggle(w->batch, Globals.hwndMain, labels);
}
static bool _BatchRun(void)
{
    DOCUMENT_WORKSPACE* w = _Workspace();
    if (!w || BatchPanel_IsBusy(w->batch)) return false;
    _WorkspaceCapture();
    HWND reader = _WorkspaceSessionReader(); if (!reader) return false;
    BATCH_INPUT inputs[WORKSPACE_DOCUMENT_LIMIT] = {0};
    size_t total = 0; bool success = true;
    for (int i = 0; i < w->count; ++i) {
        WORKSPACE_DOCUMENT* doc = &w->documents[i];
        Sci_ViewCall(reader, SCI_SETDOCPOINTER, 0, doc->handle);
        DocPos length = Sci_ViewCall(reader, SCI_GETLENGTH, 0, 0);
        if (length < 0 || length > 8 * 1024 * 1024 || (size_t)length > 32 * 1024 * 1024 - total) { success = false; break; }
        inputs[i].id = doc->id; inputs[i].version = doc->version;
        inputs[i].label = Path_IsNotEmpty(doc->path) ? Path_Get(doc->path) : L"(untitled)";
        inputs[i].length = (size_t)length; inputs[i].readOnly = doc->readOnly;
        inputs[i].eol = doc->eol == SC_EOL_CRLF ? "\r\n" : doc->eol == SC_EOL_CR ? "\r" : "\n";
        const char* text = (const char*)Sci_ViewCall(reader, SCI_GETRANGEPOINTER, 0, length);
        char* copy = AllocMem((size_t)length + 1, HEAP_ZERO_MEMORY);
        if (!copy || (length && !text)) { FreeMem(copy); success = false; break; }
        if (length) memcpy(copy, text, (size_t)length);
        inputs[i].text = copy; total += (size_t)length;
    }
    if (success) success = BatchPanel_Run(w->batch, inputs, (size_t)w->count);
    for (int i = 0; i < w->count; ++i) FreeMem((void*)inputs[i].text);
    DestroyWindow(reader); return success;
}
static bool _BatchTextMatches(HWND reader, const char* text, size_t length)
{
    if ((size_t)Sci_ViewCall(reader, SCI_GETLENGTH, 0, 0) != length) return false;
    const char* actual = (const char*)Sci_ViewCall(reader, SCI_GETRANGEPOINTER, 0, (LPARAM)length);
    return !length || (actual && text && !memcmp(actual, text, length));
}
// failureAfter is used only by the isolated development self-test, never by UI messages.
static LRESULT _BatchApplyTransaction(const BATCH_APPLY* request, size_t failureAfter)
{
    DOCUMENT_WORKSPACE* w = _Workspace();
    if (!w || !request || !request->changes || !request->count || request->count > WORKSPACE_DOCUMENT_LIMIT || w->transitioning) return 0;
    _WorkspaceCapture();
    HWND reader = _WorkspaceSessionReader(); if (!reader) return -1;
    int indices[WORKSPACE_DOCUMENT_LIMIT] = {0}; size_t total = 0, inputTotal = 0;
    bool valid = true;
    for (size_t i = 0; i < request->count && valid; ++i) {
        const BATCH_CHANGE* change = &request->changes[i]; indices[i] = -1;
        if ((!change->original && change->originalLength) || (!change->result && change->resultLength) || change->originalLength > 8 * 1024 * 1024 || change->originalLength > 32 * 1024 * 1024 - inputTotal || change->resultLength > 32 * 1024 * 1024 - total) { valid = false; break; }
        total += change->resultLength;
        inputTotal += change->originalLength;
        if (change->resultLength && (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, change->result, (int)change->resultLength, NULL, 0) || memchr(change->result, 0, change->resultLength))) { valid = false; break; }
        for (size_t j = 0; j < i; ++j) if (request->changes[j].id == change->id) valid = false;
        for (int j = 0; j < w->count; ++j) if (w->documents[j].id == change->id) { indices[i] = j; break; }
        if (indices[i] < 0) { valid = false; break; }
        WORKSPACE_DOCUMENT* doc = &w->documents[indices[i]];
        if (doc->version != change->version || doc->readOnly) { valid = false; break; }
        Sci_ViewCall(reader, SCI_SETDOCPOINTER, 0, doc->handle);
        if (!_BatchTextMatches(reader, change->original, change->originalLength)) valid = false;
    }
    DestroyWindow(reader);
    if (!valid) return 0;
    int original = w->active; size_t applied = 0; bool success = true;
    for (size_t i = 0; i < request->count; ++i) {
        const BATCH_CHANGE* change = &request->changes[i];
        if (!_WorkspaceSwitch(indices[i]) || SciCall_GetReadOnly() || w->documents[indices[i]].version != change->version || !_BatchTextMatches(Globals.hwndEdit, change->original, change->originalLength)) { success = false; break; }
        SciCall_SetStatus(SC_STATUS_OK);
        UndoTransActionBegin();
        SciCall_SetTargetRange(0, SciCall_GetLength());
        Sci_ReplaceTargetTestChgHist((DocPos)change->resultLength, change->result ? change->result : "");
        EndUndoTransAction();
        bool changed = !_BatchTextMatches(Globals.hwndEdit, change->original, change->originalLength);
        if (changed) applied = i + 1;
        if (SciCall_GetStatus() != SC_STATUS_OK || !_BatchTextMatches(Globals.hwndEdit, change->result, change->resultLength)) { success = false; break; }
        if (failureAfter && failureAfter == i + 1) { success = false; break; }
    }
    bool recovered = true;
    if (!success) {
        while (applied) {
            --applied;
            const BATCH_CHANGE* change = &request->changes[applied];
            if (_WorkspaceSwitch(indices[applied]) && !_BatchTextMatches(Globals.hwndEdit, change->original, change->originalLength) && SciCall_CanUndo()) SciCall_Undo();
            if (!_BatchTextMatches(Globals.hwndEdit, change->original, change->originalLength)) recovered = false;
        }
    }
    _WorkspaceSwitch(original); _WorkspaceCapture(); _WorkspaceRefresh();
    return success ? 1 : recovered ? -1 : -2;
}
static LRESULT _BatchApply(const BATCH_APPLY* request) { return _BatchApplyTransaction(request, 0); }
