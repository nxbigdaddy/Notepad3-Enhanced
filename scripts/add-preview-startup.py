from pathlib import Path
root = Path(__file__).resolve().parent.parent / 'Notepad3'
def replace(path, old, new):
    file = root / path
    data = file.read_bytes()
    old = old.replace('\n', '\r\n').encode('utf-8')
    new = new.replace('\n', '\r\n').encode('utf-8')
    assert data.count(old) == 1, path
    file.write_bytes(data.replace(old, new))
replace('src/TypeDefs.h', '    bool bSettingsFileSoftLocked;', '    bool bSettingsFileSoftLocked;\n    bool bStartMarkdownPreview;')
replace('src/Notepad3.c', '    if (StringCchCompareXI(lp1, L"ANSI")', '''    if (StringCchCompareXI(lp1, L"MDPREVIEW") == 0) {
        Flags.bStartMarkdownPreview = true;
    }
    else if (StringCchCompareXI(lp1, L"ANSI")''')
replace('src/Notepad3.c', '    ResetMouseDWellTime();\n\n    // print file immediately and quit', '''    ResetMouseDWellTime();

    if (Flags.bStartMarkdownPreview) {
        PostMessage(hwndMain, WM_COMMAND, IDM_VIEW_MARKDOWN_PREVIEW, 0);
    }

    // print file immediately and quit''')
