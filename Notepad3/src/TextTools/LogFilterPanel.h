#pragma once
#include <windows.h>
#include "LogFilter.h"
#define NP3_WM_LOGFILTER_CAPTURE (WM_APP + 0x358)
#define NP3_WM_LOGFILTER_LOCATE (WM_APP + 0x359)
enum LOG_PANEL_LABEL { LP_REGEX, LP_CASE, LP_INVERT, LP_RUN, LP_CANCEL, LP_LINE, LP_TEXT, LP_RUNNING, LP_DONE, LP_PATTERN_ERROR, LP_LIMIT, LP_CANCELED, LP_ERROR, LP_STALE, LP_COUNT };
typedef struct LOG_PANEL_LOCATION { unsigned long long id, version; LOG_FILTER_ROW row; } LOG_PANEL_LOCATION;
#ifdef __cplusplus
extern "C" {
#endif
void LogPanel_Toggle(HWND parent, const wchar_t* const* labels);
BOOL LogPanel_IsOpen(HWND parent);
BOOL LogPanel_IsBusy(HWND parent);
int LogPanel_Layout(HWND parent, int x, int y, int width, int height);
BOOL LogPanel_Run(HWND parent, const char* text, size_t length, unsigned long long id, unsigned long long version);
#ifdef __cplusplus
}
#endif
