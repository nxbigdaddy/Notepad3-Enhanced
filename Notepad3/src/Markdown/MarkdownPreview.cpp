#include "MarkdownPreview.h"
#include "MarkdownRender.h"
#include "NavigationPolicy.h"
#include "PreviewLayout.h"
#include <windowsx.h>
#include <cmath>
#include <cwchar>
#include "vendor/webview2/WebView2.h"
#include <wrl.h>
#include <memory>
#include <string>
#include <vector>
#include <cstdio>

using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;
namespace {
constexpr wchar_t HostClass[] = L"NP3MarkdownPreview";
constexpr UINT_PTR RefreshTimer = 1;
void Trace(const char* event, HRESULT status = S_OK)
{
    wchar_t path[32768];
    const DWORD length = GetEnvironmentVariableW(L"NP3_MARKDOWN_DIAGNOSTICS", path, 32768);
    if (!length || length >= 32768) return;
    HANDLE file = CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;
    char line[160];
    const int count = sprintf_s(line, "%s HRESULT=0x%08lX\r\n", event, static_cast<unsigned long>(status));
    DWORD written;
    if (count > 0) WriteFile(file, line, static_cast<DWORD>(count), &written, nullptr);
    CloseHandle(file);
}
struct Preview {
    HWND window = nullptr;
    HWND parent = nullptr;
    HMODULE loader = nullptr;
    ComPtr<ICoreWebView2Controller> controller;
    ComPtr<ICoreWebView2> web;
    unsigned pendingRenderNavigations = 0;
    int widthRatio = 5000;
    int layoutX = 0, layoutWidth = 0;
    bool dragging = false;
    unsigned long long renderVersion = 0, navigationVersion = 0, navigationId = 0;
    double scrollToRestore = 0;
    bool loaded = false;
    std::wstring documentKey;
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
    Trace("preview-error", E_FAIL);
    InvalidateRect(state->window, nullptr, TRUE);
}
void Resize(const std::shared_ptr<Preview>& state)
{
    if (!state->window || !state->controller) return;
    RECT bounds;
    GetClientRect(state->window, &bounds);
    bounds.left = (std::min)(bounds.right, static_cast<LONG>(MulDiv(6, GetDpiForWindow(state->window), 96)));
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
                Trace("environment-ready", status);
                if (!state->window) return S_OK;
                if (FAILED(status) || !environment) {
                    Failed(state, L"Install the Microsoft Edge WebView2 Runtime to enable preview.");
                    return S_OK;
                }
                const HRESULT creation = environment->CreateCoreWebView2Controller(state->window,
                    Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                        [state](HRESULT status, ICoreWebView2Controller* controller) -> HRESULT {
                            Trace("controller-ready", status);
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
                            const std::weak_ptr<Preview> weakState = state;
                            state->web->add_NavigationCompleted(Callback<ICoreWebView2NavigationCompletedEventHandler>(
                                [weakState](ICoreWebView2*, ICoreWebView2NavigationCompletedEventArgs* args) -> HRESULT {
                                    BOOL success = FALSE;
                                    args->get_IsSuccess(&success);
                                    Trace("navigation-completed", success ? S_OK : E_FAIL);
                                    auto live = weakState.lock();
                                    UINT64 id = 0; args->get_NavigationId(&id);
                                    if (live && live->window && live->web && id == live->navigationId && live->navigationVersion == live->renderVersion) {
                                        live->loaded = success != FALSE;
                                        if (success) {
                                            wchar_t script[160];
                                            swprintf_s(script, L"window.scrollTo(0,%.0f)", live->scrollToRestore);
                                            const auto version = live->renderVersion;
                                            const HRESULT scrollResult = live->web->ExecuteScript(script,
                                                Callback<ICoreWebView2ExecuteScriptCompletedHandler>(
                                                    [weakState, version](HRESULT status, LPCWSTR) -> HRESULT {
                                                        auto active = weakState.lock();
                                                        if (active && active->window && active->renderVersion == version) Trace("scroll-restore-completed", status);
                                                        return S_OK;
                                                    }).Get());
                                            if (FAILED(scrollResult)) Trace("scroll-restore-start", scrollResult);
                                        }
                                    }
                                    COREWEBVIEW2_WEB_ERROR_STATUS error;
                                    if (!success && SUCCEEDED(args->get_WebErrorStatus(&error))) Trace("navigation-error-status", static_cast<HRESULT>(error));
                                    return S_OK;
                                }).Get(), &token);
                            state->web->add_NavigationStarting(Callback<ICoreWebView2NavigationStartingEventHandler>(
                                [weakState](ICoreWebView2*, ICoreWebView2NavigationStartingEventArgs* args) -> HRESULT {
                                    LPWSTR uri = nullptr;
                                    args->get_Uri(&uri);
                                    Trace(!uri ? "navigation-uri-null" : std::wstring(uri) == L"about:blank" ? "navigation-uri-blank" : std::wstring(uri).find(L"data:") == 0 ? "navigation-uri-data" : "navigation-uri-other");
                                    BOOL userInitiated = TRUE;
                                    args->get_IsUserInitiated(&userInitiated);
                                    auto live = weakState.lock();
                                    // NavigateToString reports a data:text/html URI in this Runtime.
                                    // Only host-issued renders can consume a pending navigation.
                                    const bool render = live && uri && np3::IsHostRenderNavigation(
                                        uri, userInitiated != FALSE, live->pendingRenderNavigations);
                                    if (render) {
                                        --live->pendingRenderNavigations;
                                        args->get_NavigationId(&live->navigationId);
                                        live->navigationVersion = live->renderVersion;
                                        live->loaded = false;
                                    }
                                    if (!render && (!uri || std::wstring(uri) != L"about:blank")) args->put_Cancel(TRUE);
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
    Trace("environment-start", result);
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
    case WM_SETCURSOR: {
        POINT point; GetCursorPos(&point); ScreenToClient(window, &point);
        if (point.x < MulDiv(6, GetDpiForWindow(window), 96)) {
            SetCursor(LoadCursorW(nullptr, IDC_SIZEWE)); return TRUE;
        }
        break;
    }
    case WM_LBUTTONDOWN:
        if (GET_X_LPARAM(lParam) < MulDiv(6, GetDpiForWindow(window), 96)) {
            state->dragging = true; SetCapture(window); return 0;
        }
        break;
    case WM_MOUSEMOVE:
        if (state->dragging && state->layoutWidth > 0) {
            POINT point; GetCursorPos(&point); ScreenToClient(state->parent, &point);
            state->widthRatio = np3::PreviewRatio(state->layoutWidth, state->layoutX + state->layoutWidth - point.x);
            RECT bounds; GetClientRect(state->parent, &bounds);
            SendMessageW(state->parent, WM_SIZE, SIZE_RESTORED, MAKELPARAM(bounds.right, bounds.bottom));
            return 0;
        }
        break;
    case WM_LBUTTONUP:
        if (state->dragging) { state->dragging = false; ReleaseCapture(); return 0; }
        break;
    case WM_CANCELMODE:
        if (GetCapture() == window) ReleaseCapture();
        [[fallthrough]];
    case WM_CAPTURECHANGED: state->dragging = false; break;
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
        RECT divider = bounds;
        divider.right = MulDiv(6, GetDpiForWindow(window), 96);
        FillRect(dc, &divider, GetSysColorBrush(COLOR_3DFACE));
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
    Trace("preview-toggle");
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
    auto state = State(window);
    if (!state) return 0;
    state->layoutX = x; state->layoutWidth = width;
    const int reserved = np3::PreviewWidth(width, state->widthRatio);
    MoveWindow(window, x + width - reserved, y, reserved, height, TRUE);
    return reserved;
}
extern "C" void MarkdownPreview_Changed(HWND parent)
{
    if (HWND window = Find(parent)) SetTimer(window, RefreshTimer, 350, nullptr);
}
extern "C" void MarkdownPreview_Update(HWND parent, const char* text, size_t length, const WCHAR* directory, const WCHAR* documentKey)
{
    auto state = State(Find(parent));
    if (!state || !state->web) return;
    try {
        std::string html = length <= 2 * 1024 * 1024 ? np3::RenderMarkdown(std::string_view(text, length))
            : np3::RenderMarkdown("# Preview paused\n\nThis document exceeds the 2 MiB live preview limit.");
        const std::wstring wide = Wide(html);
        if (wide.empty()) { Failed(state, L"Cannot render Markdown preview."); return; }
        const std::wstring folder = directory ? directory : L"";
        const std::wstring key = documentKey ? documentKey : L"";
        const bool sameDocument = key == state->documentKey;
        const bool preserveScroll = state->loaded && sameDocument;
        state->documentKey = key;
        const auto version = ++state->renderVersion;
        const std::weak_ptr<Preview> weakState = state;
        auto navigate = [weakState, version, wide, folder](double scroll) {
            auto live = weakState.lock();
            if (!live || !live->window || !live->web || live->renderVersion != version) return;
            ComPtr<ICoreWebView2_3> mapping;
            if (SUCCEEDED(live->web.As(&mapping))) {
                mapping->ClearVirtualHostNameToFolderMapping(L"np3-preview.invalid");
                if (!folder.empty()) mapping->SetVirtualHostNameToFolderMapping(
                    L"np3-preview.invalid", folder.c_str(), COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_DENY_CORS);
            }
            live->scrollToRestore = scroll;
            ++live->pendingRenderNavigations;
            if (FAILED(live->web->NavigateToString(wide.c_str()))) {
                --live->pendingRenderNavigations;
                Failed(live, L"Cannot display Markdown preview.");
            }
        };
        if (preserveScroll) {
            const HRESULT result = state->web->ExecuteScript(L"window.scrollY", Callback<ICoreWebView2ExecuteScriptCompletedHandler>(
                [navigate, previous = state->scrollToRestore](HRESULT status, LPCWSTR json) -> HRESULT {
                    double scroll = previous;
                    if (SUCCEEDED(status) && json) {
                        wchar_t* end = nullptr;
                        const double value = std::wcstod(json, &end);
                        if (end != json && *end == 0 && std::isfinite(value) && value >= 0 && value <= 1e9) scroll = value;
                    }
                    navigate(scroll);
                    return S_OK;
                }).Get());
            if (FAILED(result)) navigate(state->scrollToRestore);
        } else navigate(sameDocument ? state->scrollToRestore : 0);
    } catch (...) { Failed(state, L"Cannot render Markdown preview."); }
}
