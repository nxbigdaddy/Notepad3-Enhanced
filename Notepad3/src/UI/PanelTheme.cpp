#include "PanelTheme.h"
#include "../DarkMode/DarkMode.h"
#include <commctrl.h>
#include <richedit.h>
#include <uxtheme.h>
#include <stdexcept>
#include <new>
#include <initializer_list>
namespace {
bool Class(HWND window, const wchar_t* name) {
    wchar_t value[80]{}; GetClassNameW(window, value, 80);
    return CompareStringOrdinal(value, -1, name, -1, TRUE) == CSTR_EQUAL;
}
BOOL CALLBACK Refresh(HWND window, LPARAM) {
    for (const wchar_t* name : {L"NP3BatchPanel", L"NP3AiPanel", L"NP3LogFilterPanel", L"NP3WorkspaceFileTree"})
        if (Class(window, name)) { SendMessageW(window, NP3_WM_PANEL_REFRESH, 0, 0); break; }
    return TRUE;
}
struct UpdateContext { const PANEL_COLORS* colors; HFONT font; };
BOOL CALLBACK UpdateChild(HWND window, LPARAM value) {
    auto context = reinterpret_cast<UpdateContext*>(value);
    const auto& colors = *context->colors;
#ifdef D_NP3_WIN10_DARK_MODE
    AllowDarkModeForWindowEx(window, colors.dark != FALSE);
#endif
    if (Class(window, WC_LISTVIEWW)) {
        SetWindowTheme(window, colors.dark ? L"DarkMode_Explorer" : L"Explorer", nullptr);
        ListView_SetBkColor(window, colors.field); ListView_SetTextBkColor(window, colors.field); ListView_SetTextColor(window, colors.text);
    } else if (Class(window, WC_TREEVIEWW)) {
        SetWindowTheme(window, colors.dark ? L"DarkMode_Explorer" : L"Explorer", nullptr);
        TreeView_SetBkColor(window, colors.field); TreeView_SetTextColor(window, colors.text);
    } else if (Class(window, L"Button") || Class(window, L"Edit") || Class(window, L"ComboBox") || Class(window, WC_HEADERW)) {
        SetWindowTheme(window, colors.dark ? L"DarkMode_Explorer" : L"Explorer", nullptr);
    }
    if (Class(window, L"RICHEDIT50W")) {
        CHARRANGE selection{}; POINT scroll{};
        SendMessageW(window, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&selection)); SendMessageW(window, EM_GETSCROLLPOS, 0, reinterpret_cast<LPARAM>(&scroll));
        SendMessageW(window, EM_SETBKGNDCOLOR, 0, colors.field);
        CHARFORMAT2W format{}; format.cbSize = sizeof(format); format.dwMask = CFM_COLOR | CFM_BACKCOLOR;
        format.crTextColor = colors.text; format.crBackColor = colors.field;
        SendMessageW(window, EM_SETCHARFORMAT, SCF_ALL, reinterpret_cast<LPARAM>(&format));
        SendMessageW(window, EM_SETCHARFORMAT, SCF_DEFAULT, reinterpret_cast<LPARAM>(&format));
        SendMessageW(window, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&selection)); SendMessageW(window, EM_SETSCROLLPOS, 0, reinterpret_cast<LPARAM>(&scroll));
    }
    SendMessageW(window, WM_SETFONT, reinterpret_cast<WPARAM>(context->font), FALSE);
    return TRUE;
}
}
extern "C" void PanelTheme_RefreshChildren(HWND parent) { EnumChildWindows(parent, Refresh, 0); }
namespace np3 {
PanelTheme::~PanelTheme() { if (panelBrush) DeleteObject(panelBrush); if (fieldBrush) DeleteObject(fieldBrush); if (font) DeleteObject(font); }
void PanelTheme::Update(HWND window, HWND parent) {
    PANEL_COLORS updated{};
    if (!SendMessageW(parent, NP3_WM_PANEL_COLORS, 0, reinterpret_cast<LPARAM>(&updated))) return;
    HBRUSH nextPanel = CreateSolidBrush(updated.panel), nextField = CreateSolidBrush(updated.field);
    if (!nextPanel || !nextField) { if (nextPanel) DeleteObject(nextPanel); if (nextField) DeleteObject(nextField); throw std::bad_alloc(); }
    NONCLIENTMETRICSW metrics{}; metrics.cbSize = sizeof(metrics);
    HFONT nextFont = SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0, GetDpiForWindow(parent)) ? CreateFontIndirectW(&metrics.lfMessageFont) : nullptr;
    HBRUSH oldPanel = panelBrush, oldField = fieldBrush; HFONT oldFont = font;
    colors = updated; panelBrush = nextPanel; fieldBrush = nextField; font = nextFont;
#ifdef D_NP3_WIN10_DARK_MODE
    AllowDarkModeForWindowEx(window, colors.dark != FALSE);
#endif
    UpdateContext context{&colors, font ? font : reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT))};
    EnumChildWindows(window, UpdateChild, reinterpret_cast<LPARAM>(&context));
    if (oldPanel) DeleteObject(oldPanel); if (oldField) DeleteObject(oldField); if (oldFont) DeleteObject(oldFont);
    RedrawWindow(window, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
}
bool PanelTheme::Handle(HWND window, UINT message, WPARAM wParam, LPARAM lParam, LRESULT& result) const {
    if (!panelBrush || !fieldBrush) return false;
    if (message == WM_ERASEBKGND) { RECT rect{}; GetClientRect(window, &rect); FillRect(reinterpret_cast<HDC>(wParam), &rect, panelBrush); result = 1; return true; }
    if (message == WM_CTLCOLORSTATIC || message == WM_CTLCOLORBTN || message == WM_CTLCOLOREDIT || message == WM_CTLCOLORLISTBOX) {
        HWND child = reinterpret_cast<HWND>(lParam);
        const bool field = message == WM_CTLCOLOREDIT || message == WM_CTLCOLORLISTBOX || Class(child, L"Edit") || Class(child, L"RICHEDIT50W");
        HDC dc = reinterpret_cast<HDC>(wParam);
        SetTextColor(dc, IsWindowEnabled(child) ? colors.text : colors.disabled); SetBkColor(dc, field ? colors.field : colors.panel);
        result = reinterpret_cast<LRESULT>(field ? fieldBrush : panelBrush); return true;
    }
    return false;
}
}
