#include "MarkdownExport.h"
#include "MarkdownPdf.h"
static bool _MarkdownExportSnapshot(const HPATHL destination, uint32_t* error, bool pdf)
{
    _WorkspaceCapture();
    HPATHL normalized = Path_Copy(destination);
    if (!normalized) { *error = ERROR_NOT_ENOUGH_MEMORY; return false; }
    Path_NormalizeEx(normalized, Paths.WorkingDirectory, true, false);
    if (_WorkspaceOwnsPath(normalized)) { Path_Release(normalized); *error = ERROR_SHARING_VIOLATION; return false; }
    DocPos const length = SciCall_GetLength();
    if (length < 0 || length > 8 * 1024 * 1024) { Path_Release(normalized); *error = ERROR_FILE_TOO_LARGE; return false; }
    char* text = AllocMem((size_t)length + 1, HEAP_ZERO_MEMORY);
    if (!text) { Path_Release(normalized); *error = ERROR_NOT_ENOUGH_MEMORY; return false; }
    SciCall_GetText(length + 1, text);
    char* utf8 = text; size_t utf8Length = (size_t)length;
    int const codePage = SciCall_GetCodePage();
    bool success = true;
    if (length && codePage != SC_CP_UTF8) {
        int const wideLength = MultiByteToWideChar(codePage, MB_ERR_INVALID_CHARS, text, (int)length, NULL, 0);
        WCHAR* wide = wideLength ? AllocMem((size_t)wideLength * sizeof(WCHAR), HEAP_ZERO_MEMORY) : NULL;
        if (!wide) success = false;
        if (success) {
            MultiByteToWideChar(codePage, MB_ERR_INVALID_CHARS, text, (int)length, wide, wideLength);
            int const converted = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide, wideLength, NULL, 0, NULL, NULL);
            utf8 = converted ? AllocMem((size_t)converted + 1, HEAP_ZERO_MEMORY) : NULL;
            success = utf8 != NULL;
            if (success) { WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide, wideLength, utf8, converted, NULL, NULL); utf8Length = converted; }
        }
        FreeMem(wide);
        if (!success) *error = ERROR_NO_UNICODE_TRANSLATION;
    }
    HPATHL directory = Path_IsNotEmpty(Paths.CurrentFile) ? Path_Copy(Paths.CurrentFile) : Path_Copy(Paths.WorkingDirectory);
    if (directory && Path_IsNotEmpty(Paths.CurrentFile)) Path_RemoveFileSpec(directory);
    if (!directory) { success = false; *error = ERROR_NOT_ENOUGH_MEMORY; }
    if (success) success = pdf ? MarkdownPdf_Start(Globals.hwndMain, utf8, utf8Length, Path_Get(directory), Path_Get(normalized), error) != 0 : Markdown_ExportHtml(utf8, utf8Length, Path_Get(directory), Path_Get(normalized), error) != 0;
    if (utf8 != text) FreeMem(utf8);
    FreeMem(text); Path_Release(directory); Path_Release(normalized); return success;
}
static bool _MarkdownExportSave(const HPATHL destination, uint32_t* error) { return _MarkdownExportSnapshot(destination, error, false); }
static void _MarkdownExportCommand(bool pdf)
{
    HPATHL destination = Path_IsNotEmpty(Paths.CurrentFile) ? Path_Copy(Paths.CurrentFile) : Path_Copy(Paths.WorkingDirectory);
    if (!destination) return;
    if (Path_IsNotEmpty(Paths.CurrentFile)) Path_RenameExtension(destination, pdf ? L".pdf" : L".html");
    else Path_Append(destination, pdf ? L"preview.pdf" : L"preview.html");
    if (SaveFileDlg(Globals.hwndMain, destination, Paths.WorkingDirectory)) {
        uint32_t error = 0;
        if (!_MarkdownExportSnapshot(destination, &error, pdf)) InfoBoxLng(MB_ICONWARNING, NULL, IDS_MUI_MARKDOWN_EXPORT_ERROR, error);
    }
    Path_Release(destination);
}
