#include "MarkdownPreview.h"
#include "MarkdownRender.h"
#include "vendor/webview2/WebView2.h"
#include <wrl.h>
#include <memory>
#include <string>
#include <vector>

using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;
namespace {
constexpr wchar_t HostClass[] = L"NP3MarkdownPreview";
constexpr UINT_PTR RefreshTimer = 1;
struct Preview {
    HWND window = nullptr;
    HWND parent = nullptr;
    HMODULE loader = nullptr;
    ComPtr<ICoreWebView2Controller> controller;
    ComPtr<ICoreWebView2> web;
    std::wstring message = L"Starting Markdown preview...";
    ~Preview() { if (loader) FreeLibrary(loader); }
};

std::shared_ptr<Preview> State(HWND window)
{
    auto* holder = reinterpret_cast<std::shared_ptr<Preview>*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    return holder ? *holder : nullptr;
}
HWND Find(HWND parent) { return FindWindowExW(parent, nullptr, HostClass, nullptr); }

std::wstring Wide(const std::string& text)
{
    const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (!size) return {};
    std::wstring result(size, L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), result.data(), size);
    return result;
}
void Failed(const std::shared_ptr<Preview>& state, const wchar_t* message)
{
    if (!state->window) return;
    state->message = message;
    InvalidateRect(state->window, nullptr, TRUE);
}
void Resize(const std::shared_ptr<Preview>& state)
{
    if (!state->window || !state->controller) return;
    RECT bounds;
    GetClientRect(state->window, &bounds);
    state->controller->put_Bounds(bounds);
}
void Start(const std::shared_ptr<Preview>& state)
{
    wchar_t module[MAX_PATH];
    const DWORD count = GetModuleFileNameW(nullptr, module, MAX_PATH);
    if (!count || count >= MAX_PATH) {
        Failed(state, L"Cannot locate the Markdown preview runtime.");
        return;
    }
    std::wstring path(module, count);
    path.resize(path.find_last_of(L"\\/") + 1);
    path += L"WebView2Loader.dll";
    state->loader = LoadLibraryExW(path.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (!state->loader) {
        Failed(state, L"WebView2Loader.dll is missing. Editing remains available.");
        return;
    }
    using CreateEnvironment = HRESULT(STDAPICALLTYPE*)(PCWSTR, PCWSTR, ICoreWebView2EnvironmentOptions*, ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler*);
    auto create = reinterpret_cast<CreateEnvironment>(GetProcAddress(state->loader, "CreateCoreWebView2EnvironmentWithOptions"));
    if (!create) { Failed(state, L"WebView2 loader is incompatible."); return; }
    wchar_t temp[MAX_PATH];
    if (!GetTempPathW(MAX_PATH, temp)) { Failed(state, L"Cannot create preview data directory."); return; }
    const std::wstring data = std::wstring(temp) + L"Notepad3-Markdown-WebView2";
    // Strong references keep the loader alive while asynchronous creation is pending.
    const HRESULT result = create(nullptr, data.c_str(), nullptr,
        Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [state](HRESULT status, ICoreWebView2Environment* environment) -> HRESULT {
                if (!state->window) return S_OK;
                if (FAILED(status) || !environment) {
                    Failed(state, L"Install the Microsoft Edge WebView2 Runtime to enable preview.");
                    return S_OK;
                }
                const HRESULT creation = environment->CreateCoreWebView2Controller(state->window,
                    Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                        [state](HRESULT status, ICoreWebView2Controller* controller) -> HRESULT {
                            if (!state->window) { if (controller) controller->Close(); return S_OK; }
                            if (FAILED(status) || !controller) { Failed(state, L"Cannot start preview."); return S_OK; }
                            state->controller = controller;
                            controller->get_CoreWebView2(&state->web);
                            if (!state->web) { Failed(state, L"Cannot create preview content."); return S_OK; }
                            ComPtr<ICoreWebView2Settings> settings;
                            state->web->get_Settings(&settings);
                            if (settings) {
                                settings->put_IsScriptEnabled(FALSE);
                                settings->put_AreDevToolsEnabled(FALSE);
                                settings->put_AreDefaultContextMenusEnabled(FALSE);
                                settings->put_IsWebMessageEnabled(FALSE);
                            }
                            EventRegistrationToken token;
                            state->web->add_NavigationStarting(Callback<ICoreWebView2NavigationStartingEventHandler>(
                                [](ICoreWebView2*, ICoreWebView2NavigationStartingEventArgs* args) -> HRESULT {
                                    LPWSTR uri = nullptr;
                                    args->get_Uri(&uri);
                                    if (!uri || std::wstring(uri) != L"about:blank") args->put_Cancel(TRUE);
                                    CoTaskMemFree(uri);
                                    return S_OK;
                                }).Get(), &token);
                            state->web->add_NewWindowRequested(Callback<ICoreWebView2NewWindowRequestedEventHandler>(
                                [](ICoreWebView2*, ICoreWebView2NewWindowRequestedEventArgs* args) -> HRESULT {
                                    args->put_Handled(TRUE);
                                    return S_OK;
                                }).Get(), &token);
                            Resize(state);
                            state->message.clear();
                            SendMessageW(state->parent, NP3_WM_MARKDOWN_REFRESH, 0, 0);
                            return S_OK;
                        }).Get());
                if (FAILED(creation)) Failed(state, L"Cannot create preview controller.");
                return S_OK;
            }).Get());
    if (FAILED(result)) Failed(state, L"WebView2 Runtime is unavailable. Editing remains available.");
}
LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    auto state = State(window);
    if (message == WM_NCCREATE) {
        state = std::make_shared<Preview>();
        state->window = window;
        state->parent = reinterpret_cast<CREATESTRUCTW*>(lParam)->hwndParent;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(new std::shared_ptr<Preview>(state)));
        return TRUE;
    }
    if (!state) return DefWindowProcW(window, message, wParam, lParam);
    switch (message) {
    case WM_CREATE: Start(state); return 0;
    case WM_SIZE: Resize(state); return 0;
    case WM_TIMER:
        if (wParam == RefreshTimer) {
            KillTimer(window, RefreshTimer);
            SendMessageW(state->parent, NP3_WM_MARKDOWN_REFRESH, 0, 0);
        }
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT paint;
        HDC dc = BeginPaint(window, &paint);
        RECT bounds;
        GetClientRect(window, &bounds);
        bounds.left += 12; bounds.top += 12; bounds.right -= 12;
        SetBkMode(dc, TRANSPARENT);
        DrawTextW(dc, state->message.c_str(), -1, &bounds, DT_WORDBREAK);
        EndPaint(window, &paint);
        return 0;
    }
    case WM_NCDESTROY:
        KillTimer(window, RefreshTimer);
        state->window = nullptr;
        if (state->controller) state->controller->Close();
        state->web.Reset(); state->controller.Reset();
        delete reinterpret_cast<std::shared_ptr<Preview>*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        SetWindowLongPtrW(window, GWLP_USERDATA, 0);
        break;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}
}

