"""One-time, anchor-checked integration of the Markdown module into the native host."""
from pathlib import Path
import re
root = Path(__file__).resolve().parent.parent / 'Notepad3'
def change(relative, old, new):
    path = root / relative
    text = path.read_bytes().decode('utf-8').replace('\r\n', '\n')
    if new in text:
        return
    if text.count(old) != 1:
        raise RuntimeError(f'Expected one anchor in {relative}: {old[:60]}')
    path.write_bytes(text.replace(old, new).replace('\n', '\r\n').encode('utf-8'))

change('src/Notepad3.c', '#include "Notepad3Util.h"', '#include "Notepad3Util.h"\n#include "Markdown/MarkdownPreview.h"')
helper = '''static void _UpdateMarkdownPreview(HWND hwnd)
{
    if (!MarkdownPreview_IsOpen(hwnd)) { return; }
    DocPos const length = SciCall_GetLength();
    if (length > 2 * 1024 * 1024) {
        MarkdownPreview_Update(hwnd, "", (size_t)length, NULL);
        return;
    }
    char* const text = AllocMem((size_t)length + 1, HEAP_ZERO_MEMORY);
    if (!text) { return; }
    SciCall_GetText(length + 1, text);
    HPATHL directory = Path_Copy(Paths.CurrentFile);
    if (directory) { Path_RemoveFileSpec(directory); }
    MarkdownPreview_Update(hwnd, text, (size_t)length, directory ? Path_Get(directory) : NULL);
    Path_Release(directory);
    FreeMem(text);
}

'''
change('src/Notepad3.c', 'LRESULT CALLBACK MainWndProc(HWND hwnd, UINT umsg, WPARAM wParam, LPARAM lParam)', helper + 'LRESULT CALLBACK MainWndProc(HWND hwnd, UINT umsg, WPARAM wParam, LPARAM lParam)')
change('src/Notepad3.c', '    switch(umsg) {\n    case WM_CREATE:', '    switch(umsg) {\n    case NP3_WM_MARKDOWN_REFRESH:\n        _UpdateMarkdownPreview(hwnd);\n        return 0;\n\n    case WM_CREATE:')
change('src/Notepad3.c', '    HDWP const hdwp = BeginDeferWindowPos(2);', '    cx -= MarkdownPreview_Layout(hwnd, x, y, max_i(0, cx), max_i(0, cy));\n\n    HDWP const hdwp = BeginDeferWindowPos(2);')
change('src/Notepad3.c', '    CheckCmd(hmenu, IDM_VIEW_WORDWRAP, Globals.fvCurFile.bWordWrap);', '    CheckCmd(hmenu, IDM_VIEW_WORDWRAP, Globals.fvCurFile.bWordWrap);\n    CheckCmd(hmenu, IDM_VIEW_MARKDOWN_PREVIEW, MarkdownPreview_IsOpen(hwnd));')
change('src/Notepad3.c', '    case IDM_VIEW_WORDWRAP:\n', '    case IDM_VIEW_MARKDOWN_PREVIEW:\n        MarkdownPreview_Toggle(hwnd);\n        return true;\n\n    case IDM_VIEW_WORDWRAP:\n')
change('src/Notepad3.c', '        if (*bModified) {\n            LONG64 const timeout', '        if (*bModified) {\n            MarkdownPreview_Changed(Globals.hwndMain);\n            LONG64 const timeout')
change('language/common_res.h', '#define IDM_VIEW_NONPRINTINGCHARS       41065', '#define IDM_VIEW_NONPRINTINGCHARS       41065\n#define IDM_VIEW_MARKDOWN_PREVIEW       41066')
for path in (root / 'language').glob('np3_*/menu_*.rc'):
    text = path.read_bytes().decode('utf-8').replace('\r\n', '\n')
    lines = text.splitlines(keepends=True)
    if 'IDM_VIEW_MARKDOWN_PREVIEW' in text:
        continue
    indexes = [i for i, line in enumerate(lines) if 'MENUITEM' in line and re.search(r'\bIDM_VIEW_WORDWRAP\b', line)]
    if len(indexes) != 1: raise RuntimeError(f'Unexpected menu anchor: {path}')
    label = 'Markdown 预览' if 'zh_cn' in str(path) else 'Markdown Preview'
    lines.insert(indexes[0] + 1, f'        MENUITEM "{label}\\tCtrl+Shift+F12", IDM_VIEW_MARKDOWN_PREVIEW\n')
    path.write_bytes(''.join(lines).replace('\n', '\r\n').encode('utf-8'))
change('src/Notepad3.rc', '    VK_F12,         IDM_VIEW_SCHEME,', '    VK_F12,         IDM_VIEW_MARKDOWN_PREVIEW,         VIRTKEY, CONTROL, SHIFT, NOINVERT\n    VK_F12,         IDM_VIEW_SCHEME,')
items = '''  <ItemGroup>
    <ClCompile Include="Markdown\\MarkdownRender.cpp" />
    <ClCompile Include="Markdown\\MarkdownPreview.cpp" />
    <ClCompile Include="Markdown\\vendor\\md4c\\md4c.c"><WarningLevel>Level3</WarningLevel><TreatWarningAsError>false</TreatWarningAsError><PreprocessorDefinitions>_CRT_SECURE_NO_WARNINGS;%(PreprocessorDefinitions)</PreprocessorDefinitions></ClCompile>
    <ClCompile Include="Markdown\\vendor\\md4c\\md4c-html.c"><WarningLevel>Level3</WarningLevel><TreatWarningAsError>false</TreatWarningAsError><PreprocessorDefinitions>_CRT_SECURE_NO_WARNINGS;%(PreprocessorDefinitions)</PreprocessorDefinitions></ClCompile>
    <ClCompile Include="Markdown\\vendor\\md4c\\entity.c"><WarningLevel>Level3</WarningLevel><TreatWarningAsError>false</TreatWarningAsError></ClCompile>
    <ClInclude Include="Markdown\\MarkdownRender.h" />
    <ClInclude Include="Markdown\\MarkdownPreview.h" />
  </ItemGroup>
  <Target Name="CopyMarkdownRuntime" AfterTargets="Build">
    <PropertyGroup><MarkdownRuntimeArch Condition="'$(Platform)'=='Win32'">x86</MarkdownRuntimeArch><MarkdownRuntimeArch Condition="'$(Platform)'!='Win32'">$(Platform)</MarkdownRuntimeArch></PropertyGroup>
    <Copy SourceFiles="$(ProjectDir)Markdown\\vendor\\webview2\\$(MarkdownRuntimeArch)\\WebView2Loader.dll" DestinationFolder="$(OutDir)" />
  </Target>
'''
change('src/Notepad3.vcxproj', '  <Import Project="$(VCTargetsPath)\\Microsoft.Cpp.targets" />', items + '  <Import Project="$(VCTargetsPath)\\Microsoft.Cpp.targets" />')
print('Integrated Markdown preview in native host and all language menus')
