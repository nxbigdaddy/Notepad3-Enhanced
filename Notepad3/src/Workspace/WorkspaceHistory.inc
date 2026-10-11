// Normal-exit metadata retention. Called off the UI thread after a successful backup.
#define WORKSPACE_HISTORY_LIMIT 10

typedef struct WORKSPACE_HISTORY_ENTRY { WCHAR name[64]; FILETIME stamp; } WORKSPACE_HISTORY_ENTRY;
static bool _WorkspaceHistoryName(const WCHAR* name)
{
    if(wcsncmp(name,L"np3-",4)!=0 || name[4]<L'1' || name[4]>L'9') return false;
    const WCHAR* at=name+4; int digits=0; unsigned long long processId=0;
    while(*at>=L'0' && *at<=L'9') { if(++digits>10) return false; processId=processId*10+(*at++-L'0'); }
    if(processId>MAXDWORD || *at++!=L'-') return false;
    for(int i=0;i<16;++i) { WCHAR ch=*at++; if(!((ch>=L'0' && ch<=L'9') || (ch>=L'a' && ch<=L'f'))) return false; }
    return wcscmp(at,L".np3workspace")==0;
}
static bool _WorkspaceHistoryValid(HPATHL path)
{
    NP3_SESSION session=NULL; uint32_t error=0;
    if(!Session_Load(Path_Get(path),&session,&error)) return false;
    bool valid=true;
    for(size_t i=0;i<Session_Count(session) && valid;++i) {
        NP3_SESSION_DOCUMENT entry={0};
        valid=Session_Get(session,i,&entry) && !entry.textLength && ((!entry.path[0] && !entry.flags) || (entry.flags&NP3_SESSION_DISK_BACKED));
    }
    Session_Free(session); return valid;
}
static HANDLE _WorkspaceHistoryClaim(HPATHL lock)
{
    DWORD attributes=Path_GetFileAttributes(lock);
    if(attributes!=INVALID_FILE_ATTRIBUTES && (attributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT))) return INVALID_HANDLE_VALUE;
    return Path_CreateFile(lock,GENERIC_READ|GENERIC_WRITE,0,NULL,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT,NULL);
}
static bool _WorkspaceHistoryDelete(HPATHL path,const FILETIME* stamp)
{
    WIN32_FILE_ATTRIBUTE_DATA current={0};
    return Path_GetFileAttributesEx(path,GetFileExInfoStandard,&current) && !(current.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT)) && CompareFileTime(&current.ftLastWriteTime,stamp)==0 && Path_DeleteFile(path);
}
static unsigned int _WorkspaceRecoveryPruneHistory(HPATHL backup)
{
    HPATHL directory=Path_Copy(backup);
    if(!directory || !Path_RemoveFileSpec(directory)) { Path_Release(directory); return 0; }
    HPATHL guardPath=Path_Copy(directory); bool guardReady=guardPath && Path_Append(guardPath,L"np3-history-cleanup.lock");
    HANDLE guard=guardReady?_WorkspaceHistoryClaim(guardPath):INVALID_HANDLE_VALUE; Path_Release(guardPath);
    if(!IS_VALID_HANDLE(guard)) { Path_Release(directory); return 0; }
    HPATHL pattern=Path_Copy(directory); bool patternReady=pattern && Path_Append(pattern,L"np3-*.np3workspace");
    WIN32_FIND_DATAW found={0}; HANDLE search=patternReady?Path_FindFirstFile(pattern,&found):INVALID_HANDLE_VALUE; Path_Release(pattern);
    WORKSPACE_HISTORY_ENTRY retained[WORKSPACE_HISTORY_LIMIT]={0}; int count=0; unsigned int removed=0;
    if(IS_VALID_HANDLE(search)) {
        do {
            if((found.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT)) || !_WorkspaceHistoryName(found.cFileName)) continue;
            HPATHL path=Path_Copy(directory); bool pathReady=path && Path_Append(path,found.cFileName);
            HPATHL lock=pathReady?Path_Copy(path):NULL; bool lockReady=lock && Path_RenameExtension(lock,L".lock");
            HANDLE claim=lockReady?_WorkspaceHistoryClaim(lock):INVALID_HANDLE_VALUE;
            bool deleted=false;
            if(IS_VALID_HANDLE(claim) && _WorkspaceHistoryValid(path)) {
                int position=0;
                while(position<count) { int order=CompareFileTime(&found.ftLastWriteTime,&retained[position].stamp); if(order>0 || (order==0 && wcscmp(found.cFileName,retained[position].name)>0)) break; ++position; }
                if(position==WORKSPACE_HISTORY_LIMIT) deleted=_WorkspaceHistoryDelete(path,&found.ftLastWriteTime);
                else {
                    WORKSPACE_HISTORY_ENTRY evicted={0}; if(count==WORKSPACE_HISTORY_LIMIT) evicted=retained[count-1];
                    int end=count<WORKSPACE_HISTORY_LIMIT?count++:count-1;
                    for(int i=end;i>position;--i) retained[i]=retained[i-1];
                    StringCchCopyW(retained[position].name,COUNTOF(retained[position].name),found.cFileName); retained[position].stamp=found.ftLastWriteTime;
                    if(evicted.name[0]) {
                        HPATHL old=Path_Copy(directory); bool oldReady=old && Path_Append(old,evicted.name);
                        HPATHL oldLock=oldReady?Path_Copy(old):NULL; bool oldLockReady=oldLock && Path_RenameExtension(oldLock,L".lock");
                        HANDLE oldClaim=oldLockReady?_WorkspaceHistoryClaim(oldLock):INVALID_HANDLE_VALUE;
                        bool oldDeleted=IS_VALID_HANDLE(oldClaim) && _WorkspaceHistoryValid(old) && _WorkspaceHistoryDelete(old,&evicted.stamp);
                        if(IS_VALID_HANDLE(oldClaim)) CloseHandle(oldClaim);
                        if(oldDeleted) { ++removed; Path_DeleteFile(oldLock); }
                        Path_Release(old); Path_Release(oldLock);
                    }
                }
            }
            if(IS_VALID_HANDLE(claim)) CloseHandle(claim);
            if(deleted) { ++removed; Path_DeleteFile(lock); }
            Path_Release(path); Path_Release(lock);
        } while(FindNextFileW(search,&found));
        FindClose(search);
    }
    CloseHandle(guard); Path_Release(directory); return removed;
}
