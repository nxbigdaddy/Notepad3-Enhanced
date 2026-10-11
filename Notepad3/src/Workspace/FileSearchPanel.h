#pragma once
#include <windows.h>
#ifdef __cplusplus
extern "C" {
#endif
HWND FileSearch_Show(HWND parent,const wchar_t* directory);
void FileSearch_Refresh(HWND parent);
#ifdef __cplusplus
}
#endif
