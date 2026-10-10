#include "../Notepad3/src/AI/AiCore.h"
#include "../Notepad3/src/TextTools/JsonFormat.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <stdexcept>
int checks = 0;
void expect(bool condition, const char* label) { ++checks; if (!condition) throw std::runtime_error(label); }
int main() {
    char* output = nullptr; size_t length = 0;
    std::wstring profile = L"test-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64());
    bool created = false;
    auto response = [&](const std::string& json, int expected) { AiProtocol_Free(output); output = nullptr; expect(AiProtocol_Response(json.data(), json.size(), &output, &length) == expected, "response status"); };
    try {
        expect(AiEndpoint_Validate(L"https://example.test/v1/chat/completions") == AI_OK, "HTTPS service endpoint");
        expect(AiEndpoint_Validate(L"http://127.0.0.1:18888/v1/chat/completions") == AI_OK, "loopback local model endpoint");
        expect(AiEndpoint_Validate(L"http://localhost/v1/chat/completions") == AI_OK, "localhost endpoint");
        expect(AiEndpoint_Validate(L"http://remote.test/v1/chat/completions") == AI_CONFIG, "remote plaintext rejected");
        expect(AiEndpoint_Validate(L"https://user:password@example.test/v1") == AI_CONFIG, "userinfo credential rejected");
        expect(AiEndpoint_Validate(L"https://example.test/v1#fragment") == AI_CONFIG, "fragment rejected");
        expect(AiEndpoint_Validate(L"https://example.test/\r\n") == AI_CONFIG, "URL controls rejected");
        wchar_t serviceA[73] = {}, serviceB[73] = {}, serviceAgain[73] = {};
        expect(AiCredential_ServiceProfile(L"https://example.test/v1", serviceA, 73) == ERROR_SUCCESS && wcslen(serviceA) == 72, "service profile fixed length");
        expect(AiCredential_ServiceProfile(L"https://example.test/v1", serviceAgain, 73) == ERROR_SUCCESS && !wcscmp(serviceA, serviceAgain), "service profile stable across calls");
        expect(AiCredential_ServiceProfile(L"https://other.test/v1", serviceB, 73) == ERROR_SUCCESS && wcscmp(serviceA, serviceB), "different service isolates credentials");
        expect(AiCredential_ServiceProfile(L"https://example.test/v2", serviceB, 73) == ERROR_SUCCESS && wcscmp(serviceA, serviceB), "different endpoint path isolates credentials");
        expect(AiCredential_ServiceProfile(L"https://example.test/v1", serviceB, 72) == ERROR_INVALID_PARAMETER, "short profile buffer rejected");
        expect(AiCredential_ServiceProfile(L"http://remote.test/v1", serviceB, 73) == ERROR_INVALID_PARAMETER, "unsafe service has no credential profile");
        const std::string input = "中文\n\"quote\"\\path";
        for (int action = AI_EXPLAIN; action <= AI_GENERATE; ++action) {
            expect(AiProtocol_Request("custom-model", action, input.data(), input.size(), "Keep headings.", &output, &length) == AI_OK, "four actions generate valid request");
            char* compact = nullptr; size_t compactLength = 0, error = 0;
            expect(JsonFormat(output, length, 1, "", &compact, &compactLength, &error) == 0, "request strict JSON validates");
            expect(std::string(output, length).find("\\u000a") != std::string::npos && std::string(output, length).find("\\\"quote\\\"") != std::string::npos, "request escapes original source");
            JsonFormat_Free(compact); AiProtocol_Free(output); output = nullptr;
        }
        expect(AiProtocol_Request("model", 99, "x", 1, nullptr, &output, &length) == AI_CONFIG, "invalid action");
        expect(AiProtocol_Request("model", AI_EXPLAIN, "\xFF", 1, nullptr, &output, &length) == AI_TEXT, "invalid source UTF8");
        std::string large(256 * 1024 + 1, 'x');
        expect(AiProtocol_Request("model", AI_EXPLAIN, large.data(), large.size(), nullptr, &output, &length) == AI_LIMIT, "source cap");
        response(R"({"choices":[{"message":{"content":"中文\ntext\uD83D\uDE00"},"finish_reason":"stop"}]})", AI_OK);
        expect(std::string(output, length) == "中文\ntext😀", "Unicode escapes and surrogate pair decoded");
        response(R"({"choices":[{"message":{"content":"first"}},{"message":{"content":"second"}}]})", AI_OK);
        expect(std::string(output) == "first", "only first completion selected");
        response(R"({"choices":[{"message":{"content":"partial"},"finish_reason":"length"}]})", AI_INCOMPLETE);
        response(R"({"choices":[{"message":{"content":null},"finish_reason":"tool_calls"}]})", AI_RESPONSE);
        response(R"({"choices":[{"message":{"content":""}}]})", AI_RESPONSE);
        response(R"({"choices":[{"message":{"content":"\uD800"}}]})", AI_RESPONSE);
        response(R"({"choices":[{"message":{"content":"\uDC00"}}]})", AI_RESPONSE);
        response(R"({"choices":[{"message":{"content":"\u0000"}}]})", AI_RESPONSE);
        response(R"({"choices":[{"message":{"content":"a","content":"b"}}]})", AI_RESPONSE);
        response(R"({"choices":[{"message":{"content":"a"},"finish_reason":"stop","finish_reason":"length"}]})", AI_RESPONSE);
        response(R"({"choices":[]})", AI_RESPONSE);
        response("{broken", AI_RESPONSE);
        response(std::string(2 * 1024 * 1024 + 1, 'x'), AI_LIMIT);
        char* secret = nullptr;
        expect(AiCredential_Load(profile.c_str(), &secret) == ERROR_NOT_FOUND, "isolated credential initially absent");
        expect(AiCredential_Save(profile.c_str(), "fake-test-key-1") == ERROR_SUCCESS, "write Windows credential"); created = true;
        expect(AiCredential_Load(profile.c_str(), &secret) == ERROR_SUCCESS && !strcmp(secret, "fake-test-key-1"), "independent credential readback"); AiCredential_Free(secret); secret = nullptr;
        expect(AiCredential_Save(profile.c_str(), "fake\r\nheader") == ERROR_INVALID_PARAMETER, "header injection rejected");
        expect(AiCredential_Load(profile.c_str(), &secret) == ERROR_SUCCESS && !strcmp(secret, "fake-test-key-1"), "invalid write preserves existing key"); AiCredential_Free(secret); secret = nullptr;
        expect(AiCredential_Save(profile.c_str(), "fake-test-key-2") == ERROR_SUCCESS, "update own credential");
        expect(AiCredential_Load(profile.c_str(), &secret) == ERROR_SUCCESS && !strcmp(secret, "fake-test-key-2"), "updated key persists"); AiCredential_Free(secret); secret = nullptr;
        expect(AiCredential_Save(L"../invalid", "fake") == ERROR_INVALID_PARAMETER, "invalid profile rejected");
        expect(AiCredential_Delete(profile.c_str()) == ERROR_SUCCESS, "delete isolated test credential"); created = false;
        expect(AiCredential_Load(profile.c_str(), &secret) == ERROR_NOT_FOUND && !secret, "deletion independently verified");
        std::printf("PASS %d AI protocol and credential checks\n", checks); return 0;
    } catch (const std::exception& error) { AiProtocol_Free(output); if (created) AiCredential_Delete(profile.c_str()); std::fprintf(stderr, "FAIL %s after %d checks\n", error.what(), checks); return 1; }
}
