#pragma once
#include <windows.h>
#ifdef __cplusplus
extern "C" {
#endif
// Copies metadata into a bounded queue; no document contents or UI handles retained.
BOOL HistoryService_Record(const wchar_t* store, const wchar_t* path, size_t maximum);
BOOL HistoryService_Flush(DWORD timeout);
DWORD HistoryService_Error(void);
#ifdef __cplusplus
}
#endif
