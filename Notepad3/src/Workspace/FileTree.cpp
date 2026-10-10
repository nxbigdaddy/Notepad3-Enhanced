#include "FileTree.h"
#include <commctrl.h>
#include <shobjidl.h>
#include <wrl.h>
#include <algorithm>
#include <atomic>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#pragma comment(lib, "comctl32.lib")
namespace {
constexpr wchar_t ClassName[] = L"NP3WorkspaceFileTree";
constexpr UINT Ready = WM_APP + 1;
struct Cursor {
    HANDLE handle = INVALID_HANDLE_VALUE;
    WIN32_FIND_DATAW found = {};
    bool started = false, finished = false;
    ~Cursor() { if (handle != INVALID_HANDLE_VALUE) FindClose(handle); }
};
struct Entry { std::wstring name; bool directory, reparse; };
struct Result { unsigned long long generation; HTREEITEM item; std::wstring path; std::vector<Entry> entries; DWORD error = 0; bool append = false; };
struct Work { unsigned long long generation; HTREEITEM item; std::wstring path; std::shared_ptr<Cursor> cursor; bool append; };
struct Node { std::wstring path; bool directory, reparse, loaded = false, pending = false; std::shared_ptr<Cursor> cursor; bool more = false; };
struct Tree {
    std::atomic<HWND> window{nullptr};
    std::atomic<unsigned long long> generation{0};
    HWND parent = nullptr, tree = nullptr, choose = nullptr, refresh = nullptr;
    std::wstring root, loading, limit;
    UINT_PTR cookie = 0;
    std::mutex mutex;
    std::deque<Result> results;
    std::deque<Work> queued; // UI thread only
    std::atomic<unsigned> workers{0};
    unsigned pending = 0;
};
std::shared_ptr<Tree> State(HWND window) {
    auto holder = reinterpret_cast<std::shared_ptr<Tree>*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    return holder ? *holder : nullptr;
}
HWND Find(HWND parent) { return FindWindowExW(parent, nullptr, ClassName, nullptr); }
std::wstring Native(const std::wstring& path) {
    if (path.rfind(L"\\\\?\\", 0) == 0) return path;
    return path.rfind(L"\\\\", 0) == 0 ? L"\\\\?\\UNC\\" + path.substr(2) : L"\\\\?\\" + path;
}
std::wstring Join(const std::wstring& path, const std::wstring& leaf) { return path + (path.back() == L'\\' ? L"" : L"\\") + leaf; }
Node* Data(HWND tree, HTREEITEM item) {
    TVITEMW value = {}; value.mask = TVIF_PARAM; value.hItem = item;
    return TreeView_GetItem(tree, &value) ? reinterpret_cast<Node*>(value.lParam) : nullptr;
}
HTREEITEM Insert(const std::shared_ptr<Tree>& state, HTREEITEM parent, const std::wstring& label, Node* node) {
    TVINSERTSTRUCTW value = {}; value.hParent = parent; value.hInsertAfter = TVI_LAST;
    value.item.mask = TVIF_TEXT | TVIF_PARAM | TVIF_CHILDREN; value.item.pszText = const_cast<wchar_t*>(label.c_str());
    value.item.lParam = reinterpret_cast<LPARAM>(node); value.item.cChildren = node && node->directory && !node->reparse ? 1 : 0;
    return TreeView_InsertItem(state->tree, &value);
}
void Schedule(const std::shared_ptr<Tree>& state) {
    while (state->workers.load() < 4 && !state->queued.empty()) {
        Work work = std::move(state->queued.front()); state->queued.pop_front();
        if (work.generation != state->generation.load()) continue;
        Node* node = Data(state->tree, work.item);
        if (!node || node->path != work.path) continue;
        ++state->workers;
        try {
            std::thread([state, work = std::move(work)] {
                const auto generation = work.generation;
                const auto item = work.item;
                const auto& path = work.path;
                // Even allocation failures and canceled generations release the slot and wake the queue.
                bool failed = false;
                try {
                    Result result = { generation, item, path, {}, 0, work.append };
                    try {
                        auto& cursor = *work.cursor;
                        if (!cursor.started) {
                            cursor.started = true;
                            cursor.handle = FindFirstFileW(Native(Join(path, L"*")).c_str(), &cursor.found);
                            if (cursor.handle == INVALID_HANDLE_VALUE) {
                                result.error = GetLastError();
                                if (result.error == ERROR_FILE_NOT_FOUND) result.error = 0;
                                cursor.finished = true;
                            }
                        }
                        while (!cursor.finished && state->window.load() && state->generation.load() == generation) {
                            const auto& found = cursor.found;
                            if (wcscmp(found.cFileName, L".") && wcscmp(found.cFileName, L"..")) {
                                if (result.entries.size() == 5000) { result.error = ERROR_MORE_DATA; break; }
                                result.entries.push_back({found.cFileName, (found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0, (found.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0});
                            }
                            if (!FindNextFileW(cursor.handle, &cursor.found)) {
                                cursor.finished = true;
                                DWORD error = GetLastError();
                                if (error != ERROR_NO_MORE_FILES) result.error = error;
                            }
                        }
                        std::sort(result.entries.begin(), result.entries.end(), [](const Entry& left, const Entry& right) {
                            if (left.directory != right.directory) return left.directory;
                            return CompareStringOrdinal(left.name.c_str(), -1, right.name.c_str(), -1, TRUE) == CSTR_LESS_THAN;
                        });
                        } catch (...) { result.error = ERROR_NOT_ENOUGH_MEMORY; result.entries.clear(); }
                    if (state->window.load() && state->generation.load() == generation) {
                        std::lock_guard<std::mutex> guard(state->mutex);
                        state->results.push_back(std::move(result));
                    }
                } catch (...) { failed = true; }
                --state->workers;
                HWND window = state->window.load();
                if (window) PostMessageW(window, Ready, state->cookie, failed ? ERROR_NOT_ENOUGH_MEMORY : 0);
            }).detach();
        } catch (...) {
            --state->workers; node->pending = false; --state->pending;
            PostMessageW(state->parent, NP3_WM_FILETREE_ERROR, ERROR_NOT_ENOUGH_MEMORY, 0);
        }
    }
}
void Request(const std::shared_ptr<Tree>& state, HTREEITEM item, bool more = false) {
    Node* node = Data(state->tree, item);
    if (!node || !node->directory || node->reparse || (node->loaded && !more) || node->pending) return;
    if (more && !node->cursor) return;
    if (!node->cursor) node->cursor = std::make_shared<Cursor>();
    state->queued.push_back({state->generation.load(), item, node->path, node->cursor, node->loaded});
    node->pending = true; ++state->pending;
    Schedule(state);
}
bool Root(const std::shared_ptr<Tree>& state, const wchar_t* directory) {
    if (!directory || !directory[0]) return false;
    DWORD size = GetFullPathNameW(directory, 0, nullptr, nullptr); if (!size || size >= 32768) return false;
    std::wstring full(size, L'\0'); DWORD written = GetFullPathNameW(directory, size, full.data(), nullptr);
    if (!written || written >= size) return false; full.resize(written);
    DWORD attributes = GetFileAttributesW(Native(full).c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES || !(attributes & FILE_ATTRIBUTE_DIRECTORY)) return false;
    ++state->generation; state->pending = 0; state->queued.clear();
    TreeView_DeleteAllItems(state->tree); state->root = full;
    auto* node = new Node{full, true, false};
    HTREEITEM root = Insert(state, TVI_ROOT, full, node);
    if (!root) { delete node; return false; }
    Insert(state, root, state->loading, nullptr); Request(state, root); TreeView_Expand(state->tree, root, TVE_EXPAND);
    return true;
}
void Drain(const std::shared_ptr<Tree>& state) {
    std::deque<Result> results;
    { std::lock_guard<std::mutex> guard(state->mutex); results.swap(state->results); }
    for (auto& result : results) {
        if (result.generation != state->generation.load()) continue;
        Node* node = Data(state->tree, result.item);
        if (!node || node->path != result.path) continue;
        node->pending = false; node->loaded = result.error == 0 || result.error == ERROR_MORE_DATA;
        if (state->pending) --state->pending;
        SendMessageW(state->tree, WM_SETREDRAW, FALSE, 0);
        if (!result.append) {
            while (HTREEITEM child = TreeView_GetChild(state->tree, result.item)) TreeView_DeleteItem(state->tree, child);
        } else {
            for (HTREEITEM child = TreeView_GetChild(state->tree, result.item); child;) {
                HTREEITEM next = TreeView_GetNextSibling(state->tree, child);
                Node* data = Data(state->tree, child);
                if (data && data->more) TreeView_DeleteItem(state->tree, child);
                child = next;
            }
        }
        for (const auto& entry : result.entries) {
            auto* child = new Node{Join(result.path, entry.name), entry.directory, entry.reparse};
            if (!Insert(state, result.item, entry.name, child)) delete child;
        }
        if (result.error == ERROR_MORE_DATA) {
            auto* more = new Node{L"", false, false}; more->more = true;
            if (!Insert(state, result.item, state->limit, more)) delete more;
        } else {
            node->cursor.reset();
            if (result.error) PostMessageW(state->parent, NP3_WM_FILETREE_ERROR, result.error, 0);
        }
        TVITEMW count = {}; count.mask = TVIF_CHILDREN; count.hItem = result.item; count.cChildren = TreeView_GetChild(state->tree, result.item) ? 1 : 0; TreeView_SetItem(state->tree, &count);
        SendMessageW(state->tree, WM_SETREDRAW, TRUE, 0); InvalidateRect(state->tree, nullptr, TRUE);
    }
    Schedule(state);
}
void Choose(const std::shared_ptr<Tree>& state) {
    Microsoft::WRL::ComPtr<IFileOpenDialog> dialog;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) return;
    DWORD options = 0; dialog->GetOptions(&options); dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
    if (FAILED(dialog->Show(state->parent))) return;
    Microsoft::WRL::ComPtr<IShellItem> item;
    if (SUCCEEDED(dialog->GetResult(&item))) {
        PWSTR path = nullptr;
        if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) { Root(state, path); CoTaskMemFree(path); }
    }
}
LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    auto state = State(window);
    try {
    if (message == Ready && state && wParam == state->cookie) {
        if (lParam) {
            // A lost result cannot leave a directory permanently pending: rebuild so it can be retried.
            Root(state, state->root.c_str());
            PostMessageW(state->parent, NP3_WM_FILETREE_ERROR, lParam, 0);
        }
        Drain(state); return 0;
    }
    if (message == WM_SIZE && state) {
        int width = LOWORD(lParam), height = HIWORD(lParam); int top = MulDiv(30, GetDpiForWindow(window), 96);
        MoveWindow(state->choose, 0, 0, width * 2 / 3, top, TRUE); MoveWindow(state->refresh, width * 2 / 3, 0, width - width * 2 / 3, top, TRUE);
        MoveWindow(state->tree, 0, top, width, (std::max)(0, height - top), TRUE); return 0;
    }
    if (message == WM_COMMAND && state) { if ((HWND)lParam == state->choose) Choose(state); else if ((HWND)lParam == state->refresh) Root(state, state->root.c_str()); return 0; }
    if (message == WM_NOTIFY && state) {
        auto* notify = reinterpret_cast<NMHDR*>(lParam);
        if (notify->code == TVN_DELETEITEMW) { delete reinterpret_cast<Node*>(reinterpret_cast<NMTREEVIEWW*>(lParam)->itemOld.lParam); }
        else if (notify->code == TVN_ITEMEXPANDINGW) Request(state, reinterpret_cast<NMTREEVIEWW*>(lParam)->itemNew.hItem);
        else if (notify->code == NM_DBLCLK || notify->code == NM_RETURN) {
            auto* node = Data(state->tree, TreeView_GetSelection(state->tree));
            if (node && node->more) Request(state, TreeView_GetParent(state->tree, TreeView_GetSelection(state->tree)), true);
            else if (node && !node->directory) { auto path = node->path; SendMessageW(state->parent, NP3_WM_FILETREE_OPEN, 0, reinterpret_cast<LPARAM>(path.c_str())); }
        }
        return 0;
    }
    if (message == WM_NCDESTROY && state) {
        state->window = nullptr; ++state->generation; state->queued.clear();
        auto* holder = reinterpret_cast<std::shared_ptr<Tree>*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        SetWindowLongPtrW(window, GWLP_USERDATA, 0); delete holder;
    }
    return DefWindowProcW(window, message, wParam, lParam);
    } catch (...) { if (state) { SendMessageW(state->tree, WM_SETREDRAW, TRUE, 0); PostMessageW(state->parent, NP3_WM_FILETREE_ERROR, ERROR_NOT_ENOUGH_MEMORY, 0); } return 0; }
}
}
extern "C" BOOL FileTree_IsOpen(HWND parent) { return Find(parent) != nullptr; }
extern "C" BOOL FileTree_IsLoading(HWND parent) { auto state = State(Find(parent)); return state && state->pending != 0; }
extern "C" BOOL FileTree_SetRoot(HWND parent, const wchar_t* directory) { try { auto state = State(Find(parent)); return state && Root(state, directory); } catch (...) { PostMessageW(parent, NP3_WM_FILETREE_ERROR, ERROR_NOT_ENOUGH_MEMORY, 0); return FALSE; } }
extern "C" int FileTree_Layout(HWND parent, int x, int y, int width, int height) {
    HWND window = Find(parent); if (!window) return 0;
    int reserved = (std::min)((std::max)(0, width / 3), MulDiv(240, GetDpiForWindow(parent), 96));
    MoveWindow(window, x, y, reserved, height, TRUE); return reserved;
}
extern "C" void FileTree_Toggle(HWND parent, const wchar_t* directory, const FILETREE_LABELS* labels) {
    if (!IsWindow(parent) || !labels) return;
    try {
    if (HWND old = Find(parent)) { DestroyWindow(old); }
    else {
        auto state = std::make_shared<Tree>(); state->parent = parent; state->loading = labels->loading; state->limit = labels->limit;
        LARGE_INTEGER tick; QueryPerformanceCounter(&tick); state->cookie = static_cast<UINT_PTR>(tick.QuadPart);
        WNDCLASSW cls = {}; cls.hInstance = GetModuleHandleW(nullptr); cls.lpfnWndProc = WindowProc; cls.lpszClassName = ClassName; cls.hCursor = LoadCursorW(nullptr, IDC_ARROW); RegisterClassW(&cls);
        HWND window = CreateWindowExW(WS_EX_CLIENTEDGE, ClassName, L"Files", WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN, 0, 0, 240, 300, parent, nullptr, cls.hInstance, nullptr);
        if (!window) return;
        state->window = window; SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(new std::shared_ptr<Tree>(state)));
        state->choose = CreateWindowExW(0, L"BUTTON", labels->choose, WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 0, 160, 30, window, nullptr, cls.hInstance, nullptr);
        state->refresh = CreateWindowExW(0, L"BUTTON", labels->refresh, WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 160, 0, 80, 30, window, nullptr, cls.hInstance, nullptr);
        state->tree = CreateWindowExW(0, WC_TREEVIEWW, L"", WS_CHILD | WS_VISIBLE | TVS_HASLINES | TVS_LINESATROOT | TVS_HASBUTTONS | TVS_SHOWSELALWAYS, 0, 30, 240, 270, window, nullptr, cls.hInstance, nullptr);
        for (HWND control : {state->choose, state->refresh, state->tree}) SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
        Root(state, directory);
    }
    RECT bounds; GetClientRect(parent, &bounds); SendMessageW(parent, WM_SIZE, SIZE_RESTORED, MAKELPARAM(bounds.right, bounds.bottom));
    } catch (...) { if (HWND window = Find(parent)) DestroyWindow(window); PostMessageW(parent, NP3_WM_FILETREE_ERROR, ERROR_NOT_ENOUGH_MEMORY, 0); }
}
