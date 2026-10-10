#pragma once
#include <stddef.h>
#include <stdint.h>
#include <wchar.h>
#ifdef __cplusplus
#include <string>
#include <string_view>
namespace np3 {
std::string RenderMarkdownExport(std::string_view markdown, const std::wstring& directory);
}
extern "C" {
#endif
int Markdown_ExportHtml(const char* markdown, size_t length, const wchar_t* directory, const wchar_t* destination, uint32_t* error);
#ifdef __cplusplus
}
#endif
