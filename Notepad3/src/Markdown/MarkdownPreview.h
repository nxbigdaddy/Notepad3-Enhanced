#pragma once
#include <windows.h>
#include <stddef.h>
#define NP3_WM_MARKDOWN_REFRESH (WM_APP + 0x351)
#ifdef __cplusplus
extern "C" {
#endif
void MarkdownPreview_Toggle(HWND parent);
BOOL MarkdownPreview_IsOpen(HWND parent);
BOOL MarkdownPreview_IsRendered(HWND parent);
BOOL MarkdownPreview_Capture(HWND parent, const WCHAR* destination);
BOOL MarkdownPreview_IsCaptureDone(HWND parent);
BOOL MarkdownPreview_IsCaptureSuccessful(HWND parent);
// Return width reserved for preview; zero while hidden.
int MarkdownPreview_Layout(HWND parent, int x, int y, int width, int height);
void MarkdownPreview_Changed(HWND parent);
void MarkdownPreview_Update(HWND parent, const char* text, size_t length, const WCHAR* directory, const WCHAR* documentKey);
#ifdef __cplusplus
}
#endif
