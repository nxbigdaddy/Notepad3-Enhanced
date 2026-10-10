#pragma once
#include "AiCore.h"
#ifdef __cplusplus
extern "C" {
#endif
enum AI_HTTP_STATUS { AI_HTTP_OK, AI_HTTP_CONFIG, AI_HTTP_CANCELED, AI_HTTP_TIMEOUT, AI_HTTP_NETWORK, AI_HTTP_SERVER, AI_HTTP_LIMIT, AI_HTTP_RESPONSE, AI_HTTP_MEMORY, AI_HTTP_BUSY };
typedef int (*AI_HTTP_CANCEL)(void* context);
typedef struct AI_HTTP_CONFIG_DATA { const wchar_t* endpoint; const char* model; const char* secret; DWORD timeoutMs; AI_HTTP_CANCEL cancel; void* context; void* client; } AI_HTTP_CONFIG_DATA;
typedef struct AI_HTTP_RESULT { char* text; size_t length; DWORD httpStatus, winError; int protocolStatus; } AI_HTTP_RESULT;
// One client per configured service origin. Keep it alive until requests return.
void* AiHttp_ClientCreate(const wchar_t* endpoint);
void AiHttp_ClientFree(void* client);
// Call from a background worker. WinHTTP operations are asynchronous; cancellation is polled while waiting.
int AiHttp_Run(const AI_HTTP_CONFIG_DATA* config, int action, const char* input, size_t length, const char* instruction, AI_HTTP_RESULT* result);
void AiHttp_Free(AI_HTTP_RESULT* result);
#ifdef __cplusplus
}
#endif
