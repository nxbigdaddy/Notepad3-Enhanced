#include "AiCore.h"
#include "../TextTools/JsonFormat.h"
#include <winhttp.h>
#include <wincred.h>
#include <bcrypt.h>
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <string_view>
#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "bcrypt.lib")
namespace {
bool Utf8(std::string_view text) {
    return text.find('\0') == std::string_view::npos && (text.empty() || MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0) > 0);
}
std::string Quote(std::string_view text) {
    std::string value = "\"";
    constexpr char hex[] = "0123456789abcdef";
    for (unsigned char c : text) {
        if (c == '"' || c == '\\') { value += '\\'; value += c; }
        else if (c < 32) { value += "\\u00"; value += hex[c >> 4]; value += hex[c & 15]; }
        else value += c;
    }
    value += '"'; return value;
}
int Copy(const std::string& value, char** output, size_t* length) {
    *output = static_cast<char*>(std::malloc(value.size() + 1)); if (!*output) return AI_MEMORY;
    std::memcpy(*output, value.data(), value.size()); (*output)[value.size()] = 0; *length = value.size(); return AI_OK;
}
struct BadJson {};
class Reader {
    std::string_view text; size_t position = 0;
    unsigned Hex() {
        unsigned value = 0;
        for (int i = 0; i < 4; ++i) {
            if (position == text.size()) throw BadJson{};
            char c = text[position++];
            unsigned digit = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : 99;
            if (digit > 15) throw BadJson{}; value = value * 16 + digit;
        }
        return value;
    }
    static void Codepoint(std::string& value, unsigned code) {
        if (code < 128) value += static_cast<char>(code);
        else if (code < 2048) { value += static_cast<char>(192 | (code >> 6)); value += static_cast<char>(128 | (code & 63)); }
        else if (code < 65536) { value += static_cast<char>(224 | (code >> 12)); value += static_cast<char>(128 | ((code >> 6) & 63)); value += static_cast<char>(128 | (code & 63)); }
        else { value += static_cast<char>(240 | (code >> 18)); value += static_cast<char>(128 | ((code >> 12) & 63)); value += static_cast<char>(128 | ((code >> 6) & 63)); value += static_cast<char>(128 | (code & 63)); }
    }
    char Peek() const { if (position >= text.size()) throw BadJson{}; return text[position]; }
    void Require(char c) { if (Peek() != c) throw BadJson{}; ++position; }
    std::string String() {
        Require('"'); std::string value;
        while (Peek() != '"') {
            char c = text[position++];
            if (c != '\\') { value += c; continue; }
            c = Peek(); ++position;
            switch (c) {
            case '"': case '\\': case '/': value += c; break;
            case 'b': value += '\b'; break; case 'f': value += '\f'; break; case 'n': value += '\n'; break; case 'r': value += '\r'; break; case 't': value += '\t'; break;
            case 'u': {
                unsigned code = Hex();
                if (code >= 0xD800 && code <= 0xDBFF) { Require('\\'); Require('u'); unsigned low = Hex(); if (low < 0xDC00 || low > 0xDFFF) throw BadJson{}; code = 0x10000 + ((code - 0xD800) << 10) + low - 0xDC00; }
                else if (code >= 0xDC00 && code <= 0xDFFF) throw BadJson{};
                Codepoint(value, code); break;
            }
            default: throw BadJson{};
            }
        }
        ++position; return value;
    }
    void Value(const char* const* path, size_t step, size_t count, std::string& result, unsigned& matches, unsigned depth) {
        if (depth > 128) throw BadJson{};
        if (step == count && Peek() != '"') throw BadJson{};
        if (step == count && Peek() == '"') { result = String(); ++matches; return; }
        if (Peek() == '{') {
            ++position;
            if (Peek() != '}') while (true) {
                std::string key = String(); Require(':');
                bool selected = path && step < count && key == path[step];
                Value(selected ? path : nullptr, selected ? step + 1 : 0, selected ? count : SIZE_MAX, result, matches, depth + 1);
                if (Peek() != ',') break; ++position;
            }
            Require('}');
        } else if (Peek() == '[') {
            ++position; size_t index = 0;
            if (Peek() != ']') while (true) {
                bool selected = path && step < count && index == 0 && !strcmp(path[step], "0");
                Value(selected ? path : nullptr, selected ? step + 1 : 0, selected ? count : SIZE_MAX, result, matches, depth + 1); ++index;
                if (Peek() != ',') break; ++position;
            }
            Require(']');
        } else if (Peek() == '"') String();
        else while (position < text.size() && text[position] != ',' && text[position] != '}' && text[position] != ']') ++position;
    }
public:
    explicit Reader(std::string_view source) : text(source) {}
    bool Extract(const char* const* path, size_t count, std::string& result) {
        unsigned matches = 0; position = 0; Value(path, 0, count, result, matches, 0); if (matches > 1) throw BadJson{}; return matches == 1 && position == text.size();
    }
};
bool Profile(const wchar_t* profile, std::wstring& target) {
    if (!profile || !*profile || wcslen(profile) > 128) return false;
    for (const wchar_t* p = profile; *p; ++p) if (!((*p >= L'a' && *p <= L'z') || (*p >= L'A' && *p <= L'Z') || (*p >= L'0' && *p <= L'9') || *p == L'-' || *p == L'_' || *p == L'.')) return false;
    target = L"Notepad3/AI/"; target += profile; return true;
}
bool Secret(std::string_view secret) {
    if (secret.empty() || secret.size() > 2048) return false;
    for (unsigned char c : secret) if (c < 33 || c > 126) return false;
    return true;
}
}
extern "C" int AiEndpoint_Validate(const wchar_t* endpoint) {
    if (!endpoint || !*endpoint || wcslen(endpoint) > 2048 || wcschr(endpoint, L'#')) return AI_CONFIG;
    for (const wchar_t* p = endpoint; *p; ++p) if (*p < 32 || *p == 127) return AI_CONFIG;
    URL_COMPONENTS url = {}; url.dwStructSize = sizeof(url);
    url.dwHostNameLength = url.dwUserNameLength = url.dwPasswordLength = url.dwUrlPathLength = url.dwExtraInfoLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(endpoint, 0, 0, &url) || !url.dwHostNameLength || url.dwUserNameLength || url.dwPasswordLength) return AI_CONFIG;
    if (url.nScheme == INTERNET_SCHEME_HTTPS) return AI_OK;
    if (url.nScheme != INTERNET_SCHEME_HTTP) return AI_CONFIG;
    std::wstring host(url.lpszHostName, url.dwHostNameLength);
    return !_wcsicmp(host.c_str(), L"localhost") || host == L"127.0.0.1" || host == L"::1" || host == L"[::1]" ? AI_OK : AI_CONFIG;
}
extern "C" void AiProtocol_Free(char* text) { std::free(text); }
extern "C" int AiProtocol_Request(const char* model, int action, const char* input, size_t length, const char* instruction, char** output, size_t* outputLength) {
    if (!output || !outputLength) return AI_CONFIG; *output = nullptr; *outputLength = 0;
    if (!model || !*model || action < AI_EXPLAIN || action > AI_GENERATE || (!input && length)) return AI_CONFIG;
    size_t modelLength = strlen(model), instructionLength = instruction ? strlen(instruction) : 0;
    if (length > 256 * 1024 || modelLength > 256 || instructionLength > 4096) return AI_LIMIT;
    if (!Utf8({model, modelLength}) || !Utf8({input ? input : "", length}) || !Utf8({instruction ? instruction : "", instructionLength})) return AI_TEXT;
    try {
        const char* prompts[] = {"Explain the provided text or code in Chinese.", "Translate the provided text into Chinese. Preserve formatting and return only the translation.", "Improve the provided text or code while preserving its meaning and language. Return only the revised content.", "Fulfill the user's instruction. Return only the generated text or code."};
        std::string prompt = prompts[action]; if (instructionLength) { prompt += "\n"; prompt += instruction; }
        std::string body = "{\"model\":" + Quote(model) + ",\"stream\":false,\"messages\":[{\"role\":\"system\",\"content\":" + Quote(prompt) + "},{\"role\":\"user\",\"content\":" + Quote({input ? input : "", length}) + "}]}";
        return Copy(body, output, outputLength);
    } catch (...) { return AI_MEMORY; }
}
extern "C" int AiProtocol_Response(const char* json, size_t length, char** output, size_t* outputLength) {
    if (!output || !outputLength) return AI_CONFIG; *output = nullptr; *outputLength = 0;
    if (!json && length) return AI_RESPONSE;
    if (length > 2 * 1024 * 1024) return AI_LIMIT;
    if (!Utf8({json ? json : "", length})) return AI_TEXT;
    try {
        char* formatted = nullptr; size_t size = 0, error = 0;
        int status = JsonFormat(json ? json : "", length, 1, "", &formatted, &size, &error);
        std::unique_ptr<char, decltype(&JsonFormat_Free)> owner(formatted, JsonFormat_Free);
        if (status) return status == 3 ? AI_MEMORY : AI_RESPONSE;
        Reader reader({formatted, size}); std::string content, reason;
        const char* contentPath[] = {"choices", "0", "message", "content"};
        const char* reasonPath[] = {"choices", "0", "finish_reason"};
        if (!reader.Extract(contentPath, 4, content) || content.empty() || !Utf8(content)) return AI_RESPONSE;
        if (reader.Extract(reasonPath, 3, reason) && reason != "stop") return AI_INCOMPLETE;
        return Copy(content, output, outputLength);
    } catch (const BadJson&) { return AI_RESPONSE; } catch (...) { return AI_MEMORY; }
}
extern "C" DWORD AiCredential_ServiceProfile(const wchar_t* endpoint, wchar_t* profile, size_t capacity) {
    if (!profile || capacity < 73 || AiEndpoint_Validate(endpoint) != AI_OK) return ERROR_INVALID_PARAMETER;
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0) return ERROR_INVALID_FUNCTION;
    unsigned char hash[32];
    NTSTATUS status = BCryptHash(algorithm, nullptr, 0, reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(endpoint)), static_cast<ULONG>(wcslen(endpoint) * sizeof(wchar_t)), hash, sizeof(hash));
    BCryptCloseAlgorithmProvider(algorithm, 0);
    if (status < 0) return ERROR_INVALID_FUNCTION;
    wcscpy_s(profile, capacity, L"service-");
    const wchar_t* hex = L"0123456789abcdef";
    for (size_t i = 0; i < 32; ++i) { profile[8 + 2 * i] = hex[hash[i] >> 4]; profile[9 + 2 * i] = hex[hash[i] & 15]; }
    profile[72] = 0; return ERROR_SUCCESS;
}
extern "C" void AiCredential_Free(char* secret) { if (secret) { SecureZeroMemory(secret, strlen(secret)); std::free(secret); } }
extern "C" DWORD AiCredential_Save(const wchar_t* profile, const char* secret) {
    try {
        std::wstring target; if (!Profile(profile, target) || !secret || !Secret(secret)) return ERROR_INVALID_PARAMETER;
        CREDENTIALW credential = {}; credential.Type = CRED_TYPE_GENERIC; credential.TargetName = const_cast<wchar_t*>(target.c_str());
        credential.CredentialBlobSize = static_cast<DWORD>(strlen(secret)); credential.CredentialBlob = reinterpret_cast<LPBYTE>(const_cast<char*>(secret)); credential.Persist = CRED_PERSIST_LOCAL_MACHINE;
        return CredWriteW(&credential, 0) ? ERROR_SUCCESS : GetLastError();
    } catch (...) { return ERROR_NOT_ENOUGH_MEMORY; }
}
extern "C" DWORD AiCredential_Load(const wchar_t* profile, char** secret) {
    if (!secret) return ERROR_INVALID_PARAMETER; *secret = nullptr;
    try {
        std::wstring target; if (!Profile(profile, target)) return ERROR_INVALID_PARAMETER;
        PCREDENTIALW credential = nullptr; if (!CredReadW(target.c_str(), CRED_TYPE_GENERIC, 0, &credential)) return GetLastError();
        DWORD result = ERROR_INVALID_DATA;
        if (Secret({reinterpret_cast<const char*>(credential->CredentialBlob), credential->CredentialBlobSize})) {
            *secret = static_cast<char*>(std::malloc(credential->CredentialBlobSize + 1));
            if (*secret) { std::memcpy(*secret, credential->CredentialBlob, credential->CredentialBlobSize); (*secret)[credential->CredentialBlobSize] = 0; result = ERROR_SUCCESS; }
            else result = ERROR_NOT_ENOUGH_MEMORY;
        }
        SecureZeroMemory(credential->CredentialBlob, credential->CredentialBlobSize); CredFree(credential); return result;
    } catch (...) { return ERROR_NOT_ENOUGH_MEMORY; }
}
extern "C" DWORD AiCredential_Delete(const wchar_t* profile) {
    try { std::wstring target; if (!Profile(profile, target)) return ERROR_INVALID_PARAMETER; return CredDeleteW(target.c_str(), CRED_TYPE_GENERIC, 0) ? ERROR_SUCCESS : GetLastError(); }
    catch (...) { return ERROR_NOT_ENOUGH_MEMORY; }
}
