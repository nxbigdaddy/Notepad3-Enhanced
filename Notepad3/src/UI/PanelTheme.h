#pragma once
#include <windows.h>
#define NP3_WM_PANEL_COLORS (WM_APP + 0x360)
#define NP3_WM_PANEL_REFRESH (WM_APP + 0x361)
#define NP3_WM_EDITOR_STYLE_CHANGED (WM_APP + 0x362)
typedef struct PANEL_COLORS {
    BOOL dark;
    COLORREF panel, field, text, disabled, added, removed, highlightText;
} PANEL_COLORS;
#ifdef __cplusplus
extern "C" {
#endif
void PanelTheme_RefreshChildren(HWND parent);
#ifdef __cplusplus
}
namespace np3 {
class PanelTheme {
    HBRUSH panelBrush = nullptr, fieldBrush = nullptr;
    HFONT font = nullptr;
    PANEL_COLORS colors{};
public:
    PanelTheme() = default;
    PanelTheme(const PanelTheme&) = delete;
    PanelTheme& operator=(const PanelTheme&) = delete;
    ~PanelTheme();
    void Update(HWND window, HWND parent);
    bool Handle(HWND window, UINT message, WPARAM wParam, LPARAM lParam, LRESULT& result) const;
    const PANEL_COLORS& Colors() const { return colors; }
};
}
#endif