extern "C" BOOL MarkdownPreview_IsOpen(HWND parent) { return Find(parent) != nullptr; }
extern "C" void MarkdownPreview_Toggle(HWND parent)
{
    if (HWND window = Find(parent)) DestroyWindow(window);
    else {
        WNDCLASSW cls = {};
        cls.lpfnWndProc = WindowProc;
        cls.hInstance = GetModuleHandleW(nullptr);
        cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        cls.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        cls.lpszClassName = HostClass;
        RegisterClassW(&cls);
        CreateWindowExW(WS_EX_CLIENTEDGE, HostClass, L"Markdown Preview",
            WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN, 0, 0, 0, 0, parent, nullptr, cls.hInstance, nullptr);
    }
    RECT bounds;
    GetClientRect(parent, &bounds);
    SendMessageW(parent, WM_SIZE, SIZE_RESTORED, MAKELPARAM(bounds.right, bounds.bottom));
}
extern "C" int MarkdownPreview_Layout(HWND parent, int x, int y, int width, int height)
{
    HWND window = Find(parent);
    if (!window) return 0;
    const int reserved = width >= 320 ? width / 2 : width / 3;
    MoveWindow(window, x + width - reserved, y, reserved, height, TRUE);
    return reserved;
}
extern "C" void MarkdownPreview_Changed(HWND parent)
{
    if (HWND window = Find(parent)) SetTimer(window, RefreshTimer, 350, nullptr);
}
extern "C" void MarkdownPreview_Update(HWND parent, const char* text, size_t length, const WCHAR* directory)
{
    auto state = State(Find(parent));
    if (!state || !state->web) return;
    try {
        ComPtr<ICoreWebView2_3> mapping;
        if (SUCCEEDED(state->web.As(&mapping))) {
            mapping->ClearVirtualHostNameToFolderMapping(L"np3-preview.invalid");
            if (directory && *directory) mapping->SetVirtualHostNameToFolderMapping(
                L"np3-preview.invalid", directory, COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_DENY_CORS);
        }
        std::string html = length <= 2 * 1024 * 1024 ? np3::RenderMarkdown(std::string_view(text, length))
            : np3::RenderMarkdown("# Preview paused\n\nThis document exceeds the 2 MiB live preview limit.");
        const std::wstring wide = Wide(html);
        if (wide.empty() || FAILED(state->web->NavigateToString(wide.c_str()))) {
            Failed(state, L"Cannot display Markdown preview.");
        }
    } catch (...) { Failed(state, L"Cannot render Markdown preview."); }
}
