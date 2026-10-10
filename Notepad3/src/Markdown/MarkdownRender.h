#pragma once
#include <string>
#include <string_view>

namespace np3 {
// Markdown is UTF-8. Raw HTML stays text. Advanced pages load only packaged assets.
std::string RenderMarkdown(std::string_view markdown, bool advanced = false);
}
