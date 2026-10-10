#include "LogFilterPanel.h"
#include <commctrl.h>
#include <algorithm>
#include <atomic>
#include <memory>
#include <string>
#include <thread>
namespace {
constexpr wchar_t ClassName[] = L"NP3LogFilterPanel";
struct Job {
    std::string text, pattern;
    unsigned long long id = 0, version = 0;
    LOG_FILTER_OPTIONS options = {};
    LOG_FILTER_RESULT result = {};
    std::atomic<bool> cancel{false}, done{false};
    int status = LOG_FILTER_OK;
    ~Job() { LogFilter_Free(&result); }
};
struct Panel {
    HWND parent = nullptr, window = nullptr, edit = nullptr, list = nullptr, status = nullptr, run = nullptr, cancel = nullptr;
    std::wstring labels[LP_COUNT];
    std::shared_ptr<Job> job;
    bool published = false;
};
std::shared_ptr<Panel> State(HWND window) {
    auto* holder = reinterpret_cast<std::shared_ptr<Panel>*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    return holder ? *holder : nullptr;
}
HWND Find(HWND parent) { return FindWindowExW(parent, nullptr, ClassName, nullptr); }
int Canceled(void* context) { return static_cast<Job*>(context)->cancel.load() ? 1 : 0; }
void Status(const std::shared_ptr<Panel>& panel, int label) { SetWindowTextW(panel->status, panel->labels[label].c_str()); }
void Publish(const std::shared_ptr<Panel>& panel) {
    auto job = panel->job;
    if (!job || !job->done.load() || panel->published) return;
    panel->published = true;
    EnableWindow(panel->run, TRUE); EnableWindow(panel->cancel, FALSE);
    if (job->status == LOG_FILTER_OK) {
        ListView_SetItemCountEx(panel->list, static_cast<int>(job->result.count), LVSICF_NOSCROLL);
        auto message = panel->labels[LP_DONE] + L" " + std::to_wstring(job->result.count);
        SetWindowTextW(panel->status, message.c_str());
    } else if (job->status == LOG_FILTER_INVALID_PATTERN) {
        auto message = panel->labels[LP_PATTERN_ERROR] + L" " + std::to_wstring(job->result.errorOffset + 1);
        SetWindowTextW(panel->status, message.c_str());
    } else Status(panel, job->status == LOG_FILTER_LIMIT ? LP_LIMIT : job->status == LOG_FILTER_CANCELED ? LP_CANCELED : LP_ERROR);
}
void Activate(const std::shared_ptr<Panel>& panel) {
    if (!panel->job || !panel->published || panel->job->status != LOG_FILTER_OK) return;
    int index = ListView_GetNextItem(panel->list, -1, LVNI_SELECTED);
    if (index < 0 || static_cast<size_t>(index) >= panel->job->result.count) return;
    LOG_PANEL_LOCATION location{panel->job->id, panel->job->version, panel->job->result.rows[index]};
    if (!SendMessageW(panel->parent, NP3_WM_LOGFILTER_LOCATE, 0, reinterpret_cast<LPARAM>(&location))) Status(panel, LP_STALE);
}
LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    auto panel = State(window);
    try {
    if (message == WM_TIMER && panel) { Publish(panel); return 0; }
    if (message == WM_COMMAND && panel) {
        if (LOWORD(wParam) == 105) SendMessageW(panel->parent, NP3_WM_LOGFILTER_CAPTURE, 0, 0);
        else if (LOWORD(wParam) == 106 && panel->job) panel->job->cancel = true;
        return 0;
    }
    if (message == WM_SIZE && panel) {
        int width = LOWORD(lParam), height = HIWORD(lParam), row = MulDiv(28, GetDpiForWindow(window), 96);
        int button = MulDiv(82, GetDpiForWindow(window), 96);
        MoveWindow(panel->edit, 2, 2, (std::max)(0, width - 2 * button - 8), row - 4, TRUE);
        MoveWindow(panel->run, (std::max)(0, width - 2 * button - 4), 2, button, row - 4, TRUE);
        MoveWindow(panel->cancel, (std::max)(0, width - button - 2), 2, button, row - 4, TRUE);
        for (int i = 0; i < 3; ++i) MoveWindow(GetDlgItem(window, 102 + i), 2 + i * width / 3, row, (std::max)(0, width / 3 - 4), row, TRUE);
        MoveWindow(panel->status, 2, 2 * row, (std::max)(0, width - 4), row, TRUE);
        MoveWindow(panel->list, 0, 3 * row, width, (std::max)(0, height - 3 * row), TRUE);
        ListView_SetColumnWidth(panel->list, 1, (std::max)(80, width - 90)); return 0;
    }
    if (message == WM_NOTIFY && panel) {
        auto* notify = reinterpret_cast<NMHDR*>(lParam);
        if (notify->hwndFrom != panel->list) return 0;
        if (notify->code == NM_DBLCLK || notify->code == NM_RETURN) { Activate(panel); return 0; }
        if (notify->code == LVN_GETDISPINFOW && panel->published && panel->job && panel->job->status == LOG_FILTER_OK) {
            auto* display = reinterpret_cast<NMLVDISPINFOW*>(lParam);
            int index = display->item.iItem;
            if (!(display->item.mask & LVIF_TEXT) || index < 0 || static_cast<size_t>(index) >= panel->job->result.count) return 0;
            const auto& row = panel->job->result.rows[index];
            std::wstring value;
            if (display->item.iSubItem == 0) value = std::to_wstring(row.line + 1);
            else {
                int length = static_cast<int>((std::min)(row.length, size_t(2048)));
                const char* text = panel->job->text.data() + row.offset;
                int size = MultiByteToWideChar(CP_UTF8, 0, text, length, nullptr, 0);
                value.resize(size); if (size) MultiByteToWideChar(CP_UTF8, 0, text, length, value.data(), size);
            }
            if (display->item.pszText && display->item.cchTextMax > 0) wcsncpy_s(display->item.pszText, display->item.cchTextMax, value.c_str(), _TRUNCATE);
            return 0;
        }
    }
    if (message == WM_NCDESTROY && panel) {
        if (panel->job) panel->job->cancel = true;
        KillTimer(window, 1);
        auto* holder = reinterpret_cast<std::shared_ptr<Panel>*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        SetWindowLongPtrW(window, GWLP_USERDATA, 0); delete holder;
    }
    return DefWindowProcW(window, message, wParam, lParam);
    } catch (...) { if (panel) Status(panel, LP_ERROR); return 0; }
}
}
extern "C" BOOL LogPanel_IsOpen(HWND parent) { return Find(parent) != nullptr; }
extern "C" BOOL LogPanel_IsBusy(HWND parent) { auto panel = State(Find(parent)); return panel && panel->job && (!panel->job->done.load() || !panel->published); }
extern "C" int LogPanel_Layout(HWND parent, int x, int y, int width, int height) {
    HWND window = Find(parent); if (!window) return 0;
    int reserved = (std::min)((std::max)(0, height / 2), MulDiv(260, GetDpiForWindow(parent), 96));
    MoveWindow(window, x, y + height - reserved, width, reserved, TRUE); return reserved;
}
extern "C" BOOL LogPanel_Run(HWND parent, const char* text, size_t length, unsigned long long id, unsigned long long version) {
    auto panel = State(Find(parent)); if (!panel || LogPanel_IsBusy(parent)) return FALSE;
    try {
        if (length > 8 * 1024 * 1024) { Status(panel, LP_LIMIT); return FALSE; }
        auto job = std::make_shared<Job>(); job->text.assign(text ? text : "", length); job->id = id; job->version = version;
        wchar_t pattern[4097]; int chars = GetWindowTextW(panel->edit, pattern, 4097);
        int bytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, pattern, chars, nullptr, 0, nullptr, nullptr);
        if (bytes > 4096 || (chars && !bytes)) { Status(panel, LP_LIMIT); return FALSE; }
        job->pattern.resize(bytes); if (bytes) WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, pattern, chars, job->pattern.data(), bytes, nullptr, nullptr);
        job->options.regex = SendMessageW(GetDlgItem(panel->window, 102), BM_GETCHECK, 0, 0) == BST_CHECKED;
        job->options.caseSensitive = SendMessageW(GetDlgItem(panel->window, 103), BM_GETCHECK, 0, 0) == BST_CHECKED;
        job->options.invert = SendMessageW(GetDlgItem(panel->window, 104), BM_GETCHECK, 0, 0) == BST_CHECKED;
        job->options.cancel = Canceled; job->options.context = job.get();
        ListView_SetItemCountEx(panel->list, 0, 0); panel->published = false; panel->job = job;
        std::thread([job] {
            job->status = LogFilter_Run(job->text.data(), job->text.size(), job->pattern.data(), job->pattern.size(), &job->options, &job->result);
            job->done = true;
        }).detach();
        EnableWindow(panel->run, FALSE); EnableWindow(panel->cancel, TRUE); Status(panel, LP_RUNNING); return TRUE;
    } catch (...) { panel->job.reset(); EnableWindow(panel->run, TRUE); EnableWindow(panel->cancel, FALSE); Status(panel, LP_ERROR); return FALSE; }
}
extern "C" void LogPanel_Toggle(HWND parent, const wchar_t* const* labels) {
    try {
        if (HWND old = Find(parent)) { DestroyWindow(old); }
        else {
            auto panel = std::make_shared<Panel>(); panel->parent = parent;
            for (int i = 0; i < LP_COUNT; ++i) panel->labels[i] = labels[i];
            WNDCLASSW type = {}; type.lpfnWndProc = WindowProc; type.hInstance = GetModuleHandleW(nullptr); type.lpszClassName = ClassName; type.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1); RegisterClassW(&type);
            HWND window = CreateWindowExW(WS_EX_CLIENTEDGE, ClassName, nullptr, WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, parent, nullptr, type.hInstance, nullptr);
            if (!window) return;
            auto* holder = new std::shared_ptr<Panel>(panel); SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(holder)); panel->window = window;
            auto control = [&](const wchar_t* name, const wchar_t* text, DWORD style, int id) {
                HWND child = CreateWindowExW(0, name, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 0, 0, window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), type.hInstance, nullptr);
                if (!child) throw std::bad_alloc();
                SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), FALSE); return child;
            };
            panel->edit = control(L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP, 101); SendMessageW(panel->edit, EM_SETLIMITTEXT, 4096, 0);
            for (int i = 0; i < 3; ++i) control(L"BUTTON", panel->labels[i].c_str(), BS_AUTOCHECKBOX | WS_TABSTOP, 102 + i);
            SendMessageW(GetDlgItem(window, 103), BM_SETCHECK, BST_CHECKED, 0);
            panel->run = control(L"BUTTON", panel->labels[LP_RUN].c_str(), BS_PUSHBUTTON | WS_TABSTOP, 105);
            panel->cancel = control(L"BUTTON", panel->labels[LP_CANCEL].c_str(), BS_PUSHBUTTON | WS_TABSTOP, 106); EnableWindow(panel->cancel, FALSE);
            panel->status = control(L"STATIC", L"", 0, 108);
            panel->list = control(WC_LISTVIEWW, L"", LVS_REPORT | LVS_OWNERDATA | LVS_SINGLESEL | WS_TABSTOP | WS_BORDER, 107);
            ListView_SetExtendedListViewStyle(panel->list, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
            for (int i = 0; i < 2; ++i) { LVCOLUMNW column = {}; column.mask = LVCF_TEXT | LVCF_WIDTH; column.pszText = const_cast<wchar_t*>(panel->labels[LP_LINE + i].c_str()); column.cx = i ? 400 : 80; ListView_InsertColumn(panel->list, i, &column); }
            if (!SetTimer(window, 1, 50, nullptr)) throw std::bad_alloc();
        }
        RECT rect; GetClientRect(parent, &rect); SendMessageW(parent, WM_SIZE, 0, MAKELPARAM(rect.right, rect.bottom));
    } catch (...) { if (HWND window = Find(parent)) DestroyWindow(window); }
}
