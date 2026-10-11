#include "../Notepad3/src/Workspace/HistoryService.h"
#include "../Notepad3/src/Workspace/HistoryCore.h"
#include <cstdio>
int wmain() {
    wchar_t temp[MAX_PATH]; if (!GetTempPathW(MAX_PATH,temp)) return 1;
    std::wstring root=std::wstring(temp)+L"np3-history-worker-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64());
    if (!CreateDirectoryW(root.c_str(),nullptr)) return 1;
    std::wstring store=root+L"\\history", path=root+L"\\中文.md";
    int checks=0;
    auto check=[&](bool ok,const char* name) { printf("%s %s\n",ok?"PASS":"FAIL",name); ++checks; return ok; };
    if (!check(!HistoryService_Record(nullptr,path.c_str(),500),"invalid input rejected")) return 1;
    for (int i=0;i<100;++i) if (!HistoryService_Record(store.c_str(),path.c_str(),500)) return 1;
    // The caller can release its strings as soon as enqueue returns.
    path.clear();
    if (!check(HistoryService_Flush(10000),"bounded flush drains asynchronous queue")) return 1;
    DWORD error=0; std::vector<np3::HistoryEntry> entries;
    if (!check(np3::HistoryRead(store,entries,error) && entries.size()==1 && entries[0].count==100 && entries[0].path==root+L"\\中文.md","worker owns metadata and persists all events")) return 1;
    if (!check(HistoryService_Error()==ERROR_SUCCESS,"successful writes have no error")) return 1;
    HANDLE held=CreateFileW((store+L".lock").c_str(),GENERIC_READ,0,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if (held==INVALID_HANDLE_VALUE) return 1;
    if (!HistoryService_Record(store.c_str(),entries[0].path.c_str(),500)) return 1;
    if (!check(!HistoryService_Flush(10),"flush timeout does not block on cross-process lock")) return 1;
    if (!check(HistoryService_Flush(3000) && HistoryService_Error()==ERROR_SHARING_VIOLATION,"background persistence failure is observable")) return 1;
    CloseHandle(held);
    if (!check(np3::HistoryRead(store,entries,error) && entries[0].count==100,"failed worker write preserves previous snapshot")) return 1;
    if (!HistoryService_Record(store.c_str(),entries[0].path.c_str(),500)) return 1;
    if (!check(HistoryService_Flush(3000) && np3::HistoryRead(store,entries,error) && entries[0].count==101,"worker restarts after idle and failure")) return 1;
    DeleteFileW(store.c_str()); DeleteFileW((store+L".lock").c_str()); RemoveDirectoryW(root.c_str());
    printf("%d checks passed\n",checks); return 0;
}
