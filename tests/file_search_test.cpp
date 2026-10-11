#include "../Notepad3/src/Workspace/FileSearchCore.h"
#include <cstdio>
#include <set>
#include <stdexcept>
static int checks=0;
static void Check(bool good,const char* label){++checks;printf("%s %s\n",good?"PASS":"FAIL",label);if(!good)throw std::runtime_error(label);}
static void File(const std::wstring& path){HANDLE file=CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);if(file==INVALID_HANDLE_VALUE)throw std::runtime_error("create fixture");DWORD wrote;WriteFile(file,"fixture",7,&wrote,nullptr);CloseHandle(file);}
static void ContentFile(const std::wstring& path,const void* bytes,DWORD size){HANDLE file=CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);if(file==INVALID_HANDLE_VALUE)throw std::runtime_error("create content fixture");DWORD wrote=0;bool good=WriteFile(file,bytes,size,&wrote,nullptr) && wrote==size;CloseHandle(file);if(!good)throw std::runtime_error("write fixture");}
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
    std::wstring content=root+L"\\content.md";const char data[]="\xef\xbb\xbfHello hello\r\n中文 hello_world\rhello\nHELLO";ContentFile(content,data,sizeof(data)-1);
    std::vector<np3::SearchFile> input{{content,0,{}}};np3::ContentOptions contentOptions;contentOptions.query=L"hello";std::vector<np3::ContentMatch> matches;
    auto collect=[&](const np3::ContentMatch& match){matches.push_back(match);return true;};
    auto contentResult=np3::SearchContents(input,contentOptions,cancel,collect);
    Check(contentResult.files==1 && contentResult.matches==5 && matches[0].line==1 && matches[0].column==1 && matches[2].line==2 && matches[2].column==4 && matches[4].line==4,"UTF8 BOM Chinese positions and mixed CR LF lines");
    contentOptions.wholeWord=true;matches.clear();contentResult=np3::SearchContents(input,contentOptions,cancel,collect);Check(matches.size()==4,"whole word excludes underscore suffix");
    contentOptions.caseSensitive=true;matches.clear();contentResult=np3::SearchContents(input,contentOptions,cancel,collect);Check(matches.size()==2,"case sensitive whole word exact matching");
    contentOptions.maxMatches=1;matches.clear();contentResult=np3::SearchContents(input,contentOptions,cancel,collect);Check(contentResult.limited && matches.size()==1,"content match cap explicit");
    contentOptions.maxMatches=100;contentOptions.maxBytes=2;contentResult=np3::SearchContents(input,contentOptions,cancel,collect);Check(contentResult.skipped==1 && !contentResult.files,"oversized file skipped without reading text");contentOptions.maxBytes=1024;
    cancel=true;matches.clear();contentResult=np3::SearchContents(input,contentOptions,cancel,collect);Check(contentResult.cancelled && matches.empty(),"content pre-cancel emits no matches");cancel=false;
    contentResult=np3::SearchContents(input,contentOptions,cancel,[](const np3::ContentMatch&){return false;});Check(contentResult.cancelled && contentResult.matches==1,"content sink cancellation respected");
    unsigned char little[]={0xff,0xfe,0x2d,0x4e,0x87,0x65,0x0a,0,0x68,0,0x69,0};unsigned char big[]={0xfe,0xff,0x4e,0x2d,0x65,0x87,0,0x0a,0,0x68,0,0x69};
    ContentFile(root+L"\\little.txt",little,sizeof(little));ContentFile(root+L"\\big.txt",big,sizeof(big));input={{root+L"\\little.txt",0,{}},{root+L"\\big.txt",0,{}}};contentOptions.query=L"中文";contentOptions.wholeWord=false;matches.clear();contentResult=np3::SearchContents(input,contentOptions,cancel,collect);Check(matches.size()==2 && matches[0].line==1 && matches[1].context==L"中文","UTF16 little and big endian BOM decode Chinese");
    unsigned char invalid[]={0xff,0xfe,0x00,0xd8};unsigned char binary[]={0x61,0,0x62};ContentFile(root+L"\\invalid.txt",invalid,sizeof(invalid));ContentFile(root+L"\\binary.txt",binary,sizeof(binary));input={{root+L"\\invalid.txt",0,{}},{root+L"\\binary.txt",0,{}},{root+L"\\missing.txt",0,{}}};contentResult=np3::SearchContents(input,contentOptions,cancel,collect);Check(contentResult.skipped==2 && contentResult.errors.size()==1,"malformed surrogate and binary skipped missing file error retained");
    contentOptions.query.clear();contentResult=np3::SearchContents(input,contentOptions,cancel,collect);Check(contentResult.errors.size()==1 && contentResult.errors[0].code==ERROR_INVALID_PARAMETER,"empty content query rejected");
    const char chinese[]="中文测试";ContentFile(root+L"\\word.txt",chinese,sizeof(chinese)-1);input={{root+L"\\word.txt",0,{}}};contentOptions.query=L"中文";contentOptions.wholeWord=true;contentResult=np3::SearchContents(input,contentOptions,cancel,collect);Check(contentResult.matches==0,"Unicode whole word recognizes adjacent Chinese letters");
    contentOptions.query=std::wstring(L"a\0b",3);contentResult=np3::SearchContents(input,contentOptions,cancel,collect);Check(contentResult.errors.size()==1 && contentResult.errors[0].code==ERROR_INVALID_PARAMETER,"embedded NUL query rejected");
    contentOptions=np3::ContentOptions{};contentOptions.regex=true;input={{content,0,{}}};contentOptions.query=L"h(?:ello|ELLO)";matches.clear();contentResult=np3::SearchContents(input,contentOptions,cancel,collect);Check(matches.size()==5 && contentResult.errors.empty(),"PCRE2 alternation and case insensitive regex matches");
    contentOptions.query=L"^hello$";matches.clear();contentResult=np3::SearchContents(input,contentOptions,cancel,collect);Check(matches.size()==2 && matches[0].line==3 && matches[1].line==4,"regex anchors use mixed newline logical lines");
    contentOptions.caseSensitive=true;matches.clear();contentResult=np3::SearchContents(input,contentOptions,cancel,collect);Check(matches.size()==1 && matches[0].line==3,"regex case sensitive mode respected");
    contentOptions.query=L"\\p{Han}+";matches.clear();contentResult=np3::SearchContents(input,contentOptions,cancel,collect);Check(matches.size()==1 && matches[0].line==2 && matches[0].column==1 && matches[0].length==2,"regex Unicode property reports character coordinates");
    contentOptions.query=L"(";contentResult=np3::SearchContents(input,contentOptions,cancel,collect);Check(contentResult.errors.size()==1 && contentResult.errors[0].code==ERROR_BAD_FORMAT && !contentResult.files,"invalid regex rejected before reading files");
    Check(!np3::ValidateSearchRegex(L"(",false) && np3::ValidateSearchRegex(L"hello.*",false),"UI regex validation shares actual PCRE2 compiler");
    contentOptions.query=L"^";contentResult=np3::SearchContents(input,contentOptions,cancel,collect);Check(contentResult.matches==0 && contentResult.errors.empty(),"zero length regex matches skipped without infinite loop");
    contentOptions.query=L"hello";contentOptions.wholeWord=true;contentOptions.caseSensitive=false;contentOptions.maxMatches=1;matches.clear();contentResult=np3::SearchContents(input,contentOptions,cancel,collect);Check(contentResult.limited && matches.size()==1,"regex result cap reports remaining matches");
    const char filtered[]="hello_world hello";ContentFile(root+L"\\filtered.txt",filtered,sizeof(filtered)-1);input={{root+L"\\filtered.txt",0,{}}};matches.clear();contentResult=np3::SearchContents(input,contentOptions,cancel,collect);Check(matches.size()==1 && matches[0].column==13 && !contentResult.limited,"whole word rejected spans do not consume regex result cap");
    std::string adversarial(10000,'a');adversarial+='b';ContentFile(root+L"\\adversarial.txt",adversarial.data(),static_cast<DWORD>(adversarial.size()));input={{root+L"\\adversarial.txt",0,{}}};contentOptions.wholeWord=false;contentOptions.maxMatches=100;contentOptions.query=L"(*NO_START_OPT)(a|aa)+$";ULONGLONG regexStart=GetTickCount64();contentResult=np3::SearchContents(input,contentOptions,cancel,collect);Check(contentResult.errors.size()==1 && contentResult.errors[0].code==ERROR_TIMEOUT && GetTickCount64()-regexStart<5000,"adversarial regex bounded by depth and match limits");
    printf("RESULT checks=%d failures=0\n",checks);return 0;
}catch(const std::exception& error){printf("ERROR %s\n",error.what());return 1;}}
