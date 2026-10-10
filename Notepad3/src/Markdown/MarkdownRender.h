#pragma once
#include <string>
#include <string_view>

namespace np3 {
// Markdown is UTF-8. Raw HTML is rendered as text; the page has no scripts.
std::string RenderMarkdown(std::string_view markdown);
}
