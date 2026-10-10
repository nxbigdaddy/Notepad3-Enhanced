#pragma once
#include <string>
#include <string_view>
namespace np3 {
std::wstring MarkdownAssetsDirectory();
std::string RenderMarkdownStandalone(std::string_view markdown, const std::wstring& directory, const std::wstring& assets);
}