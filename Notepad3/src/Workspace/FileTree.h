#pragma once
#include <windows.h>
#define NP3_WM_FILETREE_OPEN (WM_APP + 0x356)
#define NP3_WM_FILETREE_ERROR (WM_APP + 0x357)
typedef struct FILETREE_LABELS { const wchar_t* choose; const wchar_t* refresh; const wchar_t* loading; const wchar_t* limit; } FILETREE_LABELS;
#ifdef __cplusplus
extern "C" {
#endif
void FileTree_Toggle(HWND parent, const wchar_t* directory, const FILETREE_LABELS* labels);
BOOL FileTree_IsOpen(HWND parent);
int FileTree_Layout(HWND parent, int x, int y, int width, int height);
BOOL FileTree_SetRoot(HWND parent, const wchar_t* directory);
BOOL FileTree_IsLoading(HWND parent);
#ifdef __cplusplus
}
#endif
