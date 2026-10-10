#include "MarkdownStandalone.h"
#include "MarkdownExport.h"
#include "MarkdownRender.h"
#include <windows.h>
#include <bcrypt.h>
#include <wincrypt.h>
#include <regex>
#include <stdexcept>
#include <system_error>
#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "crypt32.lib")
namespace {
std::wstring NativePath(const std::wstring& path) {
    DWORD length=GetFullPathNameW(path.c_str(),0,nullptr,nullptr);
    if(!length || length>=32768) throw std::invalid_argument("Invalid asset path");
    std::wstring full(length,0); DWORD written=GetFullPathNameW(path.c_str(),length,full.data(),nullptr);
    if(!written || written>=length) throw std::invalid_argument("Invalid asset path"); full.resize(written);
    if(full.rfind(L"\\\\?\\",0)==0) return full;
    return full.rfind(L"\\\\",0)==0?L"\\\\?\\UNC\\"+full.substr(2):L"\\\\?\\"+full;
}
std::string Read(const std::wstring& path) {
    const auto native=NativePath(path);
    HANDLE file=CreateFileW(native.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE) throw std::system_error(static_cast<int>(GetLastError()),std::system_category(),"Renderer asset missing");
    LARGE_INTEGER size={};
    if(!GetFileSizeEx(file,&size) || size.QuadPart<=0 || size.QuadPart>8*1024*1024) { CloseHandle(file); throw std::runtime_error("Invalid renderer asset size"); }
    std::string bytes;
    try { bytes.resize(static_cast<size_t>(size.QuadPart)); } catch(...) { CloseHandle(file); throw; }
    DWORD count=0; bool success=ReadFile(file,bytes.data(),static_cast<DWORD>(bytes.size()),&count,nullptr) && count==bytes.size(); CloseHandle(file);
    if(!success) throw std::runtime_error("Cannot read renderer asset"); return bytes;
}
std::string Base64(const unsigned char* data,size_t size) {
    DWORD length=0;
    if(!CryptBinaryToStringA(data,static_cast<DWORD>(size),CRYPT_STRING_BASE64|CRYPT_STRING_NOCRLF,nullptr,&length)) throw std::runtime_error("Base64 sizing failed");
    std::string value(length,0);
    if(!CryptBinaryToStringA(data,static_cast<DWORD>(size),CRYPT_STRING_BASE64|CRYPT_STRING_NOCRLF,value.data(),&length)) throw std::runtime_error("Base64 failed");
    value.resize(length); if(!value.empty() && value.back()==0) value.pop_back(); return value;
}
std::string Hash(const std::string& source) {
    BCRYPT_ALG_HANDLE algorithm=nullptr; unsigned char digest[32];
    if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0) throw std::runtime_error("Hash provider failed");
    NTSTATUS result=BCryptHash(algorithm,nullptr,0,reinterpret_cast<PUCHAR>(const_cast<char*>(source.data())),static_cast<ULONG>(source.size()),digest,sizeof(digest));
    BCryptCloseAlgorithmProvider(algorithm,0); if(result<0) throw std::runtime_error("Hash failed"); return Base64(digest,sizeof(digest));
}
void Replace(std::string& text,const std::string& before,const std::string& after) {
    size_t position=text.find(before); if(position==std::string::npos) throw std::runtime_error("Renderer template changed"); text.replace(position,before.size(),after);
}
std::string TextAsset(const std::wstring& path,const std::string& closing) {
    auto bytes=Read(path);
    if(!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,bytes.data(),static_cast<int>(bytes.size()),nullptr,0) || bytes.find('\0')!=std::string::npos) throw std::runtime_error("Invalid asset text");
    std::string normalized; normalized.reserve(bytes.size());
    for(size_t i=0;i<bytes.size();++i) { if(bytes[i]=='\r') { normalized+='\n'; if(i+1<bytes.size() && bytes[i+1]=='\n') ++i; } else normalized+=bytes[i]; }
    std::string lower=normalized;
    for(char& c:lower) if(c>='A' && c<='Z') c+=32;
    if(lower.find(closing)!=std::string::npos) throw std::runtime_error("Asset cannot be safely embedded"); return normalized;
}
std::string FontCss(const std::wstring& assets) {
    const auto css=TextAsset(assets+L"\\katex.min.css","</style");
    const std::regex font("src:url\\(fonts/([A-Za-z0-9_-]+)\\.woff2\\)[^}]*");
    std::string result; size_t previous=0,faces=0;
    for(std::sregex_iterator i(css.begin(),css.end(),font),end;i!=end;++i) {
        const auto& match=*i; result.append(css,previous,static_cast<size_t>(match.position())-previous);
        const std::string name=match[1].str(); const std::wstring filename(name.begin(),name.end());
        const auto data=Read(assets+L"\\fonts\\"+filename+L".woff2");
        result+="src:url(data:font/woff2;base64,"+Base64(reinterpret_cast<const unsigned char*>(data.data()),data.size())+") format(\"woff2\")";
        previous=static_cast<size_t>(match.position()+match.length()); ++faces;
    }
    result.append(css,previous,std::string::npos);
    if(!faces || result.find("url(fonts/")!=std::string::npos) throw std::runtime_error("Unsupported font asset layout"); return result;
}
}
namespace np3 {
std::wstring MarkdownAssetsDirectory() {
    wchar_t path[32768]; DWORD size=GetModuleFileNameW(nullptr,path,32768); if(!size || size>=32768) throw std::runtime_error("Cannot locate renderer assets");
    std::wstring directory(path,size); directory.resize(directory.find_last_of(L"\\/")+1); return directory+L"render-assets";
}
std::string RenderMarkdownStandalone(std::string_view markdown,const std::wstring& directory,const std::wstring& assets) {
    if(markdown.size()>8*1024*1024) throw std::length_error("Export exceeds input limit");
    if(!markdown.empty() && !MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,markdown.data(),static_cast<int>(markdown.size()),nullptr,0)) throw std::invalid_argument("Invalid Markdown UTF8");
    auto advanced=RenderMarkdown(markdown,true);
    const auto body=advanced.find("<body>");
    if(advanced.find("<x-equation",body)==std::string::npos && advanced.find("<code class=\"language-mermaid\"",body)==std::string::npos) return RenderMarkdownExport(markdown,directory);
    // Reuse the validated source-image base URI and print rules from the exporter.
    auto base=RenderMarkdownExport("",directory); const auto start=base.find("<base href="); const auto end=base.find('>',start);
    Replace(advanced,"<base href=\"https://np3-preview.invalid/\">",base.substr(start,end-start+1));
    Replace(advanced,"img-src https://np3-preview.invalid data:","img-src file: data:"); Replace(advanced,"base-uri https://np3-preview.invalid","base-uri file:");
    std::string scriptHashes;
    for(const char* file:{"katex.min.js","mermaid.min.js","np3-render.js"}) {
        const std::string narrow=file; const std::wstring name(narrow.begin(),narrow.end());
        const auto script=TextAsset(assets+L"\\"+name,"</script"); scriptHashes+=" 'sha256-"+Hash(script)+"'";
        Replace(advanced,"<script defer src=\"https://np3-assets.invalid/"+narrow+"\"></script>","<script>"+script+"</script>");
    }
    Replace(advanced,"script-src https://np3-assets.invalid;","script-src"+scriptHashes+";");
    Replace(advanced,"style-src 'unsafe-inline' https://np3-assets.invalid; font-src https://np3-assets.invalid;","style-src 'unsafe-inline'; font-src data:;");
    Replace(advanced,"<link rel=\"stylesheet\" href=\"https://np3-assets.invalid/katex.min.css\">","<style>"+FontCss(assets)+"</style>");
    const auto printStart=base.find("@media print"),printEnd=base.find("</style>",printStart);
    Replace(advanced,"</style>",base.substr(printStart,printEnd-printStart)+"</style>");
    if(advanced.size()>32*1024*1024) throw std::length_error("Export exceeds output limit"); return advanced;
}
}