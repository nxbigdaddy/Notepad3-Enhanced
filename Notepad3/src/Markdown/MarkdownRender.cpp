#include "MarkdownRender.h"
#include "vendor/md4c/md4c-html.h"
#include <limits>
#include <stdexcept>

namespace np3 {
namespace {
struct Output {
    std::string text;
    bool failed = false;
};
void Append(const MD_CHAR* text, MD_SIZE size, void* context) noexcept
{
    auto& output = *static_cast<Output*>(context);
    if (output.failed) return;
    try { output.text.append(text, size); }
    catch (...) { output.failed = true; }
}
}

std::string RenderMarkdown(std::string_view markdown)
{
    if (markdown.size() > std::numeric_limits<MD_SIZE>::max()) {
        throw std::length_error("Markdown exceeds parser limits");
    }
    Output output;
    output.text = "<!doctype html><html><head><meta charset=\"utf-8\">"
        "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
        "<meta http-equiv=\"Content-Security-Policy\" content=\"default-src 'none'; "
        "style-src 'unsafe-inline'; img-src https://np3-preview.invalid data:; "
        "base-uri https://np3-preview.invalid; form-action 'none'\">"
        "<base href=\"https://np3-preview.invalid/\">"
        "<style>html{color-scheme:light dark}body{font:15px/1.65 system-ui,sans-serif;"
        "padding:20px;max-width:900px;margin:auto;overflow-wrap:anywhere}"
        "h1,h2,h3{line-height:1.3}h1,h2{border-bottom:1px solid #8885;padding-bottom:.3em}"
        "pre{padding:12px;overflow:auto;background:#8882;border-radius:5px}"
        "code{font-family:Consolas,monospace}blockquote{border-left:3px solid #8888;"
        "padding-left:14px;margin-left:0;color:#888}table{border-collapse:collapse}"
        "td,th{border:1px solid #8886;padding:6px 12px}img{max-width:100%}"
        "a{color:#428bdd}input{pointer-events:none}</style></head><body>";
    const int result = md_html(markdown.data(), static_cast<MD_SIZE>(markdown.size()),
        Append, &output, MD_DIALECT_GITHUB | MD_FLAG_NOHTML,
        MD_HTML_FLAG_SKIP_UTF8_BOM);
    if (result != 0 || output.failed) throw std::runtime_error("Markdown rendering failed");
    output.text += "</body></html>";
    return output.text;
}
}
