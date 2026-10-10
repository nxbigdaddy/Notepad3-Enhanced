#include "../Notepad3/src/Markdown/PreviewLayout.h"
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
        const auto advanced = np3::RenderMarkdown("Inline $E=mc^2$\n\n$$\\frac{1}{2}$$\n\n```mermaid\nflowchart LR\n A --> B\n```\n\n<script>window.attack=1</script>",true);
        require(advanced.find("<x-equation>E=mc^2</x-equation>") != std::string::npos,"inline math parser node");
        require(advanced.find("<x-equation type=\"display\">") != std::string::npos,"display math parser node");
        require(advanced.find("code class=\"language-mermaid\"") != std::string::npos,"Mermaid source remains escaped code before rendering");
        require(advanced.find("script-src https://np3-assets.invalid;") != std::string::npos,"scripts restricted to separate packaged asset origin");
        require(advanced.find("font-src https://np3-assets.invalid;") != std::string::npos,"fonts restricted to packaged asset origin");
        require(advanced.find("<script>window.attack=1</script>") == std::string::npos,"advanced mode still blocks document script markup");
        require(np3::RenderMarkdown("$E=mc^2$",false).find("<x-equation>")==std::string::npos,"basic export remains script-free until advanced export integration");
        const auto bom = np3::RenderMarkdown("\xEF\xBB\xBF# BOM\n");
        require(bom.find("<h1>BOM</h1>") != std::string::npos, "UTF-8 BOM");
        require(np3::IsHostRenderNavigation(L"data:text/html;charset=utf-8,<h1>test</h1>", false, 1), "Runtime host data navigation");
        require(!np3::IsHostRenderNavigation(L"data:text/html,<script>x</script>", true, 1), "user data navigation blocked");
        require(!np3::IsHostRenderNavigation(L"data:text/html,external", false, 0), "unsolicited data navigation blocked");
        require(!np3::IsHostRenderNavigation(L"https://example.com", false, 1), "external navigation blocked");
        require(!np3::IsHostRenderNavigation(L"javascript:alert(1)", true, 1), "script navigation blocked");
        require(np3::PreviewWidth(1000, 5000) == 500, "initial equal split");
        require(np3::PreviewWidth(1000, 9500) == 840, "editor minimum width");
        require(np3::PreviewWidth(1000, 500) == 160, "preview minimum width");
        require(np3::PreviewWidth(319, 5000) == 106, "narrow window split");
        require(np3::PreviewWidth(0, 5000) == 0, "minimized window");
        require(np3::PreviewRatio(1000, 750) == 7500, "drag ratio");
        require(np3::PreviewRatio(1000, -200) == 500, "drag outside left bound");
        require(np3::PreviewRatio(1000, 2000) == 9500, "drag outside right bound");
        require(np3::PreviewRatio(0, 0) == 5000, "zero layout ratio");
        std::cout << checks << " Markdown regression checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
