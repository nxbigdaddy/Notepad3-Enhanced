#pragma once
#include <windows.h>
#include <stddef.h>
#include <stdint.h>
#define NP3_WM_MARKDOWN_PDF_DONE (WM_APP + 0x354)
#define NP3_WM_MARKDOWN_PDF_PRINTING (WM_APP + 0x364)
#ifdef __cplusplus
extern "C" {
#endif
int MarkdownPdf_Start(HWND parent, const char* text, size_t length, const wchar_t* directory, const wchar_t* destination, uint32_t* error);
BOOL MarkdownPdf_IsBusy(HWND parent);
BOOL MarkdownPdf_Cancel(HWND parent);
#ifdef __cplusplus
}
#endif
