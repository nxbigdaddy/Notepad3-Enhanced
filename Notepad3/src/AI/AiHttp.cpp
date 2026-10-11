#include "AiHttp.h"
#include <winhttp.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <string_view>
#include <memory>
#include <mutex>
#include <string>
#pragma comment(lib, "winhttp.lib")
namespace {
struct Handle { HINTERNET value = nullptr; ~Handle() { if (value) WinHttpCloseHandle(value); } };
struct Client {
    HINTERNET session = nullptr;
    std::wstring host;
    INTERNET_PORT port = 0;
    INTERNET_SCHEME scheme{};
    std::mutex operation;
    ~Client() { if (session) WinHttpCloseHandle(session); }
};
struct State {
    HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    std::mutex mutex;
    DWORD status = 0, error = 0, bytes = 0;
    std::string body;
    std::wstring headers;
    std::array<char, 65536> buffer{};
    ~State() { if (!headers.empty()) SecureZeroMemory(&headers[0], headers.size() * sizeof(wchar_t)); if (event) CloseHandle(event); }
};
void CALLBACK Callback(HINTERNET, DWORD_PTR context, DWORD status, void* information, DWORD length) {
    auto* holder = reinterpret_cast<std::shared_ptr<State>*>(context); if (!holder) return;
    if (status == WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING) { delete holder; return; }
    if (status != WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE && status != WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE && status != WINHTTP_CALLBACK_STATUS_READ_COMPLETE && status != WINHTTP_CALLBACK_STATUS_REQUEST_ERROR) return;
    auto state = *holder;
    {
        std::lock_guard<std::mutex> lock(state->mutex); state->status = status; state->bytes = length;
        state->error = status == WINHTTP_CALLBACK_STATUS_REQUEST_ERROR ? static_cast<WINHTTP_ASYNC_RESULT*>(information)->dwError : 0;
    }
    SetEvent(state->event);
}
int Wait(const std::shared_ptr<State>& state, DWORD expected, const AI_HTTP_CONFIG_DATA& config, ULONGLONG started, AI_HTTP_RESULT* result, DWORD* bytes = nullptr) {
    while (true) {
        if (config.cancel && config.cancel(config.context)) return AI_HTTP_CANCELED;
        ULONGLONG elapsed = GetTickCount64() - started;
        if (elapsed >= config.timeoutMs) return AI_HTTP_TIMEOUT;
        DWORD waited = WaitForSingleObject(state->event, static_cast<DWORD>((std::min)(ULONGLONG(50), config.timeoutMs - elapsed)));
        if (waited == WAIT_TIMEOUT) continue;
        if (waited != WAIT_OBJECT_0) { result->winError = GetLastError(); return AI_HTTP_NETWORK; }
        std::lock_guard<std::mutex> lock(state->mutex);
        if (state->error) { result->winError = state->error; return state->error == ERROR_WINHTTP_TIMEOUT ? AI_HTTP_TIMEOUT : AI_HTTP_NETWORK; }
        if (state->status != expected) { result->winError = ERROR_INVALID_DATA; return AI_HTTP_NETWORK; }
        if (bytes) *bytes = state->bytes;
        return AI_HTTP_OK;
    }
}
int Failed(AI_HTTP_RESULT* result) { result->winError = GetLastError(); return result->winError == ERROR_WINHTTP_TIMEOUT ? AI_HTTP_TIMEOUT : AI_HTTP_NETWORK; }
}
extern "C" void* AiHttp_ClientCreate(const wchar_t* endpoint) {
    if (AiEndpoint_Validate(endpoint) != AI_OK) return nullptr;
    try {
        URL_COMPONENTS url = {}; url.dwStructSize = sizeof(url); url.dwHostNameLength = static_cast<DWORD>(-1);
        if (!WinHttpCrackUrl(endpoint, 0, 0, &url)) return nullptr;
        auto client = std::make_unique<Client>(); client->host.assign(url.lpszHostName, url.dwHostNameLength); client->port = url.nPort; client->scheme = url.nScheme;
        bool local = !_wcsicmp(client->host.c_str(), L"localhost") || client->host == L"127.0.0.1" || client->host == L"::1" || client->host == L"[::1]";
        client->session = WinHttpOpen(L"Notepad3-AI/1", local ? WINHTTP_ACCESS_TYPE_NO_PROXY : WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, WINHTTP_FLAG_ASYNC);
        return client->session ? client.release() : nullptr;
    } catch (...) { return nullptr; }
}
extern "C" void AiHttp_ClientFree(void* client) { delete static_cast<Client*>(client); }
extern "C" void AiHttp_Free(AI_HTTP_RESULT* result) { if (result) { AiProtocol_Free(result->text); *result = {}; } }
static int Run(const AI_HTTP_CONFIG_DATA* config, int action, const char* input, size_t length, const char* instruction, AI_HTTP_RESULT* result, bool models) {
    if (!result) return AI_HTTP_CONFIG; *result = {};
    if (!config || !config->client || AiEndpoint_Validate(config->endpoint) != AI_OK || config->timeoutMs < 100 || config->timeoutMs > 120000) return AI_HTTP_CONFIG;
    if (config->cancel && config->cancel(config->context)) return AI_HTTP_CANCELED;
    try {
        auto* client = static_cast<Client*>(config->client);
        std::unique_lock<std::mutex> operation(client->operation, std::try_to_lock);
        if (!operation.owns_lock()) return AI_HTTP_BUSY;
        auto state = std::make_shared<State>(); if (!state->event) return AI_HTTP_MEMORY;
        char* body = nullptr; size_t bodyLength = 0;
        int protocol = models ? AI_OK : AiProtocol_Request(config->model, action, input, length, instruction, &body, &bodyLength);
        std::unique_ptr<char, decltype(&AiProtocol_Free)> bodyOwner(body, AiProtocol_Free);
        if (protocol != AI_OK) { result->protocolStatus = protocol; return protocol == AI_MEMORY ? AI_HTTP_MEMORY : protocol == AI_LIMIT ? AI_HTTP_LIMIT : AI_HTTP_CONFIG; }
        if (bodyLength) state->body.assign(body, bodyLength);
        state->headers = L"Content-Type: application/json; charset=utf-8\r\nAccept: application/json\r\n";
        if (config->secret && *config->secret) {
            size_t secretLength = strlen(config->secret); if (secretLength > 2048) return AI_HTTP_CONFIG;
            state->headers += L"Authorization: Bearer ";
            for (unsigned char c : std::string_view(config->secret, secretLength)) { if (c < 33 || c > 126) return AI_HTTP_CONFIG; state->headers += c; }
            state->headers += L"\r\n";
        }
        URL_COMPONENTS url = {}; url.dwStructSize = sizeof(url); url.dwHostNameLength = url.dwUrlPathLength = url.dwExtraInfoLength = static_cast<DWORD>(-1);
        if (!WinHttpCrackUrl(config->endpoint, 0, 0, &url)) return Failed(result);
        std::wstring host(url.lpszHostName, url.dwHostNameLength), path(url.lpszUrlPath, url.dwUrlPathLength);
        if (path.empty()) path = L"/"; if (url.dwExtraInfoLength) path.append(url.lpszExtraInfo, url.dwExtraInfoLength);
        if (_wcsicmp(host.c_str(), client->host.c_str()) || url.nPort != client->port || url.nScheme != client->scheme) return AI_HTTP_CONFIG;
        if (!WinHttpSetTimeouts(client->session, config->timeoutMs, config->timeoutMs, config->timeoutMs, config->timeoutMs)) return Failed(result);
        Handle connection{WinHttpConnect(client->session, host.c_str(), url.nPort, 0)}; if (!connection.value) return Failed(result);
        Handle request{WinHttpOpenRequest(connection.value, models ? L"GET" : L"POST", path.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, url.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0)};
        if (!request.value) return Failed(result);
        DWORD redirect = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
        DWORD disable = WINHTTP_DISABLE_COOKIES | WINHTTP_DISABLE_AUTHENTICATION;
        if (!WinHttpSetOption(request.value, WINHTTP_OPTION_REDIRECT_POLICY, &redirect, sizeof(redirect)) || !WinHttpSetOption(request.value, WINHTTP_OPTION_DISABLE_FEATURE, &disable, sizeof(disable))) return Failed(result);
        if (WinHttpSetStatusCallback(request.value, Callback, WINHTTP_CALLBACK_FLAG_ALL_COMPLETIONS | WINHTTP_CALLBACK_FLAG_HANDLES, 0) == WINHTTP_INVALID_STATUS_CALLBACK) return Failed(result);
        auto holder = std::make_unique<std::shared_ptr<State>>(state); DWORD_PTR context = reinterpret_cast<DWORD_PTR>(holder.get());
        if (!WinHttpSetOption(request.value, WINHTTP_OPTION_CONTEXT_VALUE, &context, sizeof(context))) return Failed(result);
        // The final HANDLE_CLOSING callback owns this reference, including buffers used by pending operations.
        holder.release();
        ULONGLONG started = GetTickCount64();
        if (!WinHttpSendRequest(request.value, state->headers.c_str(), static_cast<DWORD>(state->headers.size()), state->body.empty() ? WINHTTP_NO_REQUEST_DATA : &state->body[0], static_cast<DWORD>(state->body.size()), static_cast<DWORD>(state->body.size()), context)) return Failed(result);
        int status = Wait(state, WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE, *config, started, result); if (status) return status;
        if (!WinHttpReceiveResponse(request.value, nullptr)) return Failed(result);
        status = Wait(state, WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE, *config, started, result); if (status) return status;
        DWORD size = sizeof(result->httpStatus);
        if (!WinHttpQueryHeaders(request.value, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &result->httpStatus, &size, WINHTTP_NO_HEADER_INDEX)) return Failed(result);
        if (result->httpStatus != 200) return AI_HTTP_SERVER;
        std::string response;
        while (true) {
            if (config->cancel && config->cancel(config->context)) return AI_HTTP_CANCELED;
            if (!WinHttpReadData(request.value, state->buffer.data(), static_cast<DWORD>(state->buffer.size()), nullptr)) return Failed(result);
            DWORD read = 0; status = Wait(state, WINHTTP_CALLBACK_STATUS_READ_COMPLETE, *config, started, result, &read); if (status) return status;
            if (!read) break;
            if (read > 2 * 1024 * 1024 - response.size()) return AI_HTTP_LIMIT;
            response.append(state->buffer.data(), read);
        }
        if (config->cancel && config->cancel(config->context)) return AI_HTTP_CANCELED;
        result->protocolStatus = models ? AiProtocol_Models(response.data(), response.size(), &result->text, &result->length) : AiProtocol_Response(response.data(), response.size(), &result->text, &result->length);
        bool canceled = config->cancel && config->cancel(config->context);
        if (canceled || GetTickCount64() - started >= config->timeoutMs) {
            AiProtocol_Free(result->text); result->text = nullptr; result->length = 0;
            return canceled ? AI_HTTP_CANCELED : AI_HTTP_TIMEOUT;
        }
        return result->protocolStatus == AI_OK ? AI_HTTP_OK : result->protocolStatus == AI_MEMORY ? AI_HTTP_MEMORY : result->protocolStatus == AI_LIMIT ? AI_HTTP_LIMIT : AI_HTTP_RESPONSE;
    } catch (...) { return AI_HTTP_MEMORY; }
}

