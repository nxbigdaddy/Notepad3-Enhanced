#include "FileSearchCore.h"
#include <algorithm>
#include <set>
#include <deque>
#include <cwctype>
#include <cstring>
#define PCRE2_CODE_UNIT_WIDTH 8
#define PCRE2_STATIC
#include "../../scintilla/pcre2/src/pcre2.h"

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
namespace {
struct FileHandle { HANDLE value=INVALID_HANDLE_VALUE; ~FileHandle(){if(value!=INVALID_HANDLE_VALUE)CloseHandle(value);} };
bool Word(wchar_t character){WORD type=0;return character==L'_' || (GetStringTypeW(CT_CTYPE1,&character,1,&type) && (type&(C1_ALPHA|C1_DIGIT)));}
std::string Utf8(const std::wstring& text){if(text.empty())return {};int count=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,text.data(),static_cast<int>(text.size()),nullptr,0,nullptr,nullptr);if(!count)return {};std::string value(count,'\0');WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,text.data(),static_cast<int>(text.size()),&value[0],count,nullptr,nullptr);return value;}
struct Regex {
    pcre2_code* code=nullptr;pcre2_match_data* data=nullptr;pcre2_match_context* context=nullptr;
    ~Regex(){pcre2_match_context_free(context);pcre2_match_data_free(data);pcre2_code_free(code);}
    bool Compile(const ContentOptions& options,const std::atomic_bool& cancel){
        auto pattern=Utf8(options.query);if(pattern.empty())return false;int error=0;PCRE2_SIZE offset=0;
        code=pcre2_compile(reinterpret_cast<PCRE2_SPTR>(pattern.data()),pattern.size(),PCRE2_UTF|PCRE2_UCP|PCRE2_AUTO_CALLOUT|(options.caseSensitive?0:PCRE2_CASELESS),&error,&offset,nullptr);
        if(!code)return false;data=pcre2_match_data_create_from_pattern(code,nullptr);context=pcre2_match_context_create(nullptr);if(!data || !context)return false;
        pcre2_set_match_limit(context,100000);pcre2_set_depth_limit(context,1000);
        pcre2_set_callout(context,[](pcre2_callout_block*,void* state)->int{return static_cast<const std::atomic_bool*>(state)->load()?PCRE2_ERROR_CALLOUT:0;},const_cast<std::atomic_bool*>(&cancel));return true;
    }
};
struct Span {size_t start,end;};
bool RegexSpans(Regex& regex,const std::wstring& text,size_t limit,bool wholeWord,const std::atomic_bool& cancel,std::vector<Span>& spans,DWORD& error){
    size_t lineStart=0;
    while(lineStart<=text.size()){
        if(cancel.load())return false;
        size_t lineEnd=text.find_first_of(L"\r\n",lineStart);if(lineEnd==std::wstring::npos)lineEnd=text.size();
        std::string bytes=Utf8(text.substr(lineStart,lineEnd-lineStart));size_t offset=0,previous=0,wide=0;
        while(offset<=bytes.size()){
            int found=pcre2_match(regex.code,reinterpret_cast<PCRE2_SPTR>(bytes.data()),bytes.size(),offset,0,regex.data,regex.context);
            if(found==PCRE2_ERROR_NOMATCH)break;
            if(found<0){error=(found==PCRE2_ERROR_MATCHLIMIT || found==PCRE2_ERROR_DEPTHLIMIT)?ERROR_TIMEOUT:ERROR_BAD_FORMAT;return false;}
            PCRE2_SIZE* range=pcre2_get_ovector_pointer(regex.data);size_t begin=range[0],end=range[1];
            if(begin==end){if(end==bytes.size())break;offset=end+1;while(offset<bytes.size() && (static_cast<unsigned char>(bytes[offset])&0xc0)==0x80)++offset;continue;}
            if(begin>previous)wide+=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,bytes.data()+previous,static_cast<int>(begin-previous),nullptr,0);
            size_t length=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,bytes.data()+begin,static_cast<int>(end-begin),nullptr,0);
            if(length>4096){error=ERROR_BUFFER_OVERFLOW;return false;}
            size_t absolute=lineStart+wide,absoluteEnd=absolute+length;
            if(!wholeWord || ((!absolute || !Word(text[absolute-1])) && (absoluteEnd==text.size() || !Word(text[absoluteEnd])))){spans.push_back({absolute,absoluteEnd});if(spans.size()>limit)return true;}
            wide+=length;previous=end;offset=end;if(cancel.load())return false;
        }
        if(lineEnd==text.size())break;lineStart=lineEnd+1;if(text[lineEnd]==L'\r' && lineStart<text.size() && text[lineStart]==L'\n')++lineStart;
    }
    return true;
}
bool Decode(const std::vector<unsigned char>& bytes,std::wstring& text) {
    if(bytes.size()>=2 && ((bytes[0]==0xff && bytes[1]==0xfe)||(bytes[0]==0xfe && bytes[1]==0xff))){
        if(bytes.size()%2)return false; bool little=bytes[0]==0xff;
        for(size_t i=2;i<bytes.size();i+=2)text.push_back(static_cast<wchar_t>(little?bytes[i]|(bytes[i+1]<<8):(bytes[i]<<8)|bytes[i+1]));
        for(size_t i=0;i<text.size();++i){if(text[i]>=0xd800 && text[i]<=0xdbff){if(++i==text.size() || text[i]<0xdc00 || text[i]>0xdfff)return false;}else if(text[i]>=0xdc00 && text[i]<=0xdfff)return false;}
    }else{
        size_t start=bytes.size()>=3 && bytes[0]==0xef && bytes[1]==0xbb && bytes[2]==0xbf?3:0;
        if(bytes.size()>start){int count=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,reinterpret_cast<const char*>(bytes.data()+start),static_cast<int>(bytes.size()-start),nullptr,0);if(!count)return false;
            text.resize(count);if(!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,reinterpret_cast<const char*>(bytes.data()+start),static_cast<int>(bytes.size()-start),&text[0],count))return false;}
    }
    return text.find(L'\0')==std::wstring::npos;
}
}
bool ValidateSearchRegex(const std::wstring& query,bool caseSensitive){if(query.empty() || query.size()>4096 || query.find(L'\0')!=std::wstring::npos)return false;ContentOptions options;options.query=query;options.caseSensitive=caseSensitive;Regex regex;std::atomic_bool cancel(false);return regex.Compile(options,cancel);}
ContentSummary SearchContents(const std::vector<SearchFile>& files,const ContentOptions& options,const std::atomic_bool& cancel,const std::function<bool(const ContentMatch&)>& sink) {
    ContentSummary result;
    if(options.query.empty() || options.query.size()>4096 || options.query.find(L'\0')!=std::wstring::npos || options.query.find_first_of(L"\r\n")!=std::wstring::npos || !options.maxBytes || options.maxBytes>64*1024*1024 || !options.maxMatches || !sink){result.errors.push_back({L"",ERROR_INVALID_PARAMETER});return result;}
    Regex regex;if(options.regex && !regex.Compile(options,cancel)){result.errors.push_back({L"",ERROR_BAD_FORMAT});return result;}
    for(const auto& file:files){
        if(cancel.load()){result.cancelled=true;return result;}
        FileHandle handle;handle.value=CreateFileW(file.path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_SEQUENTIAL_SCAN,nullptr);
        if(handle.value==INVALID_HANDLE_VALUE){result.errors.push_back({file.path,GetLastError()});continue;}
        BY_HANDLE_FILE_INFORMATION before{},after{};
        if(!GetFileInformationByHandle(handle.value,&before)){result.errors.push_back({file.path,GetLastError()});continue;}
        if(before.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT)){++result.skipped;continue;}
        unsigned long long size=(static_cast<unsigned long long>(before.nFileSizeHigh)<<32)|before.nFileSizeLow;
        if(size>options.maxBytes){++result.skipped;continue;}
        std::vector<unsigned char> bytes(static_cast<size_t>(size));size_t offset=0;bool read=true;
        while(offset<bytes.size()){
            if(cancel.load()){result.cancelled=true;return result;}DWORD count=0;
            if(!ReadFile(handle.value,bytes.data()+offset,static_cast<DWORD>((std::min)(bytes.size()-offset,size_t(65536))),&count,nullptr)){result.errors.push_back({file.path,GetLastError()});read=false;break;}
            if(!count){result.errors.push_back({file.path,ERROR_HANDLE_EOF});read=false;break;}offset+=count;
        }
        if(!read)continue;
        if(!GetFileInformationByHandle(handle.value,&after)){result.errors.push_back({file.path,GetLastError()});continue;}
        if(before.nFileSizeHigh!=after.nFileSizeHigh || before.nFileSizeLow!=after.nFileSizeLow || CompareFileTime(&before.ftLastWriteTime,&after.ftLastWriteTime)!=0){result.errors.push_back({file.path,ERROR_RETRY});continue;}
        std::wstring text;if(!Decode(bytes,text)){++result.skipped;continue;}++result.files;
        std::vector<Span> spans;size_t span=0;if(options.regex){DWORD error=0;if(!RegexSpans(regex,text,options.maxMatches-result.matches,options.wholeWord,cancel,spans,error)){if(cancel.load()){result.cancelled=true;return result;}result.errors.push_back({file.path,error});continue;}}
        size_t line=1,start=0;
        for(size_t i=0;i<text.size();++i){
            if(cancel.load()){result.cancelled=true;return result;}
            if(text[i]==L'\r' || text[i]==L'\n'){if(text[i]==L'\r' && i+1<text.size() && text[i+1]==L'\n')++i;++line;start=i+1;continue;}
            if(!options.regex && options.query.size()>text.size()-i)continue;
            bool matches=options.regex?(span<spans.size() && spans[span].start==i):CompareStringOrdinal(text.data()+i,static_cast<int>(options.query.size()),options.query.data(),static_cast<int>(options.query.size()),options.caseSensitive?FALSE:TRUE)==CSTR_EQUAL;
            if(!matches)continue;
            size_t end=options.regex?spans[span++].end:i+options.query.size();if(options.wholeWord && ((i && Word(text[i-1])) || (end<text.size() && Word(text[end]))))continue;
            if(result.matches==options.maxMatches){result.limited=true;return result;}
            size_t contextStart=(std::max)(start,i>80?i-80:0),contextEnd=text.find_first_of(L"\r\n",i);if(contextEnd==std::wstring::npos)contextEnd=text.size();contextEnd=(std::min)(contextEnd,end+80);
            ContentMatch match{file.path,text.substr(contextStart,contextEnd-contextStart),line,i-start+1,end-i};++result.matches;
            if(!sink(match)){result.cancelled=true;return result;}
            i=end-1;
        }
    }
    return result;
}
}
