#include "../Notepad3/src/AI/AiHttp.h"
#include <cstdio>
#include <string>
#include <stdexcept>
int checks = 0;
void expect(bool condition, const char* label) { ++checks; if (!condition) throw std::runtime_error(label); }
struct Cancellation { ULONGLONG start; DWORD after; };
int cancel(void* context) { auto* token = static_cast<Cancellation*>(context); return GetTickCount64() - token->start >= token->after; }
int wmain(int argc, wchar_t** argv) {
    if (argc != 3) return 2;
    AI_HTTP_RESULT result{};
    AI_HTTP_CONFIG_DATA config{}; config.model = "test-model"; config.secret = "fake-local-test-key"; config.timeoutMs = 5000;
    std::wstring base = argv[1], endpoint;
    void* client = AiHttp_ClientCreate(base.c_str()), *tlsClient = AiHttp_ClientCreate(argv[2]);
    config.client = client;
    auto run = [&](const wchar_t* route) {
        AiHttp_Free(&result); endpoint = base + route; config.endpoint = endpoint.c_str();
        return AiHttp_Run(&config, AI_EXPLAIN, "中文 input", sizeof("中文 input") - 1, nullptr, &result);
    };
    try {
        expect(client && tlsClient, "service client sessions created");
        endpoint=base+L"/models";config.endpoint=endpoint.c_str();config.model=nullptr;
        expect(AiHttp_Models(&config,&result)==AI_HTTP_OK && std::string(result.text,result.length)=="one\n中文\n","actual authenticated GET model list decoded without model input");
        AiHttp_Free(&result);endpoint=base+L"/models-denied";config.endpoint=endpoint.c_str();
        expect(AiHttp_Models(&config,&result)==AI_HTTP_SERVER && result.httpStatus==401 && !result.text,"model list auth failure publishes no IDs");
        config.model="test-model";
        expect(run(L"/ok") == AI_HTTP_OK, "local HTTP completion succeeds");
        expect(result.httpStatus == 200 && std::string(result.text, result.length) == "模拟中文\n😀", "actual HTTP body decoded correctly");
        expect(run(L"/denied") == AI_HTTP_SERVER && result.httpStatus == 401 && !result.text, "authorization error retains HTTP code without fake content");
        expect(AiHttp_Diagnostic(AI_HTTP_SERVER,&result)==AI_DIAG_AUTH,"actual HTTP authentication result is classified");
        expect(run(L"/redirect") == AI_HTTP_SERVER && result.httpStatus == 302 && !result.text, "redirect is not followed");
        expect(AiHttp_Diagnostic(AI_HTTP_SERVER,&result)==AI_DIAG_REDIRECT,"actual redirect remains an address diagnostic");
        expect(run(L"/broken") == AI_HTTP_RESPONSE && !result.text, "malformed HTTP response JSON rejected");
        expect(run(L"/incomplete") == AI_HTTP_RESPONSE && result.protocolStatus == AI_INCOMPLETE && !result.text, "truncated model response not successful output");
        expect(AiHttp_Diagnostic(AI_HTTP_RESPONSE,&result)==AI_DIAG_INCOMPLETE,"actual truncated result differs from malformed response");
        expect(run(L"/oversize") == AI_HTTP_LIMIT && !result.text, "actual download byte limit");
        expect(AiHttp_Diagnostic(AI_HTTP_LIMIT,&result)==AI_DIAG_LIMIT,"actual response limit is classified");
        config.timeoutMs = 250; ULONGLONG start = GetTickCount64();
        expect(run(L"/slow") == AI_HTTP_TIMEOUT && GetTickCount64() - start < 2000, "overall deadline interrupts pending response");
        expect(AiHttp_Diagnostic(AI_HTTP_TIMEOUT,&result)==AI_DIAG_TIMEOUT,"actual deadline failure is classified");
        config.timeoutMs = 5000; Cancellation token{GetTickCount64(), 150}; config.cancel = cancel; config.context = &token;
        start = GetTickCount64();
        expect(run(L"/slow") == AI_HTTP_CANCELED && GetTickCount64() - start < 1500 && !result.text, "cancel while waiting for server without waiting for timeout");
        token = {GetTickCount64(), 180};
        expect(run(L"/drip") == AI_HTTP_CANCELED && !result.text, "cancel during response body download");
        token = {GetTickCount64(), 0};
        expect(run(L"/ok") == AI_HTTP_CANCELED, "pre-canceled operation does not send request");
        config.cancel = nullptr; config.context = nullptr;
        config.secret = "bad\r\nheader";
        expect(run(L"/ok") == AI_HTTP_CONFIG, "header injection blocked before network send");
        config.secret = "fake-local-test-key";
        expect(run(L"/ok") == AI_HTTP_OK, "new request succeeds after canceled callbacks");
        AiHttp_Free(&result); endpoint = std::wstring(argv[2]) + L"/ok"; config.endpoint = endpoint.c_str(); config.client = client;
        expect(AiHttp_Run(&config, AI_EXPLAIN, "中文 input", sizeof("中文 input") - 1, nullptr, &result) == AI_HTTP_CONFIG, "service client cannot be reused for another origin");
        config.client = tlsClient;
        expect(AiHttp_Run(&config, AI_EXPLAIN, "中文 input", sizeof("中文 input") - 1, nullptr, &result) == AI_HTTP_NETWORK && !result.text && result.winError != 0, "untrusted HTTPS certificate is rejected");
        config.client = client;
        // Warm WinHTTP/Crypto runtime caches before measuring per-request resource growth.
        DWORD counts[3] = {0}; bool repeatSuccess = true;
        for (int batch = 0; batch < 3; ++batch) {
            for (int i = 0; i < 20; ++i) repeatSuccess = run(L"/ok") == AI_HTTP_OK && repeatSuccess;
            Sleep(500); GetProcessHandleCount(GetCurrentProcess(), &counts[batch]);
            std::printf("HANDLE requests=%d count=%lu\n", (batch + 1) * 20, counts[batch]);
        }
        expect(repeatSuccess, "repeated asynchronous requests complete");
        expect(counts[2] <= counts[0] + 8, "completed callbacks release request state handles after runtime warmup");
        AiHttp_Free(&result);
        expect(!result.text && result.length == 0, "transport result release clears ownership");
        // Let asynchronous HANDLE_CLOSING callbacks finish before the test process exits.
        Sleep(200);
        AiHttp_ClientFree(client); AiHttp_ClientFree(tlsClient);
        std::printf("PASS %d AI HTTP checks\n", checks); return 0;
    } catch (const std::exception& error) { DWORD http = result.httpStatus, win = result.winError; int protocol = result.protocolStatus; AiHttp_Free(&result); Sleep(200); AiHttp_ClientFree(client); AiHttp_ClientFree(tlsClient); std::fprintf(stderr, "FAIL %s after %d checks; http=%lu win=%lu protocol=%d\n", error.what(), checks, http, win, protocol); return 1; }
}
