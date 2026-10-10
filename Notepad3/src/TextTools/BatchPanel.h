#pragma once
#include <windows.h>
#include <stddef.h>
#define NP3_WM_BATCH_CAPTURE (WM_APP + 0x35d)
#define NP3_WM_BATCH_APPLY (WM_APP + 0x35e)
typedef struct BATCH_INPUT {
    unsigned long long id, version;
    const wchar_t* label;
    const char* text;
    size_t length;
    BOOL readOnly;
    const char* eol;
} BATCH_INPUT;
typedef struct BATCH_CHANGE {
    unsigned long long id, version;
    const char* original; size_t originalLength;
    const char* result; size_t resultLength;
} BATCH_CHANGE;
typedef struct BATCH_APPLY { const BATCH_CHANGE* changes; size_t count; } BATCH_APPLY;
enum BATCH_LABEL { BP_REPLACE, BP_SORT, BP_UNIQUE, BP_TRIM, BP_EMPTY, BP_JSON, BP_COMPACT, BP_FIND, BP_WITH, BP_PREVIEW, BP_CANCEL, BP_APPLY, BP_FILE, BP_STATUS, BP_ORIGINAL, BP_RESULT, BP_READY, BP_RUNNING, BP_DONE, BP_UNCHANGED, BP_READONLY, BP_INVALID, BP_LIMIT, BP_JSONERROR, BP_CANCELED, BP_STALE, BP_APPLIED, BP_ERROR, BP_COUNT };
#ifdef __cplusplus
extern "C" {
#endif
void* BatchPanel_Create(void);
void BatchPanel_Destroy(void* context);
void BatchPanel_Toggle(void* context, HWND parent, const wchar_t* const* labels);
BOOL BatchPanel_IsOpen(void* context);
BOOL BatchPanel_IsBusy(void* context);
int BatchPanel_Layout(void* context, int x, int y, int width, int height);
BOOL BatchPanel_Run(void* context, const BATCH_INPUT* inputs, size_t count);
#ifdef __cplusplus
}
#endif
