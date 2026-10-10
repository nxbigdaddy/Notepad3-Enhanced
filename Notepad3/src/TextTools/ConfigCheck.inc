#include "ConfigValidate.h"
static int _ConfigCheck(int kind, bool showMessage)
{
    DocPos const length = SciCall_GetLength();
    CONFIG_DIAGNOSTIC diagnostic = { 0 };
    int result = length > 8 * 1024 * 1024 ? 2 : ConfigValidate(SciCall_GetRangePointer(0, length), (size_t)length, kind, &diagnostic);
    if (result == 1) {
        DocPos const position = (DocPos)diagnostic.offset;
        SciCall_SetSel(position, position < length ? SciCall_PositionAfter(position) : position);
        SciCall_ScrollCaret();
        if (showMessage) {
            WCHAR reason[MIDSZ_BUFFER];
            UINT resource = IDS_MUI_CONFIG_JSON;
            if (diagnostic.reason == CONFIG_INVALID_TEXT) resource = IDS_MUI_CONFIG_TEXT;
            else if (diagnostic.reason == CONFIG_INI_SECTION) resource = IDS_MUI_CONFIG_SECTION;
            else if (diagnostic.reason == CONFIG_INI_KEY) resource = IDS_MUI_CONFIG_KEY;
            GetLngString(resource, reason, COUNTOF(reason));
            InfoBoxLng(MB_ICONWARNING, NULL, IDS_MUI_CONFIG_SYNTAX, reason, (int)SciCall_LineFromPosition(position) + 1, (int)SciCall_GetColumn(position) + 1);
        }
    } else if (showMessage) {
        if (!result) InfoBoxLng(MB_ICONINFORMATION, NULL, IDS_MUI_CONFIG_VALID);
        else InfoBoxLng(MB_ICONWARNING, NULL, IDS_MUI_CONFIG_LIMIT);
    }
    return result;
}
