#pragma once
#include "AiHttp.h"
#define NP3_WM_AI_CAPTURE (WM_APP + 0x35a)
#define NP3_WM_AI_APPLY (WM_APP + 0x35b)
#define NP3_WM_AI_SAVE (WM_APP + 0x35c)
#define NP3_WM_AI_PROFILE (WM_APP + 0x36a)
#define AI_PROFILE_MAXIMUM 32
#define NP3_WM_AI_REMOVE_PROFILE (WM_APP + 0x36b)
#define NP3_WM_AI_PROMPT (WM_APP + 0x36c)
#define NP3_WM_AI_PROMPT_SAVE (WM_APP + 0x36d)
#define NP3_WM_AI_PROMPT_DELETE (WM_APP + 0x36e)
typedef struct AI_PROMPT { int index; wchar_t name[65], text[4097]; } AI_PROMPT;
typedef struct AI_PANEL_SETTINGS { wchar_t endpoint[2049], model[257]; DWORD timeoutMs; wchar_t configurationId[65]; } AI_PANEL_SETTINGS;
typedef struct AI_PANEL_SAVE { AI_PANEL_SETTINGS settings; const char* secret; } AI_PANEL_SAVE;
typedef struct AI_PANEL_CAPTURE { int action; const char* instruction; } AI_PANEL_CAPTURE;
typedef struct AI_PANEL_SOURCE { unsigned long long id, version; long long start, end, anchor, caret; } AI_PANEL_SOURCE;
typedef struct AI_PANEL_APPLY { AI_PANEL_SOURCE source; const char* original; size_t originalLength; const char* result; size_t resultLength; } AI_PANEL_APPLY;
enum AI_PANEL_LABEL { AP_ENDPOINT, AP_MODEL, AP_KEY, AP_TIMEOUT, AP_SAVE, AP_DELETE, AP_EXPLAIN, AP_TRANSLATE, AP_REWRITE, AP_GENERATE, AP_CANCEL, AP_APPLY, AP_ORIGINAL, AP_RESULT, AP_INSTRUCTION, AP_READY, AP_RUNNING, AP_DONE, AP_CANCELED, AP_ERROR, AP_STALE, AP_SAVED, AP_GROUPED, AP_COUNT };
#ifdef __cplusplus
extern "C" {
#endif
void* AiPanel_Create(void);
void AiPanel_Destroy(void* context);
void AiPanel_Toggle(void* context, HWND parent, const AI_PANEL_SETTINGS* settings, const wchar_t* const* labels);
BOOL AiPanel_IsOpen(void* context);
BOOL AiPanel_IsBusy(void* context);
int AiPanel_Layout(void* context, int x, int y, int width, int height);
BOOL AiPanel_Run(void* context, const AI_PANEL_SETTINGS* settings, const AI_PANEL_SOURCE* source, int action, const char* original, size_t length, const char* instruction);
#ifdef __cplusplus
}
#endif
