#pragma once
#include <windows.h>
#define NP3_WM_HISTORY_SHOW (WM_APP + 0x367)
#define NP3_WM_HISTORY_STATE (WM_APP + 0x368)
#define NP3_WM_HISTORY_OPEN (WM_APP + 0x369)
typedef struct HISTORY_LOCATION { const wchar_t* path; unsigned long long line,column; } HISTORY_LOCATION;
typedef struct HISTORY_STATE { BOOL enabled,recentEnabled; int maximum; } HISTORY_STATE;
#ifdef __cplusplus
extern "C" {
#endif
HWND HistoryPanel_Show(HWND parent,const wchar_t* store);
void HistoryPanel_Refresh(HWND parent);
#ifdef __cplusplus
}
#endif
