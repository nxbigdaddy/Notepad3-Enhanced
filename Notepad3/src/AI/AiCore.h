#pragma once
#include <windows.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
enum AI_ACTION { AI_EXPLAIN, AI_TRANSLATE, AI_REWRITE, AI_GENERATE, AI_SUMMARIZE, AI_POLISH, AI_EXPAND, AI_SHORTEN, AI_CORRECT, AI_ASK };
enum AI_STATUS { AI_OK, AI_CONFIG, AI_LIMIT, AI_TEXT, AI_RESPONSE, AI_INCOMPLETE, AI_MEMORY };
int AiEndpoint_Validate(const wchar_t* endpoint);
int AiProtocol_Request(const char* model, int action, const char* input, size_t length, const char* instruction, char** output, size_t* outputLength);
int AiProtocol_Response(const char* json, size_t length, char** output, size_t* outputLength);
void AiProtocol_Free(char* text);
// Secrets are isolated by profile and never written to the application's INI.
DWORD AiCredential_AccountProfile(const wchar_t* endpoint, const wchar_t* configurationId, wchar_t* profile, size_t capacity);
DWORD AiCredential_ServiceProfile(const wchar_t* endpoint, wchar_t* profile, size_t capacity);
DWORD AiCredential_Save(const wchar_t* profile, const char* secret);
DWORD AiCredential_Load(const wchar_t* profile, char** secret);
DWORD AiCredential_Delete(const wchar_t* profile);
void AiCredential_Free(char* secret);
#ifdef __cplusplus
}
#endif
