#include "MarkdownPdf.h"
#include "MarkdownRender.h"
#include "MarkdownExport.h"
#include "vendor/webview2/WebView2.h"
#include <wrl.h>
#include <memory>
#include <string>
#include <stdexcept>
#include <cstdio>
using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;
namespace {
constexpr wchar_t ClassName[] = L"NP3MarkdownPdfJob";
struct Job {
    HWND window = nullptr, parent = nullptr;
    HMODULE loader = nullptr;
    ComPtr<ICoreWebView2Environment> environment;
    ComPtr<ICoreWebView2Controller> controller;
    ComPtr<ICoreWebView2> web;
    std::wstring folder, target, temporary, staging, index;
    bool done = false, printing = false, adopted = false, ownsTemporary = false, ownsStaging = false;
    ~Job() {
        if (ownsTemporary) DeleteFileW(temporary.c_str());
        if (ownsStaging) { DeleteFileW(index.c_str()); RemoveDirectoryW(staging.c_str()); }
        if (loader) FreeLibrary(loader);
    }
};
std::wstring Absolute(const wchar_t* path) {
    DWORD size = GetFullPathNameW(path, 0, nullptr, nullptr);
    if (!size || size >= 32768) throw std::invalid_argument("Invalid path");
    std::wstring full(size, L'\0'); DWORD written = GetFullPathNameW(path, size, full.data(), nullptr);
    if (!written || written >= size) throw std::invalid_argument("Invalid path");
    full.resize(written); return full;
}
void Finish(const std::shared_ptr<Job>& job, uint32_t error) {
    if (job->done) return;
    job->done = true;
    if (job->controller) job->controller->Close();
    job->web.Reset(); job->controller.Reset();
    if (job->ownsTemporary) DeleteFileW(job->temporary.c_str());
    if (job->ownsStaging) { DeleteFileW(job->index.c_str()); RemoveDirectoryW(job->staging.c_str()); }
    if (IsWindow(job->parent)) PostMessageW(job->parent, NP3_WM_MARKDOWN_PDF_DONE, error, 0);
    if (job->window) DestroyWindow(job->window);
}
uint32_t Status(HRESULT status) { return FAILED(status) ? static_cast<uint32_t>(status) : ERROR_GEN_FAILURE; }
void Print(const std::shared_ptr<Job>& job) {
    if (job->done || job->printing) return;
    job->printing = true;
    ComPtr<ICoreWebView2_7> printable;
    HRESULT status = job->web.As(&printable);
    if (FAILED(status)) { Finish(job, Status(status)); return; }
    ComPtr<ICoreWebView2Environment6> environment;
    ComPtr<ICoreWebView2PrintSettings> settings;
    if (FAILED(job->environment.As(&environment)) || FAILED(environment->CreatePrintSettings(&settings)) ||
        FAILED(settings->put_PageWidth(210.0 / 25.4)) || FAILED(settings->put_PageHeight(297.0 / 25.4)) ||
        FAILED(settings->put_MarginTop(18.0 / 25.4)) || FAILED(settings->put_MarginBottom(18.0 / 25.4)) ||
        FAILED(settings->put_MarginLeft(18.0 / 25.4)) || FAILED(settings->put_MarginRight(18.0 / 25.4)) ||
        FAILED(settings->put_ShouldPrintBackgrounds(TRUE)) || FAILED(settings->put_ShouldPrintHeaderAndFooter(FALSE))) {
        Finish(job, ERROR_NOT_SUPPORTED); return;
    }
    auto weak = std::weak_ptr<Job>(job);
    status = printable->PrintToPdf(job->temporary.c_str(), settings.Get(), Callback<ICoreWebView2PrintToPdfCompletedHandler>(
        [weak](HRESULT result, BOOL success) -> HRESULT {
            auto job = weak.lock(); if (!job || job->done) return S_OK;
            uint32_t error = FAILED(result) || !success ? Status(result) : 0;
            if (!error) {
                HANDLE file = CreateFileW(job->temporary.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
                char signature[5] = {}; DWORD read = 0;
                if (file == INVALID_HANDLE_VALUE || !ReadFile(file, signature, sizeof(signature), &read, nullptr) || read != sizeof(signature) || memcmp(signature, "%PDF-", 5)) error = ERROR_INVALID_DATA;
                if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
                if (!error && !MoveFileExW(job->temporary.c_str(), job->target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) error = GetLastError();
            }
            Finish(job, error); return S_OK;
        }).Get());
    if (FAILED(status)) Finish(job, Status(status));
}
void Start(const std::shared_ptr<Job>& job) {
    wchar_t module[32768]; DWORD size = GetModuleFileNameW(nullptr, module, 32768);
    if (!size || size >= 32768) { Finish(job, ERROR_PATH_NOT_FOUND); return; }
    std::wstring loader(module, size); loader.resize(loader.find_last_of(L"\\/") + 1); loader += L"WebView2Loader.dll";
    job->loader = LoadLibraryExW(loader.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (!job->loader) { Finish(job, GetLastError()); return; }
    using CreateEnvironment = HRESULT(STDAPICALLTYPE*)(PCWSTR, PCWSTR, ICoreWebView2EnvironmentOptions*, ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler*);
    auto create = reinterpret_cast<CreateEnvironment>(GetProcAddress(job->loader, "CreateCoreWebView2EnvironmentWithOptions"));
    if (!create) { Finish(job, ERROR_PROC_NOT_FOUND); return; }
    wchar_t temp[32768]; DWORD count = GetTempPathW(32768, temp);
    if (!count || count >= 32768) { Finish(job, ERROR_PATH_NOT_FOUND); return; }
    std::wstring data = std::wstring(temp) + L"Notepad3-Markdown-PDF-WebView2";
    HRESULT status = create(nullptr, data.c_str(), nullptr, Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
        [job](HRESULT result, ICoreWebView2Environment* environment) -> HRESULT {
            if (job->done) return S_OK;
            if (FAILED(result) || !environment) { Finish(job, Status(result)); return S_OK; }
            job->environment = environment;
            HRESULT status = environment->CreateCoreWebView2Controller(job->window, Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                [job](HRESULT result, ICoreWebView2Controller* controller) -> HRESULT {
                    if (job->done) { if (controller) controller->Close(); return S_OK; }
                    if (FAILED(result) || !controller) { Finish(job, Status(result)); return S_OK; }
                    job->controller = controller; controller->get_CoreWebView2(&job->web);
                    if (!job->web) { Finish(job, ERROR_NOT_SUPPORTED); return S_OK; }
                    RECT bounds = { 0, 0, 1024, 768 }; controller->put_Bounds(bounds); controller->put_IsVisible(FALSE);
                    ComPtr<ICoreWebView2Settings> settings; job->web->get_Settings(&settings);
                    if (settings) { settings->put_IsScriptEnabled(FALSE); settings->put_AreDevToolsEnabled(FALSE); settings->put_AreDefaultContextMenusEnabled(FALSE); settings->put_IsWebMessageEnabled(FALSE); }
                    ComPtr<ICoreWebView2_3> mapping;
                    if (FAILED(job->web.As(&mapping)) || FAILED(mapping->SetVirtualHostNameToFolderMapping(L"np3-preview.invalid", job->folder.c_str(), COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_DENY_CORS))) { Finish(job, ERROR_NOT_SUPPORTED); return S_OK; }
                    if (FAILED(mapping->SetVirtualHostNameToFolderMapping(L"np3-export.invalid", job->staging.c_str(), COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_DENY_CORS))) { Finish(job, ERROR_NOT_SUPPORTED); return S_OK; }
                    auto weak = std::weak_ptr<Job>(job); EventRegistrationToken token;
                    job->web->add_NavigationStarting(Callback<ICoreWebView2NavigationStartingEventHandler>([](ICoreWebView2*, ICoreWebView2NavigationStartingEventArgs* args) -> HRESULT {
                        LPWSTR uri = nullptr; args->get_Uri(&uri);
                        if (!uri || wcscmp(uri, L"https://np3-export.invalid/index.html")) args->put_Cancel(TRUE);
                        CoTaskMemFree(uri); return S_OK;
                    }).Get(), &token);
                    job->web->add_NavigationCompleted(Callback<ICoreWebView2NavigationCompletedEventHandler>([weak](ICoreWebView2*, ICoreWebView2NavigationCompletedEventArgs* args) -> HRESULT {
                        auto job = weak.lock(); if (!job || job->done) return S_OK;
                        BOOL success = FALSE; args->get_IsSuccess(&success);
                        if (success) Print(job); else Finish(job, ERROR_INVALID_DATA);
                        return S_OK;
                    }).Get(), &token);
                    HRESULT status = job->web->Navigate(L"https://np3-export.invalid/index.html");
                    if (FAILED(status)) Finish(job, Status(status));
                    return S_OK;
                }).Get());
            if (FAILED(status)) Finish(job, Status(status));
            return S_OK;
        }).Get());
    if (FAILED(status)) Finish(job, Status(status));
}
LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* holder = reinterpret_cast<std::shared_ptr<Job>*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
        auto* owner = static_cast<std::shared_ptr<Job>*>(create->lpCreateParams); (*owner)->adopted = true;
    } else if (message == WM_TIMER && holder) Finish(*holder, ERROR_TIMEOUT);
    else if (message == WM_NCDESTROY && holder) {
        auto job = *holder; job->window = nullptr; job->done = true;
        if (job->controller) job->controller->Close();
        job->web.Reset(); job->controller.Reset();
        if (job->ownsTemporary) DeleteFileW(job->temporary.c_str());
        if (job->ownsStaging) { DeleteFileW(job->index.c_str()); RemoveDirectoryW(job->staging.c_str()); }
        SetWindowLongPtrW(window, GWLP_USERDATA, 0); delete holder;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}
}
extern "C" BOOL MarkdownPdf_IsBusy(HWND parent) { return FindWindowExW(parent, nullptr, ClassName, nullptr) != nullptr; }
extern "C" int MarkdownPdf_Start(HWND parent, const char* text, size_t length, const wchar_t* directory, const wchar_t* destination, uint32_t* error) {
    if (!error) return 0;
    *error = 0;
    if (!IsWindow(parent) || (!text && length) || !directory || !destination) { *error = ERROR_INVALID_PARAMETER; return 0; }
    if (MarkdownPdf_IsBusy(parent)) { *error = ERROR_BUSY; return 0; }
    try {
        auto job = std::make_shared<Job>(); job->parent = parent; job->folder = Absolute(directory); job->target = Absolute(destination);
        auto html = np3::RenderMarkdownExport(std::string_view(text ? text : "", length), job->folder);
        auto base = html.find("<base href="); auto end = html.find('>', base);
        html.replace(base, end - base + 1, "<base href=\"https://np3-preview.invalid/\">");
        auto image = html.find("img-src file: data:"); html.replace(image, strlen("img-src file: data:"), "img-src https://np3-preview.invalid data:");
        auto uri = html.find("base-uri file:"); html.replace(uri, strlen("base-uri file:"), "base-uri https://np3-preview.invalid");
        wchar_t temp[32768]; DWORD count = GetTempPathW(32768, temp);
        if (!count || count >= 32768) { *error = ERROR_PATH_NOT_FOUND; return 0; }
        job->staging = std::wstring(temp) + L"np3-pdf-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64());
        if (!CreateDirectoryW(job->staging.c_str(), nullptr)) { *error = GetLastError(); return 0; }
        job->ownsStaging = true;
        job->index = job->staging + L"\\index.html";
        HANDLE file = CreateFileW(job->index.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        DWORD written = 0;
        bool saved = file != INVALID_HANDLE_VALUE && WriteFile(file, html.data(), static_cast<DWORD>(html.size()), &written, nullptr) && written == html.size() && FlushFileBuffers(file);
        if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
        if (!saved) { *error = ERROR_WRITE_FAULT; DeleteFileW(job->index.c_str()); RemoveDirectoryW(job->staging.c_str()); return 0; }
        job->temporary = job->target + L".tmp-pdf-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64());
        HANDLE reserved = CreateFileW(job->temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (reserved == INVALID_HANDLE_VALUE) { *error = GetLastError(); DeleteFileW(job->index.c_str()); RemoveDirectoryW(job->staging.c_str()); return 0; } CloseHandle(reserved);
        job->ownsTemporary = true;
        WNDCLASSW cls = {}; cls.hInstance = GetModuleHandleW(nullptr); cls.lpszClassName = ClassName; cls.lpfnWndProc = WindowProc; RegisterClassW(&cls);
        auto* holder = new std::shared_ptr<Job>(job);
        job->window = CreateWindowExW(0, ClassName, L"Markdown PDF", WS_CHILD, 0, 0, 1024, 768, parent, nullptr, cls.hInstance, holder);
        if (!job->window) { if (!job->adopted) delete holder; DeleteFileW(job->temporary.c_str()); DeleteFileW(job->index.c_str()); RemoveDirectoryW(job->staging.c_str()); *error = GetLastError(); return 0; }
        SetTimer(job->window, 1, 60000, nullptr);
        Start(job); return 1;
    } catch (const std::length_error&) { *error = ERROR_FILE_TOO_LARGE; }
    catch (...) { *error = ERROR_INVALID_DATA; }
    return 0;
}
