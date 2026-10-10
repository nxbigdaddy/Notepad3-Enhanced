#include "../Notepad3/src/Markdown/MarkdownExport.h"
#include "../Notepad3/src/Markdown/MarkdownStandalone.h"
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
        const std::string advancedSource="# 单文件图表与公式\n\n```mermaid\nflowchart LR\n A[开始] --> B[结束]\n```\n\n行内 $E=mc^2$\n\n$$\\frac{1}{2}$$\n\n<script>window.attack=1</script>\n";
        const auto advanced=np3::RenderMarkdownStandalone(advancedSource,folder,np3::MarkdownAssetsDirectory());
        expect(advanced.find("script-src 'sha256-")!=std::string::npos && advanced.find("'unsafe-eval'")==std::string::npos,"standalone scripts use exact content hashes");
        expect(advanced.find("np3-assets.invalid")==std::string::npos && advanced.find("np3-preview.invalid")==std::string::npos,"standalone page has no virtual host dependencies");
        expect(advanced.find("data:font/woff2;base64,")!=std::string::npos && advanced.find("url(fonts/")==std::string::npos,"font dependencies are embedded");
        expect(advanced.find("<script>window.attack=1</script>")==std::string::npos,"advanced standalone document markup remains inert");
        expect(advanced.size()>5*1024*1024 && advanced.size()<32*1024*1024,"bounded actual bundled page");
        const auto advancedTarget=folder+L"\\advanced.html";
        expect(Markdown_ExportHtml(advancedSource.data(),advancedSource.size(),folder.c_str(),advancedTarget.c_str(),&error) && disk(advancedTarget)==advanced,"native export selects advanced standalone rendering");
        bool assetsMissing=false; try { np3::RenderMarkdownStandalone(advancedSource,folder,folder+L"\\missing-assets"); } catch(const std::exception&) { assetsMissing=true; }
        expect(assetsMissing,"missing advanced assets fail instead of silently exporting raw graphs");
        std::ofstream fixture("advanced-export-fixture.json",std::ios::binary);
        int pathLength=WideCharToMultiByte(CP_UTF8,0,advancedTarget.c_str(),-1,nullptr,0,nullptr,nullptr); std::string utf8Path(pathLength,0);
        WideCharToMultiByte(CP_UTF8,0,advancedTarget.c_str(),-1,utf8Path.data(),pathLength,nullptr,nullptr); utf8Path.pop_back();
        fixture << "{\"html\":\""; for(char c:utf8Path) { if(c=='\\') fixture << "\\\\"; else if(c=='\"') fixture << "\\\""; else fixture << c; } fixture << "\"}"; fixture.close();
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
