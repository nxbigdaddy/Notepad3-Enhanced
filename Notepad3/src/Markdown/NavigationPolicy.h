#pragma once
#include <string_view>
namespace np3 {
inline bool IsHostRenderNavigation(std::wstring_view uri, bool userInitiated, unsigned pendingRenders)
{
    return pendingRenders && !userInitiated && uri.substr(0, 14) == L"data:text/html";
}
}
