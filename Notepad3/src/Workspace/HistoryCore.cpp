#include "HistoryCore.h"
#include <algorithm>
#include <atomic>
#include <cstring>
#include <limits>
#include <set>

namespace np3 { namespace {
constexpr size_t MaxBytes=8*1024*1024,MaxEntries=10000;
struct Handle {HANDLE value=INVALID_HANDLE_VALUE;~Handle(){if(value!=INVALID_HANDLE_VALUE)CloseHandle(value);}void Close(){if(value!=INVALID_HANDLE_VALUE){CloseHandle(value);value=INVALID_HANDLE_VALUE;}}};
bool Equal(const std::wstring& a,const std::wstring& b){return CompareStringOrdinal(a.c_str(),-1,b.c_str(),-1,TRUE)==CSTR_EQUAL;}
struct Less {bool operator()(const std::wstring& a,const std::wstring& b)const{return CompareStringOrdinal(a.c_str(),-1,b.c_str(),-1,TRUE)==CSTR_LESS_THAN;}};
bool FullPath(const std::wstring& input,std::wstring& output){if(input.empty() || input.find(L'\0')!=std::wstring::npos)return false;DWORD length=GetFullPathNameW(input.c_str(),0,nullptr,nullptr);if(!length || length>32768)return false;std::vector<wchar_t> buffer(length);DWORD actual=GetFullPathNameW(input.c_str(),length,buffer.data(),nullptr);if(!actual || actual>=length)return false;output.assign(buffer.data(),actual);return true;}
bool SafeStore(const std::wstring& store,DWORD& error){
    if(store.find(L':',2)!=std::wstring::npos){error=ERROR_INVALID_NAME;return false;}
    std::wstring check=store;for(;;){DWORD attributes=GetFileAttributesW(check.c_str());if(attributes!=INVALID_FILE_ATTRIBUTES && (attributes&FILE_ATTRIBUTE_REPARSE_POINT)){error=ERROR_NOT_SUPPORTED;return false;}size_t slash=check.find_last_of(L"\\/");if(slash==std::wstring::npos || slash<3)break;check.resize(slash);}return true;
}
unsigned long long Hash(const std::vector<unsigned char>& bytes,size_t begin){unsigned long long value=14695981039346656037ull;for(size_t i=begin;i<bytes.size();++i){value^=bytes[i];value*=1099511628211ull;}return value;}
void Number(std::vector<unsigned char>& bytes,unsigned long long value){for(int i=0;i<8;++i)bytes.push_back(static_cast<unsigned char>(value>>(i*8)));}
bool Number(const std::vector<unsigned char>& bytes,size_t& cursor,unsigned long long& value){if(cursor>bytes.size() || bytes.size()-cursor<8)return false;value=0;for(int i=0;i<8;++i)value|=static_cast<unsigned long long>(bytes[cursor++])<<(i*8);return true;}
bool Load(const std::wstring& path,std::vector<HistoryEntry>& entries,DWORD& error){
    Handle file;file.value=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
    if(file.value==INVALID_HANDLE_VALUE){error=GetLastError();if(error==ERROR_FILE_NOT_FOUND){entries.clear();error=0;return true;}return false;}
    BY_HANDLE_FILE_INFORMATION info{};if(!GetFileInformationByHandle(file.value,&info)){error=GetLastError();return false;}
    if(info.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT)){error=ERROR_NOT_SUPPORTED;return false;}
    if(info.nFileSizeHigh || info.nFileSizeLow<24 || info.nFileSizeLow>MaxBytes){error=ERROR_BAD_FORMAT;return false;}
    std::vector<unsigned char> bytes(info.nFileSizeLow);DWORD read=0;if(!ReadFile(file.value,bytes.data(),info.nFileSizeLow,&read,nullptr) || read!=info.nFileSizeLow){error=ERROR_READ_FAULT;return false;}
    size_t cursor=8;unsigned long long checksum=0,count=0;
    if(memcmp(bytes.data(),"NP3HIST1",8) || !Number(bytes,cursor,checksum) || checksum!=Hash(bytes,16) || !Number(bytes,cursor,count) || count>MaxEntries){error=ERROR_BAD_FORMAT;return false;}
    std::vector<HistoryEntry> parsed;parsed.reserve(static_cast<size_t>(count));std::set<std::wstring,Less> paths;
    for(unsigned long long i=0;i<count;++i){HistoryEntry entry;unsigned long long size=0,time=0,pinned=0;
        if(!Number(bytes,cursor,size) || !size || size>32767 || size>(bytes.size()-cursor)/2){error=ERROR_BAD_FORMAT;return false;}
        for(unsigned long long c=0;c<size;++c){wchar_t value=static_cast<wchar_t>(bytes[cursor]|(bytes[cursor+1]<<8));cursor+=2;if(!value){error=ERROR_BAD_FORMAT;return false;}entry.path+=value;}
        if(!Number(bytes,cursor,time) || !Number(bytes,cursor,entry.count) || !entry.count || !Number(bytes,cursor,entry.line) || !Number(bytes,cursor,entry.column) || !Number(bytes,cursor,pinned) || pinned>1){error=ERROR_BAD_FORMAT;return false;}
        entry.opened={static_cast<DWORD>(time),static_cast<DWORD>(time>>32)};entry.pinned=pinned!=0;
        std::wstring normalized;if(!FullPath(entry.path,normalized) || normalized!=entry.path || !paths.insert(entry.path).second){error=ERROR_BAD_FORMAT;return false;}parsed.push_back(std::move(entry));
    }
    if(cursor!=bytes.size()){error=ERROR_BAD_FORMAT;return false;}entries.swap(parsed);error=0;return true;
}
bool Save(const std::wstring& path,const std::vector<HistoryEntry>& entries,DWORD& error){
    std::vector<unsigned char> bytes{'N','P','3','H','I','S','T','1'};Number(bytes,0);Number(bytes,entries.size());
    for(const auto& entry:entries){Number(bytes,entry.path.size());for(wchar_t c:entry.path){bytes.push_back(static_cast<unsigned char>(c));bytes.push_back(static_cast<unsigned char>(c>>8));}Number(bytes,(static_cast<unsigned long long>(entry.opened.dwHighDateTime)<<32)|entry.opened.dwLowDateTime);Number(bytes,entry.count);Number(bytes,entry.line);Number(bytes,entry.column);Number(bytes,entry.pinned?1:0);if(bytes.size()>MaxBytes){error=ERROR_BUFFER_OVERFLOW;return false;}}
    unsigned long long hash=Hash(bytes,16);for(int i=0;i<8;++i)bytes[8+i]=static_cast<unsigned char>(hash>>(8*i));
    static std::atomic<unsigned long long> sequence{0};std::wstring temporary=path+L".tmp-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64())+L"-"+std::to_wstring(++sequence);
    Handle file;file.value=CreateFileW(temporary.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);if(file.value==INVALID_HANDLE_VALUE){error=GetLastError();return false;}
    DWORD wrote=0;bool good=WriteFile(file.value,bytes.data(),static_cast<DWORD>(bytes.size()),&wrote,nullptr) && wrote==bytes.size() && FlushFileBuffers(file.value);error=good?0:GetLastError();file.Close();
    if(good && SafeStore(path,error))good=MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;else good=false;
    if(!good){if(!error)error=GetLastError();if(!error)error=ERROR_WRITE_FAULT;DeleteFileW(temporary.c_str());}return good;
}
}
bool HistoryRead(const std::wstring& store,std::vector<HistoryEntry>& entries,DWORD& error){std::wstring path;error=0;if(!FullPath(store,path)){error=ERROR_INVALID_NAME;return false;}if(!SafeStore(path,error))return false;return Load(path,entries,error);}
bool HistoryUpdate(const std::wstring& store,const HistoryChange& change,size_t maximum,std::vector<HistoryEntry>& entries,DWORD& error){
    error=0;if(!maximum || maximum>MaxEntries){error=ERROR_INVALID_PARAMETER;return false;}std::wstring path,target;if(!FullPath(store,path) || (change.action!=HistoryAction::Clear && !FullPath(change.path,target))){error=ERROR_INVALID_NAME;return false;}
    if(!SafeStore(path,error) || !SafeStore(path+L".lock",error))return false;
    Handle lock;ULONGLONG start=GetTickCount64();do{lock.value=CreateFileW((path+L".lock").c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_ALWAYS,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);if(lock.value!=INVALID_HANDLE_VALUE)break;error=GetLastError();if(error!=ERROR_SHARING_VIOLATION && error!=ERROR_LOCK_VIOLATION)return false;Sleep(10);}while(GetTickCount64()-start<1000);
    if(lock.value==INVALID_HANDLE_VALUE)return false;
    BY_HANDLE_FILE_INFORMATION lockInfo{};if(!GetFileInformationByHandle(lock.value,&lockInfo)){error=GetLastError();return false;}if(lockInfo.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT)){error=ERROR_NOT_SUPPORTED;return false;}
    std::vector<HistoryEntry> latest;if(!Load(path,latest,error))return false;
    auto found=std::find_if(latest.begin(),latest.end(),[&](const HistoryEntry& entry){return Equal(entry.path,target);});
    if(change.action==HistoryAction::Clear)latest.clear();
    else if(change.action==HistoryAction::Open){if(found==latest.end()){latest.insert(latest.begin(),HistoryEntry{target,change.time,1,change.line,change.column,false});}else{if(found->count<(std::numeric_limits<unsigned long long>::max)())++found->count;found->path=target;found->opened=change.time;if(change.line){found->line=change.line;found->column=change.column;}std::rotate(latest.begin(),found,found+1);}}
    else if(found==latest.end()){error=ERROR_NOT_FOUND;return false;}
    else if(change.action==HistoryAction::Remove)latest.erase(found);
    else if(change.action==HistoryAction::Pin)found->pinned=change.pinned;
    else if(change.action==HistoryAction::Position){found->line=change.line;found->column=change.column;}
    else{error=ERROR_INVALID_PARAMETER;return false;}
    std::stable_sort(latest.begin(),latest.end(),[](const HistoryEntry& a,const HistoryEntry& b){if(a.pinned!=b.pinned)return a.pinned;return CompareFileTime(&a.opened,&b.opened)>0;});
    size_t pinned=static_cast<size_t>(std::count_if(latest.begin(),latest.end(),[](const HistoryEntry& entry){return entry.pinned;}));size_t limit=(std::max)(maximum,pinned);if(latest.size()>limit)latest.resize(limit);
    if(!Save(path,latest,error))return false;entries.swap(latest);error=0;return true;
}
}
