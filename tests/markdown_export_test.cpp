#include "../Notepad3/src/Markdown/MarkdownExport.h"
#include <windows.h>
#include <fstream>
#include <iterator>
#include <cstdio>
#include <stdexcept>
int checks = 0;
void expect(bool condition, const char* label) { ++checks; if (!condition) throw std::runtime_error(label); }
std::string disk(const std::wstring& path) { std::ifstream input(path, std::ios::binary); return { std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>() }; }
int main() {
    try {
        wchar_t temp[MAX_PATH]; GetTempPathW(MAX_PATH, temp);
        std::wstring folder = std::wstring(temp) + L"np3-export-中文 & #-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64());
        expect(CreateDirectoryW(folder.c_str(), nullptr) != FALSE, "isolated Unicode export directory");
        std::string markdown = "# 导出测试\n\n| A | B |\n|---|---|\n| 1 | 2 |\n\n```cpp\nif (x < y) {}\n```\n\n![local](images/a.png)\n\n<script>alert(1)</script>\n";
        auto html = np3::RenderMarkdownExport(markdown, folder);
        expect(html.find("<h1>导出测试</h1>") != std::string::npos, "Unicode heading rendered");
        expect(html.find("<table>") != std::string::npos && html.find("<code class=\"language-cpp\">") != std::string::npos, "GFM table and code block preserved");
        expect(html.find("if (x &lt; y)") != std::string::npos, "code escaped");
        expect(html.find("<script>") == std::string::npos && html.find("&lt;script&gt;") != std::string::npos, "raw HTML remains inert");
        expect(html.find("img-src file: data:") != std::string::npos && html.find("default-src 'none'") != std::string::npos, "export CSP allows only local images");
        expect(html.find("np3-preview.invalid") == std::string::npos && html.find("<base href=\"file:///") != std::string::npos, "export independent of preview virtual host");
        expect(html.find("%E4%B8%AD%E6%96%87%20%26%20%23") != std::string::npos && html.find("src=\"images/a.png\"") != std::string::npos, "Unicode reserved path characters encoded and image path retained");
        expect(html.find("@media print") != std::string::npos && html.find("@page{margin:18mm}") != std::string::npos, "print styles present");
        auto unc = np3::RenderMarkdownExport("text", L"\\\\server\\share\\a & 中");
        expect(unc.find("file://server/share/a%20%26%20%E4%B8%AD/") != std::string::npos, "UNC source path URI");
        std::wstring target = folder + L"\\导出.html"; uint32_t error = 0;
        expect(Markdown_ExportHtml(markdown.data(), markdown.size(), folder.c_str(), target.c_str(), &error) && !error, "actual UTF-8 atomic file export");
        const auto original = disk(target);
        expect(original == html && original.substr(0, 15) == "<!doctype html>", "saved HTML bytes match rendering without BOM");
        HANDLE locked = CreateFileW(target.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
        expect(locked != INVALID_HANDLE_VALUE, "hold replacement lock");
        expect(!Markdown_ExportHtml("changed", 7, folder.c_str(), target.c_str(), &error) && error && disk(target) == original, "failed replacement retains earlier export");
        CloseHandle(locked);
        expect(!Markdown_ExportHtml("\xC0", 1, folder.c_str(), target.c_str(), &error) && error == ERROR_INVALID_DATA && disk(target) == original, "invalid UTF-8 retains earlier export");
        std::string oversized(8 * 1024 * 1024 + 1, 'x');
        expect(!Markdown_ExportHtml(oversized.data(), oversized.size(), folder.c_str(), target.c_str(), &error) && error == ERROR_FILE_TOO_LARGE && disk(target) == original, "input limit rejects without truncation");
        std::string expansion(8 * 1024 * 1024, '&');
        expect(!Markdown_ExportHtml(expansion.data(), expansion.size(), folder.c_str(), target.c_str(), &error) && error == ERROR_FILE_TOO_LARGE && disk(target) == original, "expanded output limit retains existing export");
        expect(!Markdown_ExportHtml(nullptr, 1, folder.c_str(), target.c_str(), &error) && error == ERROR_INVALID_PARAMETER, "null buffer rejected");
        expect(Markdown_ExportHtml("updated", 7, folder.c_str(), target.c_str(), &error) && disk(target).find("<p>updated</p>") != std::string::npos, "replace existing output successfully");
        auto missing = folder + L"\\missing\\export.html";
        expect(!Markdown_ExportHtml("x", 1, folder.c_str(), missing.c_str(), &error) && error != 0, "missing destination folder reports failure");
        // Keep an inspectable full-content artifact alongside replacement test.
        auto sample = folder + L"\\sample.html";
        expect(Markdown_ExportHtml(markdown.data(), markdown.size(), folder.c_str(), sample.c_str(), &error), "retain inspectable export sample");
        int size = WideCharToMultiByte(CP_UTF8, 0, folder.c_str(), -1, nullptr, 0, nullptr, nullptr);
        std::string evidence(size, '\0'); WideCharToMultiByte(CP_UTF8, 0, folder.c_str(), -1, evidence.data(), size, nullptr, nullptr);
        std::printf("%d Markdown export checks passed; evidence: %s\n", checks, evidence.c_str());
        return 0;
    } catch (const std::exception& error) { std::fprintf(stderr, "FAIL %s\n", error.what()); return 1; }
}
