#include "../Notepad3/src/Workspace/HistoryCore.h"
#include <cstdio>
#include <stdexcept>
static int checks=0;
static void Check(bool good,const char* name){++checks;printf("%s %s\n",good?"PASS":"FAIL",name);if(!good)throw std::runtime_error(name);}
static void Write(const std::wstring& path,const char* data,DWORD length){HANDLE file=CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);if(file==INVALID_HANDLE_VALUE)throw std::runtime_error("fixture write");DWORD count=0;bool good=WriteFile(file,data,length,&count,nullptr)&&count==length;CloseHandle(file);if(!good)throw std::runtime_error("fixture write bytes");}
static std::vector<char> Bytes(const std::wstring& path){HANDLE file=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);if(file==INVALID_HANDLE_VALUE)throw std::runtime_error("fixture read");DWORD size=GetFileSize(file,nullptr),count=0;std::vector<char> bytes(size);bool good=ReadFile(file,bytes.data(),size,&count,nullptr)&&count==size;CloseHandle(file);if(!good)throw std::runtime_error("fixture read bytes");return bytes;}
int wmain(int argc,wchar_t** argv){try{
    if(argc==4 && !wcscmp(argv[1],L"--writer")){np3::HistoryChange change;change.path=argv[3];GetSystemTimeAsFileTime(&change.time);std::vector<np3::HistoryEntry> entries;DWORD error=0;return np3::HistoryUpdate(argv[2],change,500,entries,error)?0:1;}
    wchar_t temporary[MAX_PATH],exe[32768];GetTempPathW(MAX_PATH,temporary);GetModuleFileNameW(nullptr,exe,32768);
    std::wstring root=std::wstring(temporary)+L"np3-history-中文-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64());CreateDirectoryW(root.c_str(),nullptr);std::wstring store=root+L"\\history.dat",a=root+L"\\说明.txt",b=root+L"\\b.txt",c=root+L"\\c.txt";Write(a,"original",8);Write(b,"original",8);Write(c,"original",8);
    DWORD error=0;std::vector<np3::HistoryEntry> entries;np3::HistoryChange change;change.path=a;change.time={1,0};change.line=10;change.column=5;
    Check(np3::HistoryRead(store,entries,error) && entries.empty(),"missing history is empty without document reads");
    Check(np3::HistoryUpdate(store,change,2,entries,error) && entries.size()==1 && entries[0].count==1,"record first open with metadata");
    entries.clear();Check(np3::HistoryRead(store,entries,error) && entries[0].path==a && entries[0].line==10 && entries[0].column==5,"actual disk snapshot restores Unicode path and reading position");
    change.path=root+L"\\说明.TXT";change.time={2,0};Check(np3::HistoryUpdate(store,change,2,entries,error) && entries.size()==1 && entries[0].count==2,"case insensitive duplicate increments open count");
    change.action=np3::HistoryAction::Pin;change.pinned=true;Check(np3::HistoryUpdate(store,change,2,entries,error) && entries[0].pinned,"pin record persistently");
    change.action=np3::HistoryAction::Open;change.path=b;change.time={3,0};Check(np3::HistoryUpdate(store,change,2,entries,error) && entries.size()==2,"add second document");
    change.path=c;change.time={4,0};Check(np3::HistoryUpdate(store,change,2,entries,error) && entries.size()==2 && entries[0].pinned && entries[1].path==c,"trim oldest unpinned record preserving pinned document");
    change.action=np3::HistoryAction::Position;change.path=a;change.line=77;Check(np3::HistoryUpdate(store,change,2,entries,error) && entries[0].line==77 && entries[0].count==2 && entries[0].opened.dwLowDateTime==2,"position update does not increase count or change open timestamp");
    change.action=np3::HistoryAction::Remove;change.path=c;Check(np3::HistoryUpdate(store,change,2,entries,error) && entries.size()==1 && Bytes(c)==std::vector<char>({'o','r','i','g','i','n','a','l'}),"remove history record does not delete or rewrite document");
    auto original=Bytes(store);HANDLE held=CreateFileW((store+L".lock").c_str(),GENERIC_READ,0,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);change.action=np3::HistoryAction::Open;change.path=b;
    Check(held!=INVALID_HANDLE_VALUE && !np3::HistoryUpdate(store,change,2,entries,error) && error==ERROR_SHARING_VIOLATION && Bytes(store)==original,"exclusive transaction lock rejects contention without changes");CloseHandle(held);
    held=CreateFileW(store.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    Check(held!=INVALID_HANDLE_VALUE && !np3::HistoryUpdate(store,change,2,entries,error) && Bytes(store)==original,"blocked atomic replacement preserves complete old snapshot");CloseHandle(held);
    std::wstring corrupt=root+L"\\corrupt.dat";original.back()^=1;Write(corrupt,original.data(),static_cast<DWORD>(original.size()));auto oldCount=entries.size();
    Check(!np3::HistoryRead(corrupt,entries,error) && error==ERROR_BAD_FORMAT && entries.size()==oldCount,"corrupt checksum preserves caller history");
    Check(!np3::HistoryUpdate(corrupt,change,2,entries,error) && Bytes(corrupt)==original,"corrupt store is not silently overwritten");
    change.action=np3::HistoryAction::Clear;Check(np3::HistoryUpdate(store,change,2,entries,error) && entries.empty() && Bytes(a).size()==8,"clear removes records only");
    std::vector<PROCESS_INFORMATION> children;
    for(int i=0;i<8;++i){std::wstring command=L"\""+std::wstring(exe)+L"\" --writer \""+store+L"\" \""+root+L"\\shared.txt\"";STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION process{};if(!CreateProcessW(nullptr,&command[0],nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process))throw std::runtime_error("spawn writer");children.push_back(process);}
    bool merged=true;for(auto& process:children){DWORD code=1;merged=WaitForSingleObject(process.hProcess,10000)==WAIT_OBJECT_0 && merged;GetExitCodeProcess(process.hProcess,&code);merged=merged && !code;CloseHandle(process.hThread);CloseHandle(process.hProcess);}
    Check(merged && np3::HistoryRead(store,entries,error) && entries.size()==1 && entries[0].count==8,"eight independent writer processes merge latest counts without lost updates");
    Check(!np3::HistoryUpdate(store,change,0,entries,error) && error==ERROR_INVALID_PARAMETER,"invalid history capacity rejected");
    change.action=np3::HistoryAction::Open;change.path=root+L"\\"+std::wstring(300,L'a')+L".txt";GetSystemTimeAsFileTime(&change.time);
    Check(np3::HistoryUpdate(store,change,500,entries,error) && np3::HistoryRead(store,entries,error) && entries.size()==2 && entries[0].path==change.path,"long unavailable document path preserved as metadata without opening document");
    auto stable=Bytes(store);change.action=np3::HistoryAction::Position;change.path=root+L"\\never-recorded.txt";
    Check(!np3::HistoryUpdate(store,change,500,entries,error) && error==ERROR_NOT_FOUND && Bytes(store)==stable,"position for unknown record does not create or mutate history");
    change.action=np3::HistoryAction::Open;change.path=root+L"\\same-clock.txt";change.time=entries[0].opened;
    Check(np3::HistoryUpdate(store,change,500,entries,error) && entries[0].path==change.path,"most recent open takes precedence when clock timestamps tie");
    printf("RESULT checks=%d failures=0\n",checks);return 0;
}catch(const std::exception& error){printf("ERROR %s\n",error.what());return 1;}}