extern "C" int AiHttp_Diagnostic(int status,const AI_HTTP_RESULT* result) {
    switch(status) {
    case AI_HTTP_OK:return AI_DIAG_OK;
    case AI_HTTP_CONFIG:return AI_DIAG_CONFIG;
    case AI_HTTP_CANCELED:return AI_DIAG_CANCELED;
    case AI_HTTP_TIMEOUT:return AI_DIAG_TIMEOUT;
    case AI_HTTP_NETWORK:return result && result->winError==ERROR_WINHTTP_SECURE_FAILURE?AI_DIAG_TLS:AI_DIAG_NETWORK;
    case AI_HTTP_LIMIT:return AI_DIAG_LIMIT;
    case AI_HTTP_MEMORY:return AI_DIAG_MEMORY;
    case AI_HTTP_BUSY:return AI_DIAG_BUSY;
    case AI_HTTP_RESPONSE:return result && result->protocolStatus==AI_INCOMPLETE?AI_DIAG_INCOMPLETE:AI_DIAG_RESPONSE;
    case AI_HTTP_SERVER:
        if(!result) return AI_DIAG_OTHER;
        switch(result->httpStatus) {
        case 401:return AI_DIAG_AUTH;
        case 403:return AI_DIAG_PERMISSION;
        case 402:return AI_DIAG_PAYMENT;
        case 429:return AI_DIAG_RATE;
        case 404:return AI_DIAG_ADDRESS_MODEL;
        case 400:case 422:return AI_DIAG_REQUEST;
        default:if(result->httpStatus>=500 && result->httpStatus<=599)return AI_DIAG_SERVER;
            if(result->httpStatus>=300 && result->httpStatus<=399)return AI_DIAG_REDIRECT;
            return AI_DIAG_OTHER;
        }
    default:return AI_DIAG_OTHER;
    }
}

extern "C" int AiHttp_Models(const AI_HTTP_CONFIG_DATA* config, AI_HTTP_RESULT* result) { return Run(config, 0, nullptr, 0, nullptr, result, true); }
extern "C" int AiHttp_Run(const AI_HTTP_CONFIG_DATA* config, int action, const char* input, size_t length, const char* instruction, AI_HTTP_RESULT* result) { return Run(config, action, input, length, instruction, result, false); }
