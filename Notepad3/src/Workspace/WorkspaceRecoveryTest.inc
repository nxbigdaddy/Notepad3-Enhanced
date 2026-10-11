static HPATHL _WorkspaceHistoryTestArchive(HPATHL directory,unsigned int index,unsigned int time,NP3_SESSION session)
{
    WCHAR leaf[64]; StringCchPrintfW(leaf,COUNTOF(leaf),L"np3-400000-%016llx.np3workspace",(unsigned long long)index);
    HPATHL path=Path_Copy(directory); if(!path || !Path_Append(path,leaf)) { Path_Release(path); return NULL; }
    uint32_t error=0; bool saved=false;
    if(session) saved=Session_Save(session,Path_Get(path),&error)!=0;
    else { HANDLE file=Path_CreateFile(path,GENERIC_WRITE,FILE_SHARE_READ,NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL); DWORD written=0; if(IS_VALID_HANDLE(file)) { saved=WriteFile(file,"corrupt",7,&written,NULL) && written==7; CloseHandle(file); } }
    HANDLE file=saved?Path_CreateFile(path,FILE_WRITE_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL):INVALID_HANDLE_VALUE;
    FILETIME stamp={time,0x01d00000}; bool stamped=IS_VALID_HANDLE(file) && SetFileTime(file,NULL,NULL,&stamp);
    if(IS_VALID_HANDLE(file)) CloseHandle(file);
    if(!stamped) { Path_Release(path); return NULL; } return path;
}
static bool _WorkspaceHistoryTestCopy(HPATHL source,HPATHL directory,LPCWSTR name)
{
    HPATHL copy=Path_Copy(directory); bool saved=source && copy && Path_Append(copy,name) && CopyFileW(Path_Get(source),Path_Get(copy),FALSE);
    Path_Release(copy); return saved;
}
// Only entered with an explicit mode and a marker-validated isolated directory.
static void _WorkspaceRecoveryTest()
{
    WCHAR mode[32];
    DWORD const length = GetEnvironmentVariableW(L"NP3_RECOVERY_TEST_MODE", mode, COUNTOF(mode));
    if (!length || length >= COUNTOF(mode)) return;
    DOCUMENT_WORKSPACE* workspace = _Workspace();
    if (!workspace || !workspace->recovery) return;
    WORKSPACE_RECOVERY* recovery = workspace->recovery;
    // A mode alone must never operate on the normal user's recovery directory.
    if (!GetEnvironmentVariableW(L"NP3_RECOVERY_TEST_DIR", NULL, 0)) return;
    HPATHL logPath = Path_Copy(recovery->directory); Path_Append(logPath, mode);
    Path_RenameExtension(logPath, L".log");
    WORKSPACE_TEST_RESULT result = { 0 };
    result.log = Path_CreateFile(logPath, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    Path_Release(logPath);
    if (!IS_VALID_HANDLE(result.log)) return;
    bool const seeded = wcscmp(mode, L"seed") == 0;
    bool const verified = wcscmp(mode, L"verify") == 0;
    bool const live = wcscmp(mode, L"live") == 0;
    bool const clean = wcscmp(mode, L"clean") == 0;
    bool const failure = wcscmp(mode, L"failure") == 0;
    bool const initFailure = wcscmp(mode,L"init-failure")==0;
    bool const history = wcscmp(mode,L"history")==0;
    bool const splitSeed = wcscmp(mode, L"split-seed") == 0;
    bool const splitVerify = wcscmp(mode, L"split-verify") == 0;
    bool const splitImport = wcscmp(mode, L"split-import") == 0;
    bool const normalSeed = wcscmp(mode, L"normal-seed") == 0;
    bool const normalVerify = wcscmp(mode, L"normal-verify") == 0;
    bool const normalExplicit = wcscmp(mode, L"normal-explicit") == 0;
    bool const normalMissing = wcscmp(mode, L"normal-missing") == 0;
    bool const normalDisabled = wcscmp(mode, L"normal-disabled") == 0;
    _WorkspaceTestCheck(&result, seeded || verified || live || clean || failure || initFailure || history || splitSeed || splitVerify || splitImport || normalSeed || normalVerify || normalExplicit || normalMissing || normalDisabled, "recognized recovery test mode");
    if(history) {
        _WorkspaceTestCheck(&result,_WorkspaceHistoryName(L"np3-4294967295-0123456789abcdef.np3workspace") && !_WorkspaceHistoryName(L"np3-4294967296-0123456789abcdef.np3workspace") && !_WorkspaceHistoryName(L"np3-0-0123456789abcdef.np3workspace") && !_WorkspaceHistoryName(L"np3-user-not-generated.np3workspace"),"history ownership requires exact generated filename and a valid nonzero DWORD process ID");
        HPATHL directory=Path_Copy(recovery->directory); Path_Append(directory,L"history"); Path_CreateDirectoryEx(directory);
        NP3_SESSION normal=Session_Create(),draft=Session_Create(); NP3_SESSION_DOCUMENT entry={0};
        entry.encoding=CPI_UTF8; entry.codePage=SC_CP_UTF8; entry.tabWidth=4; entry.indent=4; entry.path=L"";
        bool seeds=normal && draft && Session_Add(normal,&entry);
        entry.text="history draft"; entry.textLength=13; entry.flags=NP3_SESSION_MODIFIED; seeds=seeds && Session_Add(draft,&entry);
        HPATHL archives[16]={0};
        for(unsigned int i=0;i<16;++i) { archives[i]=_WorkspaceHistoryTestArchive(directory,i+1,i+1,normal); WCHAR copy[64]; StringCchPrintfW(copy,COUNTOF(copy),L"valid-%u-before.np3session",i+1); seeds=archives[i] && _WorkspaceHistoryTestCopy(archives[i],directory,copy) && seeds; }
        HPATHL active=_WorkspaceHistoryTestArchive(directory,17,0,normal),blocked=_WorkspaceHistoryTestArchive(directory,18,0,normal),corrupt=_WorkspaceHistoryTestArchive(directory,19,1000,NULL),dirty=_WorkspaceHistoryTestArchive(directory,20,1001,draft);
        HPATHL activeLock=Path_Copy(active); Path_RenameExtension(activeLock,L".lock"); HANDLE held=_WorkspaceHistoryClaim(activeLock);
        HANDLE blockedFile=blocked?Path_CreateFile(blocked,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL):INVALID_HANDLE_VALUE;
        HPATHL manual=Path_Copy(directory); Path_Append(manual,L"np3-user-not-generated.np3workspace");
        HPATHL crash=Path_Copy(directory); Path_Append(crash,L"np3-400000-0000000000000015.np3recovery"); uint32_t error=0;
        seeds=seeds && active && blocked && corrupt && dirty && IS_VALID_HANDLE(held) && IS_VALID_HANDLE(blockedFile) && Session_Save(normal,Path_Get(manual),&error) && Session_Save(draft,Path_Get(crash),&error);
        seeds=seeds && _WorkspaceHistoryTestCopy(corrupt,directory,L"corrupt-before.bin") && _WorkspaceHistoryTestCopy(dirty,directory,L"dirty-before.np3session") && _WorkspaceHistoryTestCopy(manual,directory,L"manual-before.np3session") && _WorkspaceHistoryTestCopy(crash,directory,L"crash-before.np3session");
        _WorkspaceTestCheck(&result,seeds,"history fixtures contain sixteen valid archives and independently copied protected records");
        HPATHL backup=Path_Copy(directory); Path_Append(backup,L"worker-test.np3recovery");
        HPATHL guardPath=Path_Copy(directory); Path_Append(guardPath,L"np3-history-cleanup.lock"); HANDLE guard=_WorkspaceHistoryClaim(guardPath);
        _WorkspaceTestCheck(&result,IS_VALID_HANDLE(guard) && _WorkspaceRecoveryPruneHistory(backup)==0 && Path_IsExistingFile(archives[0]) && Path_IsExistingFile(archives[15]),"history cleanup serialization lock prevents a second concurrent cleanup");
        if(IS_VALID_HANDLE(guard)) CloseHandle(guard);
        unsigned int removed=_WorkspaceRecoveryPruneHistory(backup); bool newest=true;
        for(int i=0;i<16;++i) newest=newest && Path_IsExistingFile(archives[i])==(i>=6);
        _WorkspaceTestCheck(&result,removed==6 && newest,"history pruning removes six oldest records and keeps newest ten valid archives");
        _WorkspaceTestCheck(&result,Path_IsExistingFile(active) && Path_IsExistingFile(blocked),"active owner and archive opened without delete sharing remain protected");
        _WorkspaceTestCheck(&result,Path_IsExistingFile(corrupt) && Path_IsExistingFile(dirty) && Path_IsExistingFile(manual) && Path_IsExistingFile(crash),"corrupt draft unmanaged-name and crash-recovery records survive normal history cleanup");
        for(unsigned int i=0;i<6;++i) { Path_Release(archives[i]); archives[i]=_WorkspaceHistoryTestArchive(directory,i+1,i+1,normal); }
        unsigned long long beforeVersion=_WorkspaceVersion(); int beforeUndo=SciCall_GetUndoCurrent(); sptr_t beforeDoc=SciCall_GetDocPointer();
        WORKSPACE_RECOVERY_JOB job={0}; job.session=_WorkspaceSessionCapture(&error); job.path=Path_Copy(backup); _WorkspaceRecoveryWorker(&job);
        _WorkspaceTestCheck(&result,job.success && job.historyRemoved==6,"successful actual recovery worker persists backup then prunes older history");
        NP3_SESSION snapshot=NULL;
        _WorkspaceTestCheck(&result,Session_Load(Path_Get(backup),&snapshot,&error) && Session_Count(snapshot)==(size_t)workspace->count,"worker history cleanup leaves freshly protected backup readable"); Session_Free(snapshot);
        _WorkspaceTestCheck(&result,_WorkspaceVersion()==beforeVersion && SciCall_GetUndoCurrent()==beforeUndo && SciCall_GetDocPointer()==beforeDoc,"background history cleanup leaves live editor version undo and document binding unchanged");
        Session_Free(job.session); Path_Release(job.path);
        if(IS_VALID_HANDLE(held)) CloseHandle(held); if(IS_VALID_HANDLE(blockedFile)) CloseHandle(blockedFile);
        _WorkspaceTestCheck(&result,_WorkspaceRecoveryPruneHistory(backup)==2 && !Path_IsExistingFile(active) && !Path_IsExistingFile(blocked),"released owners and file handles permit a later cleanup of obsolete records");
        _WorkspaceTestCheck(&result,_WorkspaceRecoveryPruneHistory(backup)==0,"repeated history cleanup is idempotent");
        for(int i=0;i<16;++i) Path_Release(archives[i]);
        Session_Free(normal); Session_Free(draft); Path_Release(directory); Path_Release(active); Path_Release(activeLock); Path_Release(blocked); Path_Release(corrupt); Path_Release(dirty); Path_Release(manual); Path_Release(crash); Path_Release(backup); Path_Release(guardPath);
    } else if(initFailure) {
        HPATHL blocked=Path_Copy(recovery->directory); Path_Append(blocked,L"blocked-directory");
        HANDLE blockFile=Path_CreateFile(blocked,GENERIC_WRITE,FILE_SHARE_READ,NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);
        DWORD written=0; if(IS_VALID_HANDLE(blockFile)) { WriteFile(blockFile,"directory fixture",17,&written,NULL); CloseHandle(blockFile); }
        _WorkspaceTestCheck(&result,written==17,"initialization failure fixture is an existing ordinary file");
        SciCall_SetReadOnly(false); SciCall_BeginUndoAction(); SciCall_SelectAll(); SciCall_ReplaceSel("important draft"); SciCall_EndUndoAction(); SciCall_SetSel(2,7);
        int const originalCount=workspace->count;
        sptr_t const originalDoc=SciCall_GetDocPointer(); unsigned long long originalId=workspace->documents[workspace->active].id,originalVersion=_WorkspaceVersion();
        int originalUndo=SciCall_GetUndoCurrent(); bool reopen=Settings2.ReopenWorkspace;
        Settings2.ReopenWorkspace=false; _WorkspaceRecoveryClose(); recovery=NULL;
        WORKSPACE_RECOVERY* attempted=_WorkspaceRecoveryInitialize(Path_Copy(blocked),true);
        _WorkspaceTestCheck(&result,!attempted && !workspace->recovery && workspace->recoveryStartError==ERROR_DIRECTORY && workspace->recoveryStartReports==1,"real directory initialization failure records one warning without installing incomplete service");
        if(attempted) { workspace->recovery=attempted; _WorkspaceRecoveryClose(); }
        attempted=_WorkspaceRecoveryInitialize(Path_Copy(blocked),true);
        _WorkspaceTestCheck(&result,!attempted && workspace->recoveryStartReports==1,"repeated identical initialization failure does not repeat warning");
        if(attempted) { workspace->recovery=attempted; _WorkspaceRecoveryClose(); }
        _WorkspaceTestCheck(&result,workspace->count==originalCount && SciCall_GetDocPointer()==originalDoc && workspace->documents[workspace->active].id==originalId && _WorkspaceVersion()==originalVersion && _WorkspaceTestText("important draft") && SciCall_GetAnchor()==2 && SciCall_GetCurrentPos()==7 && SciCall_GetUndoCurrent()==originalUndo && IsSaveNeeded(),"failed recovery initialization preserves draft identity version selection undo and dirty state");
        HPATHL manual=Path_Copy(blocked); Path_RenameExtension(manual,L".np3session"); uint32_t error=0;
        _WorkspaceTestCheck(&result,_WorkspaceSessionSave(manual,&error),"manual protected backup remains available while automatic initialization failed");
        NP3_SESSION saved=NULL; NP3_SESSION_DOCUMENT entry={0};
        bool savedDraft=Session_Load(Path_Get(manual),&saved,&error) && Session_Count(saved)==(size_t)originalCount && Session_Get(saved,Session_Active(saved),&entry) && entry.textLength==15 && !memcmp(entry.text,"important draft",15);
        _WorkspaceTestCheck(&result,savedDraft,"manual backup independently reads back full unsaved draft after initialization failure"); Session_Free(saved);
        _WorkspaceRecoveryStart(); recovery=workspace->recovery;
        _WorkspaceTestCheck(&result,recovery && workspace->recoveryStartError==0 && workspace->recoveryStartReports==1 && recovery->job && _WorkspaceRecoveryHarvest(recovery,10000),"successful real startup retry clears initialization error and persists automatic backup");
        _WorkspaceTestCheck(&result,_WorkspaceTestText("important draft") && _WorkspaceVersion()==originalVersion && SciCall_GetUndoCurrent()==originalUndo && IsSaveNeeded(),"successful recovery retry preserves pending edits and undo history");
        Settings2.ReopenWorkspace=reopen; Path_Release(blocked); Path_Release(manual);
    } else if (seeded) {
        _WorkspaceTestCheck(&result, workspace->count == 1 && SciCall_GetLength() == 0, "seed starts isolated");
        SciCall_AppendText(15, "unsaved alpha\r\n"); Encoding_Current(CPI_UTF8SIGN);
        HPATHL empty = Path_Allocate(NULL);
        _WorkspaceTestCheck(&result, FileLoad(empty, FLF_New | FLF_DontSave, 0, 0), "second unsaved document");
        Path_Release(empty);
        SciCall_AppendText(14, "unsaved beta\r\n"); Encoding_Current(CPI_UNICODE);
        _WorkspaceTestCheck(&result, _WorkspaceRecoveryHarvest(recovery, 10000), "initial background snapshot completed");
        _SplitToggle(); workspace->splitRatio = 3700; _SplitRelayout();
        Sci_ViewCall(workspace->primaryView, SCI_SETSEL, 1, 3); Sci_ViewCall(workspace->primaryView, SCI_SETZOOM, 2, 0);
        SendMessageW(workspace->splitView, WM_SETFOCUS, 0, 0); SciCall_SetSel(4, 7); SciCall_SetZoom(5);
        _WorkspaceRecoveryTimer(NULL, 0, 0, 0);
        _WorkspaceTestCheck(&result, recovery->job && _WorkspaceRecoveryHarvest(recovery, 10000) && Path_IsExistingFile(recovery->file), "two-document background snapshot persisted");
    } else if (verified) {
        _WorkspaceTestCheck(&result, workspace->count == 2, "crash restored both unsaved documents");
        _WorkspaceTestCheck(&result, workspace->splitView && workspace->active == 1 && Globals.hwndEdit == workspace->splitView && workspace->splitRatio == 3700, "crash restores split ratio and active right view across processes");
        _WorkspaceTestCheck(&result, workspace->splitView && Sci_ViewCall(workspace->primaryView, SCI_GETANCHOR, 0, 0) == 1 && Sci_ViewCall(workspace->primaryView, SCI_GETCURRENTPOS, 0, 0) == 3 && Sci_ViewCall(workspace->primaryView, SCI_GETZOOM, 0, 0) == 2 && SciCall_GetAnchor() == 4 && SciCall_GetCurrentPos() == 7 && SciCall_GetZoom() == 5, "crash restores independent split selections and zoom across processes");
        _WorkspaceTestCheck(&result, _WorkspaceSwitch(0) && _WorkspaceTestText("unsaved alpha\r\n"), "alpha content restored");
        _WorkspaceTestCheck(&result, Encoding_GetCurrent() == CPI_UTF8SIGN && IsSaveNeeded(), "alpha encoding and unsaved status restored");
        _WorkspaceTestCheck(&result, _WorkspaceSwitch(1) && _WorkspaceTestText("unsaved beta\r\n"), "beta content restored");
        _WorkspaceTestCheck(&result, Encoding_GetCurrent() == CPI_UNICODE && IsSaveNeeded(), "beta encoding and unsaved status restored");
        _WorkspaceTestCheck(&result, _WorkspaceRecoveryHarvest(recovery, 10000) && Path_IsExistingFile(recovery->file), "recovery transferred to current owner");
    } else if (splitSeed || splitVerify || splitImport) {
        HPATHL fixture = Path_Copy(recovery->directory); Path_Append(fixture, L"split-long.json");
        HPATHL snapshot = Path_Copy(recovery->directory); Path_Append(snapshot, L"split-state.np3session");
        uint32_t error = 0;
        if (splitSeed) {
            _WorkspaceTestCheck(&result, FileLoad(fixture, FLF_DontSave, 0, 0) && !IsSaveNeeded(), "split session long JSON loads from disk");
            if (!workspace->splitView) _SplitToggle();
            workspace->splitRatio = 4600; _SplitRelayout();
            _SplitActivate(workspace->primaryView); Sci_ColouriseAll();
            Sci_ViewCall(workspace->primaryView, SCI_FOLDLINE, 1, SC_FOLDACTION_CONTRACT);
            Sci_ViewCall(workspace->splitView, SCI_FOLDLINE, 1, SC_FOLDACTION_EXPAND);
            Sci_ViewCall(workspace->primaryView, SCI_FOLDLINE, 83, SC_FOLDACTION_EXPAND);
            Sci_ViewCall(workspace->splitView, SCI_FOLDLINE, 83, SC_FOLDACTION_CONTRACT);
            Sci_ViewCall(workspace->primaryView, SCI_SETWRAPMODE, SC_WRAP_NONE, 0); Sci_ViewCall(workspace->splitView, SCI_SETWRAPMODE, SC_WRAP_NONE, 0);
            Sci_ViewCall(workspace->primaryView, SCI_SETZOOM, 2, 0); Sci_ViewCall(workspace->splitView, SCI_SETZOOM, 5, 0);
            Sci_ViewCall(workspace->primaryView, SCI_SETSEL, 1, 3); Sci_ViewCall(workspace->splitView, SCI_SETSEL, 4, 7);
            Sci_ViewCall(workspace->primaryView, SCI_SETFIRSTVISIBLELINE, 10, 0); Sci_ViewCall(workspace->splitView, SCI_SETFIRSTVISIBLELINE, 30, 0);
            _WorkspaceTestCheck(&result, _WorkspaceSessionSave(snapshot, &error), "manual split snapshot writes protected long document");
        } else if (splitImport) {
            _WorkspaceTestCheck(&result, _WorkspaceSessionLoad(snapshot, &error) && workspace->count == 2 && IsSaveNeeded(), "manual import restores long snapshot as independent dirty document");
        }
        _WorkspaceTestCheck(&result, workspace->splitView && Globals.hwndEdit == workspace->primaryView && workspace->splitRatio == 4600, "long split session restores active primary and ratio");
        _WorkspaceTestCheck(&result, workspace->splitView && Sci_ViewCall(workspace->primaryView, SCI_GETFIRSTVISIBLELINE, 0, 0) == 10 && Sci_ViewCall(workspace->splitView, SCI_GETFIRSTVISIBLELINE, 0, 0) == 30, "long split session preserves independent vertical scroll");
        _WorkspaceTestCheck(&result, workspace->splitView && !Sci_ViewCall(workspace->primaryView, SCI_GETFOLDEXPANDED, 1, 0) && Sci_ViewCall(workspace->splitView, SCI_GETFOLDEXPANDED, 1, 0) && Sci_ViewCall(workspace->primaryView, SCI_GETFOLDEXPANDED, 83, 0) && !Sci_ViewCall(workspace->splitView, SCI_GETFOLDEXPANDED, 83, 0), "long split session preserves opposite folds in each view");
        _WorkspaceTestCheck(&result, workspace->splitView && Sci_ViewCall(workspace->primaryView, SCI_GETANCHOR, 0, 0) == 1 && Sci_ViewCall(workspace->primaryView, SCI_GETCURRENTPOS, 0, 0) == 3 && Sci_ViewCall(workspace->primaryView, SCI_GETZOOM, 0, 0) == 2 && Sci_ViewCall(workspace->splitView, SCI_GETANCHOR, 0, 0) == 4 && Sci_ViewCall(workspace->splitView, SCI_GETCURRENTPOS, 0, 0) == 7 && Sci_ViewCall(workspace->splitView, SCI_GETZOOM, 0, 0) == 5, "long split session preserves selections and zoom");
        if (!splitSeed && !splitImport) _WorkspaceTestCheck(&result, !IsSaveNeeded(), "normal long split reopening retains saved disk state");
        Path_Release(snapshot); Path_Release(fixture);
    } else if (normalSeed) {
        HPATHL a = Path_Copy(recovery->directory); Path_Append(a, L"normal-a.txt");
        HPATHL b = Path_Copy(recovery->directory); Path_Append(b, L"normal-b.txt");
        _WorkspaceTestCheck(&result, FileLoad(a, FLF_DontSave, 0, 0), "open normal-session A");
        Encoding_Current(CPI_UTF8SIGN); SciCall_AppendText(5, "SAVED");
        _WorkspaceTestCheck(&result, FileSave(FSF_None), "save normal-session A change");
        SciCall_SetZoom(3); SciCall_SetSel(1, 4); _WorkspaceCapture();
        _WorkspaceTestCheck(&result, FileLoad(b, FLF_DontSave, 0, 0), "open normal-session B");
        SciCall_AppendText(7, "DISCARD");
        HPATHL empty = Path_Allocate(NULL);
        _WorkspaceTestCheck(&result, FileLoad(empty, FLF_New | FLF_DontSave, 0, 0), "new discarded untitled document");
        SciCall_AppendText(17, "UNTITLED DISCARD!"); Path_Release(empty);
        _WorkspaceSwitch(0);
        if (!workspace->splitView) _SplitToggle(); workspace->splitRatio = 4100; _SplitRelayout();
        Sci_ViewCall(workspace->primaryView, SCI_SETSEL, 1, 4); Sci_ViewCall(workspace->primaryView, SCI_SETZOOM, 3, 0);
        SendMessageW(workspace->splitView, WM_SETFOCUS, 0, 0); SciCall_SetSel(2, 5); SciCall_SetZoom(6);
        uint32_t error = 0; NP3_SESSION normal = NULL;
        _WorkspaceTestCheck(&result, _WorkspaceNormalSave(recovery->normalPath, &error) && Session_Load(Path_Get(recovery->normalPath), &normal, &error), "normal metadata persisted");
        _WorkspaceTestCheck(&result, Session_Count(normal) == 2 && Session_Active(normal) == 0, "normal archive excludes discarded untitled tab");
        NP3_SESSION_DOCUMENT entry = { 0 }; Session_Get(normal, 1, &entry);
        _WorkspaceTestCheck(&result, entry.textLength == 0 && (entry.flags & NP3_SESSION_DISK_BACKED), "discarded named buffer text absent from archive");
        _WorkspaceTestCheck(&result, !_WorkspaceSessionLoad(recovery->normalPath, &error) && workspace->count == 3 && _WorkspaceTestText("alpha\r\nSAVED"), "metadata archive cannot import as empty recovered buffers");
        Session_Free(normal); Path_Release(a); Path_Release(b);
    } else if (normalDisabled) {
        _WorkspaceTestCheck(&result, !Settings2.ReopenWorkspace, "reopen setting loaded from isolated INI");
        _WorkspaceTestCheck(&result, workspace->count == 1 && SciCall_GetLength() == 0, "disabled reopening leaves empty workspace despite earlier archive");
        _WorkspaceTestCheck(&result, workspace->recovery != NULL, "disabled normal reopening retains crash backup service");
    } else if (normalMissing) {
        _WorkspaceTestCheck(&result, workspace->count == 2 && recovery->errorReports > 0, "missing normal-session file reported while available files reopen");
        _WorkspaceTestCheck(&result, _WorkspaceSwitch(1) && _WorkspaceTestText("x") && !IsSaveNeeded(), "externally shortened disk file loaded without stale buffer text");
        _WorkspaceTestCheck(&result, SciCall_GetCurrentPos() <= 1 && SciCall_GetAnchor() <= 1 && SciCall_GetZoom() == 6, "selection clamped to shortened document");
    } else if (normalVerify || normalExplicit) {
        int const offset = normalExplicit ? 1 : 0;
        _WorkspaceTestCheck(&result, workspace->count == 2 + offset, "normal session tab count restored");
        if (!normalExplicit) {
            _WorkspaceTestCheck(&result, workspace->splitView && Globals.hwndEdit == workspace->splitView && workspace->splitRatio == 4100, "normal restart restores split ratio and active right side");
            _WorkspaceTestCheck(&result, workspace->splitView && Sci_ViewCall(workspace->primaryView, SCI_GETANCHOR, 0, 0) == 1 && Sci_ViewCall(workspace->primaryView, SCI_GETCURRENTPOS, 0, 0) == 4 && Sci_ViewCall(workspace->primaryView, SCI_GETZOOM, 0, 0) == 3 && SciCall_GetAnchor() == 2 && SciCall_GetCurrentPos() == 5 && SciCall_GetZoom() == 6, "normal restart restores independent split selections and zoom");
        } else _WorkspaceTestCheck(&result, !workspace->splitView, "explicit incoming document retains its original single-view layout");
        if (normalExplicit) _WorkspaceTestCheck(&result, workspace->active == 0 && _WorkspaceTestText("incoming\r\n"), "explicit incoming document stays active");
        else _WorkspaceTestCheck(&result, workspace->active == 0, "normal active tab restored");
        _WorkspaceTestCheck(&result, _WorkspaceSwitch(offset) && _WorkspaceTestText("alpha\r\nSAVED") && !IsSaveNeeded(), "saved A reopens from disk without dirty state");
        _WorkspaceTestCheck(&result, Encoding_GetCurrent() == CPI_UTF8SIGN && SciCall_GetZoom() == 6 && SciCall_GetAnchor() == 2 && SciCall_GetCurrentPos() == 5, "normal A encoding and active view restored");
        _WorkspaceTestCheck(&result, _WorkspaceSwitch(offset + 1) && _WorkspaceTestText("beta\r\n") && !IsSaveNeeded(), "discarded B text not resurrected");
    } else if (failure) {
        _WorkspaceTestCheck(&result, recovery->errorReports > 0, "corrupt orphan recovery reported");
        _WorkspaceTestCheck(&result, _WorkspaceRecoveryHarvest(recovery, 10000), "initial backup completed");
        HANDLE locked = Path_CreateFile(recovery->file, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        _WorkspaceTestCheck(&result, IS_VALID_HANDLE(locked), "lock backup against replacement");
        unsigned int const before = recovery->errorReports;
        _WorkspaceRecoveryTimer(NULL, 0, 0, 0);
        _WorkspaceTestCheck(&result, _WorkspaceRecoveryHarvest(recovery, 10000) && recovery->lastError != 0 && recovery->errorReports == before + 1, "background write error reported once");
        _WorkspaceRecoveryTimer(NULL, 0, 0, 0);
        _WorkspaceTestCheck(&result, _WorkspaceRecoveryHarvest(recovery, 10000) && recovery->errorReports == before + 1, "repeated write error does not repeat warning");
        if (IS_VALID_HANDLE(locked)) CloseHandle(locked);
        NP3_SESSION prior = NULL; uint32_t error = 0;
        _WorkspaceTestCheck(&result, Session_Load(Path_Get(recovery->file), &prior, &error) != 0, "failed write retains valid earlier snapshot");
        Session_Free(prior);
        _WorkspaceRecoveryTimer(NULL, 0, 0, 0);
        _WorkspaceTestCheck(&result, _WorkspaceRecoveryHarvest(recovery, 10000) && recovery->lastError == 0, "successful retry clears failure state");
    } else if (live || clean) {
        _WorkspaceTestCheck(&result, workspace->count == 1 && SciCall_GetLength() == 0, live ? "live owner backup not imported" : "normal discard not resurrected");
    }
    char summary[128]; DWORD written;
    StringCchPrintfA(summary, COUNTOF(summary), "RESULT checks=%d failures=%d\r\n", result.checks, result.failures);
    WriteFile(result.log, summary, (DWORD)strlen(summary), &written, NULL); FlushFileBuffers(result.log); CloseHandle(result.log);
    if (seeded && !result.failures) {
        HPATHL ready = Path_Copy(recovery->directory); Path_Append(ready, L"seed.ready");
        HANDLE marker = Path_CreateFile(ready, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (IS_VALID_HANDLE(marker)) CloseHandle(marker);
        Path_Release(ready);
        return; // runner terminates only its own seed process to simulate a crash
    }
    _WorkspaceRecoveryClose();
    _WorkspaceDispose(); DestroyWindow(Globals.hwndMain);
}
