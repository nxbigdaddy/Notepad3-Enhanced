#pragma once
#include <windows.h>
#include <atomic>
#include <functional>
#include <string>
#include <vector>

namespace np3 {
struct SearchFile { std::wstring path; unsigned long long size=0; FILETIME modified{}; };
struct SearchError { std::wstring path; DWORD code=0; };
struct SearchOptions {
    std::vector<std::wstring> roots;
    std::vector<std::wstring> extensions{L".md"}; // Empty means all formats.
    std::vector<std::wstring> excludedDirectories{L".git",L"node_modules",L".np3-extension-backups",L"render-assets"};
    std::wstring query;
    size_t batchSize=128, maxFiles=100000;
    bool recursive=true;
};
struct SearchSummary { size_t files=0; bool cancelled=false, limited=false; std::vector<SearchError> errors; };
// Worker-thread API. No windows, shared document state, or file contents are touched.
// Returning false from the sink cancels further enumeration.
SearchSummary SearchFiles(const SearchOptions&,const std::atomic_bool& cancel,const std::function<bool(const std::vector<SearchFile>&)>& sink);
bool ParseSearchExtensions(const std::wstring& text,std::vector<std::wstring>& extensions);
}
