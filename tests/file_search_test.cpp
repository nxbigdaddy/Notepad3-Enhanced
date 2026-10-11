#include "../Notepad3/src/Workspace/FileSearchCore.h"
#include <cstdio>
#include <set>
#include <stdexcept>
static int checks=0;
static void Check(bool good,const char* label){++checks;printf("%s %s\n",good?"PASS":"FAIL",label);if(!good)throw std::runtime_error(label);}
static void File(const std::wstring& path){HANDLE file=CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);if(file==INVALID_HANDLE_VALUE)throw std::runtime_error("create fixture");DWORD wrote;WriteFile(file,"fixture",7,&wrote,nullptr);CloseHandle(file);}
int main(){try{
    wchar_t temporary[MAX_PATH];GetTempPathW(MAX_PATH,temporary);
    std::wstring root=std::wstring(temporary)+L"np3-search-中文-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64());
    CreateDirectoryW(root.c_str(),nullptr);CreateDirectoryW((root+L"\\nested").c_str(),nullptr);CreateDirectoryW((root+L"\\.git").c_str(),nullptr);
    File(root+L"\\说明.md");File(root+L"\\UPPER.MD");File(root+L"\\other.txt");File(root+L"\\nested\\child.md");File(root+L"\\.git\\hidden.md");
    std::atomic_bool cancel(false);np3::SearchOptions options;options.roots={root,root+L"\\nested",root};options.batchSize=2;
    std::vector<np3::SearchFile> files;size_t batches=0;auto sink=[&](const std::vector<np3::SearchFile>& batch){Check(batch.size()<=2,"bounded published batch");++batches;files.insert(files.end(),batch.begin(),batch.end());return true;};
    auto result=np3::SearchFiles(options,cancel,sink);Check(result.files==3 && files.size()==3 && result.errors.empty() && !result.limited,"recursive default Markdown matches uppercase extension and excludes git");
    std::set<std::wstring> unique;for(const auto& file:files){unique.insert(file.path);Check(file.size==7,"actual enumerated file size");}Check(unique.size()==3 && batches==2,"overlapping and repeated roots have no duplicate paths");
    std::vector<std::wstring> extensions{L".old"};Check(np3::ParseSearchExtensions(L"*.MD; md,txt .json",extensions) && extensions.size()==3,"custom extension syntax and case-insensitive dedup");
    auto previous=extensions;Check(!np3::ParseSearchExtensions(L"../bad",extensions) && extensions==previous,"invalid extension input preserves old filter");
    Check(np3::ParseSearchExtensions(L"*",extensions) && extensions.empty(),"all formats filter");
    files.clear();options.extensions=extensions;result=np3::SearchFiles(options,cancel,sink);Check(result.files==4,"all formats includes text files");
    files.clear();options.query=L"CHILD";result=np3::SearchFiles(options,cancel,sink);Check(result.files==1 && files[0].path.find(L"child.md")!=std::wstring::npos,"case-insensitive path query");
    files.clear();options.query.clear();options.roots={root};options.recursive=false;result=np3::SearchFiles(options,cancel,sink);Check(result.files==3,"nonrecursive search excludes descendants");
    files.clear();options.recursive=true;options.maxFiles=2;result=np3::SearchFiles(options,cancel,sink);Check(result.files==2 && result.limited && files.size()==2,"result limit reported explicitly");
    cancel=true;files.clear();result=np3::SearchFiles(options,cancel,sink);Check(result.cancelled && result.files==0 && files.empty(),"pre-cancel publishes no results");cancel=false;
    options.maxFiles=100;options.roots={root+L"\\missing",root};files.clear();result=np3::SearchFiles(options,cancel,sink);Check(result.errors.size()==1 && result.files==4,"bad root preserves valid root results");
    result=np3::SearchFiles(options,cancel,[](const std::vector<np3::SearchFile>&){return false;});Check(result.cancelled && result.files==2,"sink cancellation stops further enumeration");
    printf("RESULT checks=%d failures=0\n",checks);return 0;
}catch(const std::exception& error){printf("ERROR %s\n",error.what());return 1;}}
