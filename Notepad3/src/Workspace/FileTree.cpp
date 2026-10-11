#include "FileTree.h"
#include "FileSearchPanel.h"
#include "../UI/PanelTheme.h"
#include <commctrl.h>
#include <shobjidl.h>
#include <shellapi.h>
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
constexpr wchar_t PreferencesKey[] = L"NP3FileTreePreferences";
struct Preferences { std::wstring root; int widthDip = 240; };
LRESULT CALLBACK PreferencesProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR) {
    LRESULT result = DefSubclassProc(window, message, wParam, lParam);
    if (message == WM_NCDESTROY) {
        delete reinterpret_cast<Preferences*>(RemovePropW(window, PreferencesKey));
        RemoveWindowSubclass(window, PreferencesProc, 0);
    }
    return result;
}
Preferences* GetPreferences(HWND parent) {
    auto* saved = reinterpret_cast<Preferences*>(GetPropW(parent, PreferencesKey));
    if (saved) return saved;
    auto candidate = std::make_unique<Preferences>();
    std::vector<wchar_t> root(32768);
    FILETREE_STATE initial{root.data(),static_cast<UINT>(root.size()),240};
    if (SendMessageW(parent,NP3_WM_FILETREE_STATE,0,reinterpret_cast<LPARAM>(&initial))) {
        candidate->root=root.data(); candidate->widthDip=(std::clamp)(initial.widthDip,120,10000);
    }
    if (!SetWindowSubclass(parent, PreferencesProc, 0, 0)) throw std::bad_alloc();
    if (!SetPropW(parent, PreferencesKey, candidate.get())) {
        RemoveWindowSubclass(parent, PreferencesProc, 0); throw std::bad_alloc();
    }
    return candidate.release();
}
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
    np3::PanelTheme theme;
    std::atomic<HWND> window{nullptr};
    std::atomic<unsigned long long> generation{0};
    HWND parent = nullptr, tree = nullptr, choose = nullptr, refresh = nullptr, search = nullptr, grip = nullptr;
    int widthDip = 240, startWidthDip = 240; bool dragging = false; RECT area{};
    HIMAGELIST images = nullptr;
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
void Publish(const std::shared_ptr<Tree>& state) {
    FILETREE_STATE saved{state->root.data(),static_cast<UINT>(state->root.size()+1),state->dragging?state->startWidthDip:state->widthDip};
    SendMessageW(state->parent,NP3_WM_FILETREE_STATE,1,reinterpret_cast<LPARAM>(&saved));
}
void Icons(const std::shared_ptr<Tree>& state, HWND window) {
    UINT dpi=GetDpiForWindow(window);
    int width=GetSystemMetricsForDpi(SM_CXSMICON,dpi),height=GetSystemMetricsForDpi(SM_CYSMICON,dpi);
    HIMAGELIST images=ImageList_Create(width,height,ILC_COLOR32|ILC_MASK,2,0);
    if(!images) return;
    for(SHSTOCKICONID id:{SIID_FOLDER,SIID_DOCNOASSOC}) {
        SHSTOCKICONINFO icon={}; icon.cbSize=sizeof(icon);
        UINT flags=SHGSI_ICON | (width>GetSystemMetrics(SM_CXSMICON)?SHGSI_LARGEICON:SHGSI_SMALLICON);
        if(FAILED(SHGetStockIconInfo(id,flags,&icon))) { ImageList_Destroy(images); return; }
        int index=ImageList_AddIcon(images,icon.hIcon); DestroyIcon(icon.hIcon);
        if(index<0) { ImageList_Destroy(images); return; }
    }
    HIMAGELIST old=state->images;
    state->images=images; TreeView_SetImageList(state->tree,images,TVSIL_NORMAL);
    // Keep node handles, expansion and selection while replacing DPI-dependent resources.
    for(HTREEITEM item=TreeView_GetRoot(state->tree);item;) {
        TVITEMW value{}; value.mask=TVIF_PARAM; value.hItem=item;
        if(TreeView_GetItem(state->tree,&value)) {
            auto* node=reinterpret_cast<Node*>(value.lParam);
            value.mask=TVIF_IMAGE|TVIF_SELECTEDIMAGE;
            value.iImage=value.iSelectedImage=node && !node->more?(node->directory?0:1):I_IMAGENONE;
            TreeView_SetItem(state->tree,&value);
        }
        if(HTREEITEM child=TreeView_GetChild(state->tree,item)) item=child;
        else {
            HTREEITEM next=TreeView_GetNextSibling(state->tree,item);
            while(!next && (item=TreeView_GetParent(state->tree,item))) next=TreeView_GetNextSibling(state->tree,item);
            item=next;
        }
    }
    if(old) ImageList_Destroy(old);
}
void Relayout(const std::shared_ptr<Tree>& state) {
    RECT bounds; GetClientRect(state->parent,&bounds);
    SendMessageW(state->parent,WM_SIZE,0,MAKELPARAM(bounds.right,bounds.bottom));
}
int TreeWidth(const std::shared_ptr<Tree>& state,int available) {
    UINT dpi=GetDpiForWindow(state->parent);
    int minimum=MulDiv(120,dpi,96),maximum=(std::min)(available/2,(std::max)(0,available-MulDiv(240,dpi,96)));
    return maximum>=minimum ? (std::clamp)(MulDiv(state->widthDip,dpi,96),minimum,maximum) : (std::max)(0,available/3);
}
LRESULT CALLBACK GripProc(HWND window,UINT message,WPARAM wParam,LPARAM lParam,UINT_PTR,DWORD_PTR) {
    auto state=State(GetParent(window));
    if(!state) return DefSubclassProc(window,message,wParam,lParam);
    switch(message) {
    case WM_SETCURSOR: SetCursor(LoadCursorW(nullptr,IDC_SIZEWE)); return TRUE;
    case WM_LBUTTONDOWN: state->dragging=true; state->startWidthDip=state->widthDip; SetCapture(window); return 0;
    case WM_MOUSEMOVE:
        if(state->dragging && GetCapture()==window) {
            POINT point{static_cast<short>(LOWORD(lParam)),static_cast<short>(HIWORD(lParam))}; MapWindowPoints(window,state->parent,&point,1);
            UINT dpi=GetDpiForWindow(state->parent); int available=state->area.right-state->area.left;
            int minimum=MulDiv(120,dpi,96),maximum=(std::min)(available/2,(std::max)(0,available-MulDiv(240,dpi,96)));
            if(maximum>=minimum) { state->widthDip=MulDiv((std::clamp)(static_cast<int>(point.x-state->area.left),minimum,maximum),96,dpi); Relayout(state); }
        } return 0;
    case WM_LBUTTONUP: state->dragging=false; if(GetCapture()==window) ReleaseCapture(); Publish(state); return 0;
    case WM_CANCELMODE:
        if(state->dragging) { state->widthDip=state->startWidthDip; state->dragging=false; if(GetCapture()==window) ReleaseCapture(); Relayout(state); } return 0;
    case WM_CAPTURECHANGED: state->dragging=false; return 0;
    case WM_PAINT: {
        PAINTSTRUCT paint; HDC dc=BeginPaint(window,&paint); RECT bounds; GetClientRect(window,&bounds);
        const auto& colors=state->theme.Colors(); HBRUSH brush=(HBRUSH)GetStockObject(DC_BRUSH);
        COLORREF previous=SetDCBrushColor(dc,colors.panel); FillRect(dc,&bounds,brush);
        bounds.left=bounds.right/2; bounds.right=(std::min)(bounds.right,bounds.left+(std::max)(1,MulDiv(1,GetDpiForWindow(window),96)));
        SetDCBrushColor(dc,colors.disabled); FillRect(dc,&bounds,brush); SetDCBrushColor(dc,previous); EndPaint(window,&paint); return 0;
    }
    }
    return DefSubclassProc(window,message,wParam,lParam);
}
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
    value.item.mask = TVIF_TEXT | TVIF_PARAM | TVIF_CHILDREN | TVIF_IMAGE | TVIF_SELECTEDIMAGE;
    value.item.iImage=value.item.iSelectedImage=state->images && node && !node->more ? (node->directory ? 0 : 1) : I_IMAGENONE; value.item.pszText = const_cast<wchar_t*>(label.c_str());
    value.item.lParam = reinterpret_cast<LPARAM>(node); value.item.cChildren = node && node->directory && !node->reparse ? 1 : 0;
    HTREEITEM item=TreeView_InsertItem(state->tree, &value);
    // Native TreeView needs a real child during expansion, before its notification.
    // This placeholder performs no directory I/O and lets pending loads be collapsed.
    if(item && node && node->directory && !node->reparse) Insert(state,item,state->loading,nullptr);
    return item;
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
    if(!node->loaded && !TreeView_GetChild(state->tree,item)) Insert(state,item,state->loading,nullptr);
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
    Request(state, root); TreeView_Expand(state->tree, root, TVE_EXPAND);
    Publish(state);
    return true;
}
void Drain(const std::shared_ptr<Tree>& state) {
    std::deque<Result> results;
    { std::lock_guard<std::mutex> guard(state->mutex); results.swap(state->results); }
    for (auto& result : results) {
        if (result.generation != state->generation.load()) continue;
        Node* node = Data(state->tree, result.item);
        if (!node || node->path != result.path) continue;
        bool const expand=(TreeView_GetItemState(state->tree,result.item,TVIS_EXPANDED)&TVIS_EXPANDED)!=0;
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
        if(expand && node->loaded) TreeView_Expand(state->tree,result.item,TVE_EXPAND);
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
    if (message == NP3_WM_PANEL_REFRESH && state) {
        state->theme.Update(window,state->parent); Icons(state,window);
        Relayout(state); InvalidateRect(state->grip,nullptr,TRUE); return 0;
    }
    LRESULT themed = 0;
    if (state && state->theme.Handle(window,message,wParam,lParam,themed)) return themed;
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
        int gap=(std::min)(width,MulDiv(6,GetDpiForWindow(window),96));
        MoveWindow(state->grip,width-gap,0,gap,height,TRUE); width-=gap;
        MoveWindow(state->choose, 0, 0, width / 3, top, TRUE); MoveWindow(state->refresh, width / 3, 0, width / 3, top, TRUE); MoveWindow(state->search, width * 2 / 3, 0, width - width * 2 / 3, top, TRUE);
        MoveWindow(state->tree, 0, top, width, (std::max)(0, height - top), TRUE); return 0;
    }
    if (message == WM_COMMAND && state) { if ((HWND)lParam == state->choose) Choose(state); else if ((HWND)lParam == state->refresh) Root(state, state->root.c_str()); else if ((HWND)lParam == state->search) FileSearch_Show(state->parent,state->root.c_str()); return 0; }
    if (message == WM_NOTIFY && state) {
        auto* notify = reinterpret_cast<NMHDR*>(lParam);
        if (notify->code == TVN_DELETEITEMW) { delete reinterpret_cast<Node*>(reinterpret_cast<NMTREEVIEWW*>(lParam)->itemOld.lParam); }
        else if (notify->code == TVN_ITEMEXPANDINGW) {
            auto* change=reinterpret_cast<NMTREEVIEWW*>(lParam);
            if(change->action==TVE_EXPAND) Request(state,change->itemNew.hItem);
        }
        else if (notify->code == NM_DBLCLK || notify->code == NM_RETURN) {
            auto* node = Data(state->tree, TreeView_GetSelection(state->tree));
            if (node && node->more) Request(state, TreeView_GetParent(state->tree, TreeView_GetSelection(state->tree)), true);
            else if (node && !node->directory) { auto path = node->path; SendMessageW(state->parent, NP3_WM_FILETREE_OPEN, 0, reinterpret_cast<LPARAM>(path.c_str())); }
        }
        return 0;
    }
    if (message == WM_DESTROY && state && state->images) {
        if(IsWindow(state->tree)) TreeView_SetImageList(state->tree,nullptr,TVSIL_NORMAL);
        ImageList_Destroy(state->images); state->images=nullptr;
    }
    if (message == WM_DESTROY && state) {
        auto* saved = GetPreferences(state->parent);
        saved->root = state->root;
        saved->widthDip = state->dragging ? state->startWidthDip : state->widthDip;
        Publish(state);
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
    auto state=State(window); if(!state) return 0;
    state->area={x,y,x+(std::max)(0,width),y+(std::max)(0,height)};
    int reserved=TreeWidth(state,(std::max)(0,width));
    MoveWindow(window, x, y, reserved, height, TRUE); return reserved;
}
extern "C" void FileTree_Toggle(HWND parent, const wchar_t* directory, const FILETREE_LABELS* labels) {
    if (!IsWindow(parent) || !labels) return;
    try {
    if (HWND old = Find(parent)) { DestroyWindow(old); }
    else {
        auto* saved = GetPreferences(parent);
        auto state = std::make_shared<Tree>(); state->parent = parent; state->loading = labels->loading; state->limit = labels->limit;
        state->widthDip = saved->widthDip;
        LARGE_INTEGER tick; QueryPerformanceCounter(&tick); state->cookie = static_cast<UINT_PTR>(tick.QuadPart);
        WNDCLASSW cls = {}; cls.hInstance = GetModuleHandleW(nullptr); cls.lpfnWndProc = WindowProc; cls.lpszClassName = ClassName; cls.hCursor = LoadCursorW(nullptr, IDC_ARROW); RegisterClassW(&cls);
        HWND window = CreateWindowExW(WS_EX_CLIENTEDGE, ClassName, L"Files", WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN, 0, 0, 240, 300, parent, nullptr, cls.hInstance, nullptr);
        if (!window) return;
        state->window = window; SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(new std::shared_ptr<Tree>(state)));
        state->choose = CreateWindowExW(0, L"BUTTON", labels->choose, WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 0, 160, 30, window, nullptr, cls.hInstance, nullptr);
        state->refresh = CreateWindowExW(0, L"BUTTON", labels->refresh, WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 160, 0, 80, 30, window, nullptr, cls.hInstance, nullptr);
        state->search = CreateWindowExW(0, L"BUTTON", L"搜索", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 0, 80, 30, window, nullptr, cls.hInstance, nullptr);
        state->tree = CreateWindowExW(0, WC_TREEVIEWW, L"", WS_CHILD | WS_VISIBLE | TVS_HASLINES | TVS_LINESATROOT | TVS_HASBUTTONS | TVS_SHOWSELALWAYS, 0, 30, 240, 270, window, nullptr, cls.hInstance, nullptr);
        for (HWND control : {state->choose, state->refresh, state->search, state->tree}) SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
        state->grip=CreateWindowExW(0,L"STATIC",L"",WS_CHILD|WS_VISIBLE|SS_NOTIFY,0,0,0,0,window,nullptr,cls.hInstance,nullptr);
        if(!state->grip || !SetWindowSubclass(state->grip,GripProc,0,0)) throw std::bad_alloc();
        Icons(state,window);
        if (saved->root.empty() || !Root(state, saved->root.c_str())) Root(state, directory);
        state->theme.Update(window,parent);
    }
    RECT bounds; GetClientRect(parent, &bounds); SendMessageW(parent, WM_SIZE, SIZE_RESTORED, MAKELPARAM(bounds.right, bounds.bottom));
    } catch (...) { if (HWND window = Find(parent)) DestroyWindow(window); PostMessageW(parent, NP3_WM_FILETREE_ERROR, ERROR_NOT_ENOUGH_MEMORY, 0); }
}
