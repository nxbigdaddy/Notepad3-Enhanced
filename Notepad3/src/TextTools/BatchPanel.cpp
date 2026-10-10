#include "BatchPanel.h"
#include "BatchTransform.h"
#include <commctrl.h>
#include <richedit.h>
#include <algorithm>
#include <atomic>
#include <memory>
#include <stdexcept>
#include <thread>
#include <shobjidl.h>
#include <wrl.h>

namespace {
using namespace np3::batch;
constexpr wchar_t ClassName[] = L"NP3BatchPanel";
struct Job {
    std::vector<Source> sources;
    std::vector<Proposal> proposals;
    Options options;
    std::atomic<bool> cancel{false}, done{false};
    bool error = false;
};
struct Panel {
    HWND window = nullptr, parent = nullptr;
    HMODULE richEdit = nullptr;
    std::wstring labels[BP_COUNT];
    std::shared_ptr<Job> job;
    bool published = false, applied = false, invalidated = false;
    ~Panel() { if (richEdit) FreeLibrary(richEdit); }
};
using Owner = std::shared_ptr<Panel>;
Owner State(void* context) { return context ? *static_cast<Owner*>(context) : nullptr; }
Owner WindowState(HWND window) { return State(reinterpret_cast<void*>(GetWindowLongPtrW(window, GWLP_USERDATA))); }
std::wstring Wide(const std::string& text) {
    if (text.empty()) return {};
    int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (!count) throw std::runtime_error("UTF8");
    std::wstring result(count, 0); MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), result.data(), count); return result;
}
std::string EditText(HWND control) {
    std::wstring text(static_cast<size_t>(GetWindowTextLengthW(control)) + 1, 0);
    GetWindowTextW(control, text.data(), static_cast<int>(text.size()));
    int count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (!count) throw std::runtime_error("UTF16");
    std::string result(count, 0); WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.c_str(), -1, result.data(), count, nullptr, nullptr); result.pop_back(); return result;
}
void StatusText(const Owner& panel, int label) { SetWindowTextW(GetDlgItem(panel->window, 130), panel->labels[label].c_str()); }
bool BusyJob(const Owner& panel) { return panel && panel->job && (!panel->job->done.load() || !panel->published); }
void Enable(const Owner& panel) {
    const bool busy = BusyJob(panel);
    for (int id : {101, 102, 103, 110, 113}) EnableWindow(GetDlgItem(panel->window, id), !busy);
    EnableWindow(GetDlgItem(panel->window, 111), busy);
    EnableWindow(GetDlgItem(panel->window, 112), !busy && panel->job && !panel->job->error && !panel->job->cancel && !panel->applied && !panel->invalidated);
    const bool replace = SendMessageW(GetDlgItem(panel->window, 101), CB_GETCURSEL, 0, 0) == 0;
    EnableWindow(GetDlgItem(panel->window, 102), !busy && replace);
    EnableWindow(GetDlgItem(panel->window, 103), !busy && replace);
}
int Label(Status status) {
    switch (status) {
    case Status::Ready: return BP_DONE;
    case Status::Unchanged: return BP_UNCHANGED;
    case Status::ReadOnly: return BP_READONLY;
    case Status::Limit: return BP_LIMIT;
    case Status::InvalidJson: return BP_JSONERROR;
    case Status::Canceled: return BP_CANCELED;
    default: return BP_INVALID;
    }
}
void Select(const Owner& panel, int index) {
    if (!panel->job || !panel->published || index < 0 || static_cast<size_t>(index) >= panel->job->proposals.size()) return;
    const auto& p = panel->job->proposals[index];
    SetWindowTextW(GetDlgItem(panel->window, 121), Wide(p.source.text).c_str());
    SetWindowTextW(GetDlgItem(panel->window, 122), Wide(p.result).c_str());
    auto text = panel->labels[Label(p.status)];
    if (p.status == Status::Ready) text += L" (" + std::to_wstring(p.source.text.size()) + L" → " + std::to_wstring(p.result.size()) + L" bytes)";
    if (p.status == Status::InvalidJson) text += L" (byte " + std::to_wstring(p.errorOffset) + L")";
    SetWindowTextW(GetDlgItem(panel->window, 130), text.c_str());
}
void Publish(const Owner& panel) {
    auto job = panel->job;
    if (!job || !job->done || panel->published) return;
    panel->published = true;
    HWND list = GetDlgItem(panel->window, 120);
    ListView_DeleteAllItems(list);
    if (!job->error) {
        for (size_t i = 0; i < job->proposals.size(); ++i) {
            auto& p = job->proposals[i];
            LVITEMW item{}; item.mask = LVIF_TEXT; item.iItem = static_cast<int>(i); item.pszText = p.source.label.data();
            if (ListView_InsertItem(list, &item) < 0) throw std::bad_alloc();
            ListView_SetItemText(list, static_cast<int>(i), 1, panel->labels[Label(p.status)].data());
            ListView_SetCheckState(list, static_cast<int>(i), p.status == Status::Ready && !job->cancel);
        }
    }
    StatusText(panel, job->error ? BP_ERROR : job->cancel ? BP_CANCELED : BP_DONE);
    Enable(panel);
    if (!job->proposals.empty()) { ListView_SetItemState(list, 0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED); Select(panel, 0); }
}
void Apply(const Owner& panel) {
    if (!panel->job || BusyJob(panel) || panel->applied || panel->invalidated || panel->job->error || panel->job->cancel) return;
    std::vector<BATCH_CHANGE> changes;
    for (size_t i = 0; i < panel->job->proposals.size(); ++i) {
        if (!ListView_GetCheckState(GetDlgItem(panel->window, 120), static_cast<int>(i))) continue;
        const auto& p = panel->job->proposals[i];
        if (p.status != Status::Ready) { StatusText(panel, BP_INVALID); return; }
        changes.push_back({p.source.id, p.source.version, p.source.text.data(), p.source.text.size(), p.result.data(), p.result.size()});
    }
    if (changes.empty()) { StatusText(panel, BP_UNCHANGED); return; }
    const BATCH_APPLY request{changes.data(), changes.size()};
    LRESULT result = SendMessageW(panel->parent, NP3_WM_BATCH_APPLY, 0, reinterpret_cast<LPARAM>(&request));
    if (result == 1) panel->applied = true;
    StatusText(panel, result == 1 ? BP_APPLIED : result == 0 ? BP_STALE : BP_ERROR); Enable(panel);
}
void ChooseFiles(const Owner& panel) {
    Microsoft::WRL::ComPtr<IFileOpenDialog> dialog;
    HRESULT result = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog));
    if (FAILED(result)) { StatusText(panel, BP_ADDERROR); return; }
    DWORD flags = 0;
    if (FAILED(dialog->GetOptions(&flags)) || FAILED(dialog->SetOptions(flags | FOS_ALLOWMULTISELECT | FOS_FORCEFILESYSTEM | FOS_FILEMUSTEXIST | FOS_PATHMUSTEXIST))) { StatusText(panel, BP_ADDERROR); return; }
    dialog->SetTitle(panel->labels[BP_ADD].c_str());
    result = dialog->Show(panel->parent);
    if (!panel->window || !IsWindow(panel->parent)) return;
    if (result == HRESULT_FROM_WIN32(ERROR_CANCELLED)) return;
    if (FAILED(result)) { StatusText(panel, BP_ADDERROR); return; }
    Microsoft::WRL::ComPtr<IShellItemArray> items;
    DWORD count = 0;
    if (FAILED(dialog->GetResults(&items)) || FAILED(items->GetCount(&count)) || !count) { StatusText(panel, BP_ADDERROR); return; }
    if (count > 64) { StatusText(panel, BP_LIMIT); return; }
    std::vector<std::wstring> paths; paths.reserve(count);
    for (DWORD i = 0; i < count; ++i) {
        Microsoft::WRL::ComPtr<IShellItem> item; PWSTR raw = nullptr;
        if (FAILED(items->GetItemAt(i, &item)) || FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &raw))) { StatusText(panel, BP_ADDERROR); return; }
        std::unique_ptr<wchar_t, decltype(&CoTaskMemFree)> owned(raw, &CoTaskMemFree);
        if (!raw || wcsnlen(raw, 32768) >= 32768) { StatusText(panel, BP_ADDERROR); return; }
        paths.emplace_back(raw);
    }
    std::vector<const wchar_t*> pointers; pointers.reserve(paths.size());
    for (const auto& path : paths) pointers.push_back(path.c_str());
    panel->invalidated = true; Enable(panel);
    const BATCH_FILES request{pointers.data(), pointers.size()};
    result = static_cast<HRESULT>(SendMessageW(panel->parent, NP3_WM_BATCH_OPEN, 0, reinterpret_cast<LPARAM>(&request)));
    StatusText(panel, result == 1 ? BP_ADDED : result == -2 ? BP_LIMIT : BP_ADDERROR);
}
LRESULT CALLBACK Proc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    auto panel = WindowState(window);
    try {
        if (panel && message == WM_TIMER) { Publish(panel); return 0; }
        if (panel && message == WM_SIZE) {
            const int dpi = static_cast<int>(GetDpiForWindow(panel->parent));
            const auto scale = [dpi](int value) { return MulDiv(value, dpi, 96); };
            RECT rect{}; GetClientRect(window, &rect); const int gap = scale(6), width = rect.right, height = rect.bottom;
            auto move = [&](int id, int x, int y, int w, int h) { MoveWindow(GetDlgItem(window, id), x, y, (std::max)(0, w), (std::max)(0, h), TRUE); };
            const int unit = (std::max)(0, (width - 5 * gap) / 4);
            move(101, gap, gap, unit, scale(200));
            move(102, 2 * gap + unit, gap, unit, scale(24)); move(103, 3 * gap + 2 * unit, gap, unit, scale(24));
            move(110, 4 * gap + 3 * unit, gap, unit, scale(24));
            move(111, gap, scale(36), scale(95), scale(24)); move(112, scale(108), scale(36), scale(155), scale(24));
            move(113, scale(270), scale(36), scale(140), scale(24));
            move(130, scale(416), scale(36), width - scale(422), scale(24));
            const int top = scale(68), left = (std::max)(0, width / 3), half = (std::max)(0, (width - left - 3 * gap) / 2);
            move(120, gap, top, left - gap, height - top - gap);
            move(201, left + gap, top, half, scale(20)); move(202, left + 2 * gap + half, top, half, scale(20));
            move(121, left + gap, top + scale(22), half, height - top - scale(22) - gap);
            move(122, left + 2 * gap + half, top + scale(22), half, height - top - scale(22) - gap);
            ListView_SetColumnWidth(GetDlgItem(window, 120), 0, (std::max)(scale(90), left - scale(120)));
            ListView_SetColumnWidth(GetDlgItem(window, 120), 1, scale(100)); return 0;
        }
        if (panel && message == WM_NOTIFY) {
            auto header = reinterpret_cast<NMHDR*>(lParam);
            if (header && header->idFrom == 120 && header->code == LVN_ITEMCHANGED) {
                auto item = reinterpret_cast<NMLISTVIEW*>(lParam);
                if ((item->uNewState & LVIS_SELECTED) && !(item->uOldState & LVIS_SELECTED)) Select(panel, item->iItem);
            }
            return 0;
        }
        if (panel && message == WM_COMMAND) {
            int id = LOWORD(wParam);
            if ((id == 101 && HIWORD(wParam) == CBN_SELCHANGE) || ((id == 102 || id == 103) && HIWORD(wParam) == EN_CHANGE)) {
                if (panel->job && panel->published) { panel->invalidated = true; StatusText(panel, BP_READY); }
                Enable(panel);
            }
            else if (id == 110 && !BusyJob(panel)) {
                panel->invalidated = true; Enable(panel);
                if (!SendMessageW(panel->parent, NP3_WM_BATCH_CAPTURE, 0, 0)) StatusText(panel, BP_ERROR);
            } else if (id == 111 && panel->job) { panel->job->cancel = true; EnableWindow(GetDlgItem(window, 111), FALSE); }
            else if (id == 113 && !BusyJob(panel)) ChooseFiles(panel);
            else if (id == 112) Apply(panel);
            return 0;
        }
        if (panel && message == WM_NCDESTROY) {
            KillTimer(window, 1); if (panel->job) panel->job->cancel = true;
            panel->window = nullptr;
            delete reinterpret_cast<Owner*>(GetWindowLongPtrW(window, GWLP_USERDATA)); SetWindowLongPtrW(window, GWLP_USERDATA, 0);
        }
    } catch (...) { if (panel && panel->window) { if (panel->job) panel->job->cancel = true; StatusText(panel, BP_ERROR); Enable(panel); } return 0; }
    return DefWindowProcW(window, message, wParam, lParam);
}
}
extern "C" void* BatchPanel_Create() { try { return new Owner(std::make_shared<Panel>()); } catch (...) { return nullptr; } }
extern "C" void BatchPanel_Destroy(void* context) { auto panel = State(context); if (panel && panel->window) DestroyWindow(panel->window); delete static_cast<Owner*>(context); }
extern "C" BOOL BatchPanel_IsOpen(void* context) { auto panel = State(context); return panel && panel->window; }
extern "C" BOOL BatchPanel_IsBusy(void* context) { return BusyJob(State(context)); }
extern "C" int BatchPanel_Layout(void* context, int x, int y, int width, int height) {
    auto panel = State(context); if (!panel || !panel->window) return 0;
    int reserve = (std::min)((std::max)(0, height * 2 / 3), MulDiv(340, GetDpiForWindow(panel->parent), 96));
    MoveWindow(panel->window, x, y + height - reserve, width, reserve, TRUE); return reserve;
}
extern "C" BOOL BatchPanel_Run(void* context, const BATCH_INPUT* inputs, size_t count) {
    auto panel = State(context); if (!panel || !panel->window || BusyJob(panel) || !inputs || !count || count > 64) return FALSE;
    try {
        auto job = std::make_shared<Job>();
        const auto selected = SendMessageW(GetDlgItem(panel->window, 101), CB_GETCURSEL, 0, 0);
        if (selected < 0 || selected > 6) return FALSE;
        job->options.operation = static_cast<Operation>(selected);
        job->options.find = EditText(GetDlgItem(panel->window, 102)); job->options.replacement = EditText(GetDlgItem(panel->window, 103));
        size_t total = 0;
        for (size_t i = 0; i < count; ++i) {
            const auto& input = inputs[i];
            if ((!input.text && input.length) || !input.label || input.length > 8 * 1024 * 1024 || input.length > 32 * 1024 * 1024 - total) return FALSE;
            total += input.length;
            Source source{input.id, input.version, input.label, std::string(input.text ? input.text : "", input.length), input.readOnly != FALSE, input.eol ? input.eol : ""};
            if (source.label == L"(untitled)") source.label += L" #" + std::to_wstring(source.id);
            job->sources.push_back(std::move(source));
        }
        panel->job = job; panel->published = false; panel->applied = false; panel->invalidated = false;
        ListView_DeleteAllItems(GetDlgItem(panel->window, 120));
        SetWindowTextW(GetDlgItem(panel->window, 121), L""); SetWindowTextW(GetDlgItem(panel->window, 122), L"");
        std::thread([job] {
            try { job->proposals = Prepare(job->sources, job->options, [job] { return job->cancel.load(); }); }
            catch (...) { job->error = true; }
            std::vector<Source>().swap(job->sources); job->done = true;
        }).detach();
        Enable(panel); StatusText(panel, BP_RUNNING); return TRUE;
    } catch (...) { panel->job.reset(); Enable(panel); StatusText(panel, BP_ERROR); return FALSE; }
}
extern "C" void BatchPanel_Toggle(void* context, HWND parent, const wchar_t* const* labels) {
    auto panel = State(context); if (!panel || !labels) return;
    try {
        if (panel->window) DestroyWindow(panel->window);
        else {
            panel->parent = parent; for (int i = 0; i < BP_COUNT; ++i) panel->labels[i] = labels[i];
            if (!panel->richEdit) panel->richEdit = LoadLibraryExW(L"Msftedit.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
            if (!panel->richEdit) throw std::runtime_error("RichEdit");
            WNDCLASSW type{}; type.lpfnWndProc = Proc; type.hInstance = GetModuleHandleW(nullptr); type.lpszClassName = ClassName; type.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1); RegisterClassW(&type);
            auto holder = std::make_unique<Owner>(panel);
            panel->window = CreateWindowExW(WS_EX_CLIENTEDGE, ClassName, nullptr, WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, parent, nullptr, type.hInstance, nullptr);
            if (!panel->window) return;
            SetWindowLongPtrW(panel->window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(holder.release()));
            auto control = [&](const wchar_t* name, const wchar_t* text, DWORD style, int id) {
                HWND child = CreateWindowExW(0, name, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 0, 0, panel->window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), type.hInstance, nullptr);
                if (!child) throw std::bad_alloc(); SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), FALSE); return child;
            };
            HWND combo = control(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP | WS_VSCROLL, 101);
            for (int i = BP_REPLACE; i <= BP_COMPACT; ++i) SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(labels[i]));
            SendMessageW(combo, CB_SETCURSEL, 0, 0);
            for (int id : {102, 103}) { control(L"EDIT", L"", WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL, id); SendMessageW(GetDlgItem(panel->window, id), EM_SETLIMITTEXT, 4096, 0); }
            // Cue banners distinguish the fields without consuming another layout row.
            SendMessageW(GetDlgItem(panel->window, 102), EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(labels[BP_FIND]));
            SendMessageW(GetDlgItem(panel->window, 103), EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(labels[BP_WITH]));
            control(L"BUTTON", labels[BP_PREVIEW], BS_PUSHBUTTON | WS_TABSTOP, 110);
            control(L"BUTTON", labels[BP_CANCEL], BS_PUSHBUTTON | WS_TABSTOP, 111);
            control(L"BUTTON", labels[BP_APPLY], BS_PUSHBUTTON | WS_TABSTOP, 112);
            control(L"BUTTON", labels[BP_ADD], BS_PUSHBUTTON | WS_TABSTOP, 113);
            HWND list = control(WC_LISTVIEWW, L"", WS_BORDER | WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS, 120);
            ListView_SetExtendedListViewStyle(list, LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
            for (int i = 0; i < 2; ++i) { LVCOLUMNW column{}; column.mask = LVCF_TEXT | LVCF_WIDTH; column.cx = 140; column.pszText = panel->labels[i ? BP_STATUS : BP_FILE].data(); ListView_InsertColumn(list, i, &column); }
            control(L"STATIC", labels[BP_ORIGINAL], 0, 201); control(L"STATIC", labels[BP_RESULT], 0, 202);
            for (int id : {121, 122}) { HWND edit = control(L"RICHEDIT50W", L"", WS_BORDER | WS_VSCROLL | WS_HSCROLL | WS_TABSTOP | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | ES_AUTOHSCROLL, id); SendMessageW(edit, EM_EXLIMITTEXT, 0, 32 * 1024 * 1024); }
            control(L"STATIC", labels[BP_READY], 0, 130);
            panel->published = false; panel->applied = false; Enable(panel);
            if (!SetTimer(panel->window, 1, 50, nullptr)) throw std::bad_alloc();
        }
        RECT rect{}; GetClientRect(parent, &rect); SendMessageW(parent, WM_SIZE, 0, MAKELPARAM(rect.right, rect.bottom));
    } catch (...) { if (panel->window) DestroyWindow(panel->window); }
}
