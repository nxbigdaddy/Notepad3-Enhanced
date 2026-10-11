#include "MarkdownExport.h"
#include "MarkdownRender.h"
#include "MarkdownStandalone.h"
#include <system_error>
#include <windows.h>
#include <stdexcept>
#include <algorithm>
#include <climits>
namespace {
constexpr size_t InputLimit = 8 * 1024 * 1024;
constexpr size_t OutputLimit = 32 * 1024 * 1024;
std::wstring Absolute(const std::wstring& path) {
    if (path.empty() || path.size() >= 32768 || path.find(L'\0') != std::wstring::npos) throw std::invalid_argument("Invalid path");
    DWORD length = GetFullPathNameW(path.c_str(), 0, nullptr, nullptr);
    if (!length || length >= 32768) throw std::invalid_argument("Invalid path");
    std::wstring result(length, L'\0');
    DWORD written = GetFullPathNameW(path.c_str(), length, result.data(), nullptr);
    if (!written || written >= length) throw std::invalid_argument("Invalid path");
    result.resize(written); return result;
}
std::wstring NativePath(const std::wstring& path) {
    auto full = Absolute(path);
    if (full.rfind(L"\\\\?\\", 0) == 0) return full;
    return full.rfind(L"\\\\", 0) == 0 ? L"\\\\?\\UNC\\" + full.substr(2) : L"\\\\?\\" + full;
}
std::string DirectoryUri(const std::wstring& directory) {
    auto full = Absolute(directory);
    if (full.rfind(L"\\\\?\\UNC\\", 0) == 0) full = L"\\\\" + full.substr(8);
    else if (full.rfind(L"\\\\?\\", 0) == 0) full = full.substr(4);
    std::replace(full.begin(), full.end(), L'\\', L'/');
    if (full.back() != L'/') full += L'/';
    int length = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, full.data(), static_cast<int>(full.size()), nullptr, 0, nullptr, nullptr);
    if (!length) throw std::invalid_argument("Invalid Unicode path");
    std::string utf8(length, '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, full.data(), static_cast<int>(full.size()), utf8.data(), length, nullptr, nullptr);
    std::string uri = full.rfind(L"//", 0) == 0 ? "file:" : "file:///";
    constexpr char hex[] = "0123456789ABCDEF";
    for (unsigned char c : utf8) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '/' || c == ':' || c == '-' || c == '_' || c == '.' || c == '~') uri += static_cast<char>(c);
        else { uri += '%'; uri += hex[c >> 4]; uri += hex[c & 15]; }
    }
    return uri;
}
void Replace(std::string& text, const std::string& before, const std::string& after) {
    auto position = text.find(before);
    if (position == std::string::npos) throw std::runtime_error("Renderer template changed");
    text.replace(position, before.size(), after);
}
struct Handle { HANDLE value = INVALID_HANDLE_VALUE; ~Handle() { if (value != INVALID_HANDLE_VALUE) CloseHandle(value); } };
}
namespace np3 {
std::string RenderMarkdownExport(std::string_view markdown, const std::wstring& directory) {
    if (markdown.size() > InputLimit) throw std::length_error("Export exceeds 8 MiB limit");
    if (!markdown.empty() && !MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, markdown.data(), static_cast<int>(markdown.size()), nullptr, 0)) throw std::invalid_argument("Invalid UTF-8 document");
    const auto uri = DirectoryUri(directory);
    auto html = RenderMarkdown(markdown);
    Replace(html, "img-src https://np3-preview.invalid data:", "img-src file: data:");
    Replace(html, "base-uri https://np3-preview.invalid", "base-uri file:");
    Replace(html, "<base href=\"https://np3-preview.invalid/\">", "<base href=\"" + uri + "\">");
    Replace(html, "</style>", "@media print{html{color-scheme:light}body{color:#111;background:#fff;padding:0;max-width:none}pre{white-space:pre-wrap;overflow-wrap:anywhere}table,pre,blockquote,.katex-display{break-inside:avoid}.np3-diagram{break-inside:auto}.np3-diagram svg{display:block;max-height:235mm;width:auto;max-width:100%}h1,h2,h3,h4{break-after:avoid}a{color:inherit}}@page{margin:18mm}</style>");
    if (html.size() > OutputLimit) throw std::length_error("Export exceeds 32 MiB output limit");
    return html;
}
}
extern "C" int Markdown_ExportHtml(const char* markdown, size_t length, const wchar_t* directory, const wchar_t* destination, uint32_t* error) {
    uint32_t localError = 0; if (!error) error = &localError; *error = 0;
    if ((!markdown && length) || !directory || !destination) { *error = ERROR_INVALID_PARAMETER; return 0; }
    std::wstring temporary;
    bool ownsTemporary = false;
    try {
        auto html = np3::RenderMarkdownStandalone(std::string_view(markdown ? markdown : "", length), directory, np3::MarkdownAssetsDirectory());
        auto target = NativePath(destination);
        temporary = target + L".tmp-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetCurrentThreadId()) + L"-" + std::to_wstring(GetTickCount64());
        {
            Handle file; file.value = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, nullptr);
            if (file.value == INVALID_HANDLE_VALUE) { *error = GetLastError(); return 0; }
            ownsTemporary = true;
            DWORD written = 0;
            if (!WriteFile(file.value, html.data(), static_cast<DWORD>(html.size()), &written, nullptr) || written != html.size() || !FlushFileBuffers(file.value)) *error = GetLastError() ? GetLastError() : ERROR_WRITE_FAULT;
        }
        if (!*error && !MoveFileExW(temporary.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) *error = GetLastError();
        if (*error) DeleteFileW(temporary.c_str());
        return *error == 0;
    } catch (const std::system_error& exception) { *error = static_cast<uint32_t>(exception.code().value()); }
    catch (const std::bad_alloc&) { *error = ERROR_NOT_ENOUGH_MEMORY; }
    catch (const std::length_error&) { *error = ERROR_FILE_TOO_LARGE; }
    catch (...) { *error = ERROR_INVALID_DATA; }
    if (ownsTemporary) DeleteFileW(temporary.c_str());
    return 0;
}
