#include "../Notepad3/src/Markdown/MarkdownRender.h"
#include "../Notepad3/src/Markdown/NavigationPolicy.h"
#include <iostream>
#include <stdexcept>
#include <string>
int main()
{
    int checks = 0;
    auto require = [&](bool condition, const char* name) {
        if (!condition) throw std::runtime_error(name);
        ++checks;
    };
    try {
        const auto html = np3::RenderMarkdown(
            "# 中文标题\n\n**bold** & text\n\n"
            "| A | B |\n|---|---|\n| 1 | 2 |\n\n"
            "- [x] done\n- [ ] pending\n\n"
            "```cpp\nif (a < b) {}\n```\n\n![image](images/a.png)\n");
        require(html.find("<h1>中文标题</h1>") != std::string::npos, "UTF-8 heading");
        require(html.find("<strong>bold</strong>") != std::string::npos, "emphasis");
        require(html.find("&amp; text") != std::string::npos, "entity escaping");
        require(html.find("<table>") != std::string::npos, "GFM tables");
        require(html.find("type=\"checkbox\"") != std::string::npos, "task lists");
        require(html.find("a &lt; b") != std::string::npos, "code escaping");
        require(html.find("src=\"images/a.png\"") != std::string::npos, "relative image");
        const auto hostile = np3::RenderMarkdown("<script>alert(1)</script>\n\n<img src=x onerror=alert(2)>\n");
        require(hostile.find("<script>") == std::string::npos, "raw script blocked");
        require(hostile.find("<img src=x") == std::string::npos, "raw HTML blocked");
        require(hostile.find("default-src 'none'") != std::string::npos, "CSP");
        require(np3::RenderMarkdown("").find("</body></html>") != std::string::npos, "empty document");
        const auto bom = np3::RenderMarkdown("\xEF\xBB\xBF# BOM\n");
        require(bom.find("<h1>BOM</h1>") != std::string::npos, "UTF-8 BOM");
        require(np3::IsHostRenderNavigation(L"data:text/html;charset=utf-8,<h1>test</h1>", false, 1), "Runtime host data navigation");
        require(!np3::IsHostRenderNavigation(L"data:text/html,<script>x</script>", true, 1), "user data navigation blocked");
        require(!np3::IsHostRenderNavigation(L"data:text/html,external", false, 0), "unsolicited data navigation blocked");
        require(!np3::IsHostRenderNavigation(L"https://example.com", false, 1), "external navigation blocked");
        require(!np3::IsHostRenderNavigation(L"javascript:alert(1)", true, 1), "script navigation blocked");
        std::cout << checks << " Markdown regression checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
