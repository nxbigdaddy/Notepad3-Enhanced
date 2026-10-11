#include "FileSearchCore.h"
#include <algorithm>
#include <set>
#include <deque>
#include <cwctype>

namespace np3 { namespace {
bool Equal(const std::wstring& a,const std::wstring& b) {return CompareStringOrdinal(a.c_str(),-1,b.c_str(),-1,TRUE)==CSTR_EQUAL;}
struct Less {bool operator()(const std::wstring& a,const std::wstring& b) const {return CompareStringOrdinal(a.c_str(),-1,b.c_str(),-1,TRUE)==CSTR_LESS_THAN;}};
bool Contains(const std::wstring& value,const std::wstring& query) {
    if(query.empty()) return true;
    if(query.size()>value.size()) return false;
    for(size_t i=0;i<=value.size()-query.size();++i)
        if(CompareStringOrdinal(value.data()+i,static_cast<int>(query.size()),query.data(),static_cast<int>(query.size()),TRUE)==CSTR_EQUAL) return true;
    return false;
}
bool Matches(const std::wstring& name,const std::vector<std::wstring>& extensions) {
    if(extensions.empty()) return true;
    for(const auto& extension:extensions) if(name.size()>=extension.size() && Equal(name.substr(name.size()-extension.size()),extension)) return true;
    return false;
}
std::wstring FullPath(const std::wstring& value) {
    DWORD count=GetFullPathNameW(value.c_str(),0,nullptr,nullptr); if(!count) return {};
    std::vector<wchar_t> buffer(count); DWORD actual=GetFullPathNameW(value.c_str(),count,buffer.data(),nullptr);
    if(!actual || actual>=count) return {};
    std::wstring result(buffer.data(),actual);
    while(result.size()>3 && result.back()==L'\\') result.pop_back();
    return result;
}
std::wstring Join(const std::wstring& root,const std::wstring& name) {return root+(root.back()==L'\\'?L"":L"\\")+name;}
struct Enumeration {HANDLE handle=INVALID_HANDLE_VALUE; ~Enumeration(){if(handle!=INVALID_HANDLE_VALUE) FindClose(handle);}};
}
bool ParseSearchExtensions(const std::wstring& text,std::vector<std::wstring>& extensions) {
    std::vector<std::wstring> parsed; std::wstring token;
    auto accept=[&]()->bool {
        if(token.empty()) return true;
        if(token==L"*" || token==L"*.*") {parsed={L"*"};token.clear();return true;}
        if(token.size()>1 && token[0]==L'*') token.erase(0,1);
        if(token.empty()) return false;
        if(token[0]!=L'.') token.insert(0,1,L'.');
        if(token.size()<2 || token.size()>64 || token.find_first_of(L"\\/:*?\"<>|")!=std::wstring::npos) return false;
        if(std::none_of(parsed.begin(),parsed.end(),[&](const std::wstring& old){return Equal(old,token);})) parsed.push_back(token);
        token.clear();return true;
    };
    if(text.size()>4096) return false;
    for(wchar_t c:text) {if(c==L';' || c==L',' || iswspace(c)){if(!accept())return false;} else token+=c;}
    if(!accept()) return false;
    if(std::find(parsed.begin(),parsed.end(),L"*")!=parsed.end()) parsed.clear();
    extensions.swap(parsed);return true;
}
SearchSummary SearchFiles(const SearchOptions& options,const std::atomic_bool& cancel,const std::function<bool(const std::vector<SearchFile>&)>& sink) {
    SearchSummary summary;
    if(options.roots.size()>64 || options.query.size()>4096 || !options.maxFiles || !options.batchSize || options.batchSize>4096 || !sink) {summary.errors.push_back({L"",ERROR_INVALID_PARAMETER});return summary;}
    std::deque<std::wstring> pending;
    std::set<std::wstring,Less> seen;
    for(const auto& root:options.roots) {
        if(cancel.load()){summary.cancelled=true;return summary;}
        std::wstring normalized=root.empty()?L"":FullPath(root);
        if(normalized.empty()){summary.errors.push_back({root,ERROR_INVALID_NAME});continue;}
        if(seen.insert(normalized).second) pending.push_back(normalized);
    }
    std::vector<SearchFile> batch; batch.reserve(options.batchSize);
    while(!pending.empty()) {
        if(cancel.load()){summary.cancelled=true;return summary;}
        std::wstring directory=std::move(pending.front());pending.pop_front();
        DWORD attributes=GetFileAttributesW(directory.c_str());
        if(attributes==INVALID_FILE_ATTRIBUTES){summary.errors.push_back({directory,GetLastError()});continue;}
        if(!(attributes&FILE_ATTRIBUTE_DIRECTORY)){summary.errors.push_back({directory,ERROR_DIRECTORY});continue;}
        if(attributes&FILE_ATTRIBUTE_REPARSE_POINT){summary.errors.push_back({directory,ERROR_NOT_SUPPORTED});continue;}
        WIN32_FIND_DATAW data{}; Enumeration enumeration;
        enumeration.handle=FindFirstFileW(Join(directory,L"*").c_str(),&data);
        if(enumeration.handle==INVALID_HANDLE_VALUE){DWORD error=GetLastError();if(error!=ERROR_FILE_NOT_FOUND)summary.errors.push_back({directory,error});continue;}
        for(;;) {
            if(cancel.load()){summary.cancelled=true;return summary;}
            std::wstring name=data.cFileName;
            if(name!=L"." && name!=L".." && !(data.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)) {
                std::wstring path=Join(directory,name);
                if(data.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY) {
                    bool excluded=std::any_of(options.excludedDirectories.begin(),options.excludedDirectories.end(),[&](const std::wstring& entry){return Equal(entry,name);});
                    if(options.recursive && !excluded && seen.insert(path).second) pending.push_back(path);
                } else if(Matches(name,options.extensions) && Contains(path,options.query)) {
                    if(summary.files==options.maxFiles){summary.limited=true;break;}
                    batch.push_back({path,(static_cast<unsigned long long>(data.nFileSizeHigh)<<32)|data.nFileSizeLow,data.ftLastWriteTime});++summary.files;
                    if(batch.size()==options.batchSize){if(!sink(batch)){summary.cancelled=true;return summary;}batch.clear();}
                }
            }
            if(!FindNextFileW(enumeration.handle,&data)){DWORD error=GetLastError();if(error!=ERROR_NO_MORE_FILES)summary.errors.push_back({directory,error});break;}
        }
        if(summary.limited)break;
    }
    if(cancel.load()){summary.cancelled=true;return summary;}
    if(!batch.empty() && !sink(batch)) summary.cancelled=true;
    return summary;
}
}
