#pragma once
#include <windows.h>
#define NP3_WM_SEARCH_LOCATE (WM_APP + 0x365)
typedef struct SEARCH_LOCATION { const wchar_t* path; const wchar_t* context; size_t line,column,length; } SEARCH_LOCATION;
#ifdef __cplusplus
extern "C" {
#endif
HWND FileSearch_Show(HWND parent,const wchar_t* directory);
void FileSearch_Refresh(HWND parent);
#ifdef __cplusplus
}
#endif
