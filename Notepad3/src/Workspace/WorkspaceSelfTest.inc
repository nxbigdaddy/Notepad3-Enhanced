#include <richedit.h>
// Development-only opt-in test driven by NP3_WORKSPACE_SELFTEST_DIR. The runner
// creates an isolated marker and fixtures; this code never picks user documents.
typedef struct WORKSPACE_TEST_RESULT { HANDLE log; int checks, failures; } WORKSPACE_TEST_RESULT;
static LRESULT CALLBACK _WorkspaceTestDpiMessage(HWND window,UINT message,WPARAM wParam,LPARAM lParam,UINT_PTR subclass,DWORD_PTR counter)
{
    UNREFERENCED_PARAMETER(subclass);
    if(message==WM_DPICHANGED) ++*(int*)counter;
    return DefSubclassProc(window,message,wParam,lParam);
}
static UINT_PTR workspaceCloseTestTimer;
static bool workspaceCloseTestWait;
static const WCHAR* workspaceCloseTestFilename=L"close-flow.txt";
static int workspaceCloseTestChoice, workspaceCloseTestClicks;
static BOOL CALLBACK _WorkspaceTestCloseDialog(HWND dialog, LPARAM unused)
{
    UNREFERENCED_PARAMETER(unused);
    if(workspaceCloseTestWait) return TRUE;
    WCHAR className[32], message[2048];
    GetClassNameW(dialog,className,COUNTOF(className));
    if(wcscmp(className,L"#32770") || GetAncestor(GetWindow(dialog,GW_OWNER),GA_ROOT)!=Globals.hwndMain) return TRUE;
    GetDlgItemTextW(dialog,IDC_INFOBOXTEXT,message,COUNTOF(message));
    // The upstream Yes/No/Cancel resource uses IDOK for its affirmative button.
    int affirmative=GetDlgItem(dialog,IDYES)?IDYES:IDOK;
    if(!wcsstr(message,workspaceCloseTestFilename) || !GetDlgItem(dialog,affirmative) || !GetDlgItem(dialog,IDNO) || !GetDlgItem(dialog,IDCANCEL)) return TRUE;
    HWND button=GetDlgItem(dialog,workspaceCloseTestChoice==IDYES?affirmative:workspaceCloseTestChoice);
    if(!IsWindowEnabled(button) || !IsWindowVisible(dialog)) return TRUE;
    KillTimer(NULL,workspaceCloseTestTimer); workspaceCloseTestTimer=0;
    ++workspaceCloseTestClicks;
    SetActiveWindow(dialog);
    SendMessageW(button,BM_CLICK,0,0);
    return FALSE;
}
static void CALLBACK _WorkspaceTestCloseTimer(HWND window,UINT message,UINT_PTR timer,DWORD time)
{
    UNREFERENCED_PARAMETER(window); UNREFERENCED_PARAMETER(message); UNREFERENCED_PARAMETER(timer); UNREFERENCED_PARAMETER(time);
    EnumThreadWindows(GetCurrentThreadId(),_WorkspaceTestCloseDialog,0);
}
static bool _WorkspaceTestCloseChoice(int choice)
{
    workspaceCloseTestChoice=choice; workspaceCloseTestClicks=0;
    workspaceCloseTestTimer=SetTimer(NULL,0,25,_WorkspaceTestCloseTimer);
    if(!workspaceCloseTestTimer) return false;
    SendWMCommand(Globals.hwndMain,IDM_FILE_CLOSE_TAB);
    if(workspaceCloseTestTimer) KillTimer(NULL,workspaceCloseTestTimer);
    workspaceCloseTestTimer=0;
    return workspaceCloseTestClicks==1;
}
static void _WorkspaceTestCheck(WORKSPACE_TEST_RESULT* result, bool condition, const char* label)
{
    ++result->checks;
    if (!condition) ++result->failures;
    char line[256]; DWORD written;
    StringCchPrintfA(line, COUNTOF(line), "%s %s\r\n", condition ? "PASS" : "FAIL", label);
    WriteFile(result->log, line, (DWORD)strlen(line), &written, NULL);
}
static bool _WorkspaceTestText(const char* expected)
{
    size_t const length = strlen(expected);
    if (SciCall_GetLength() != (DocPos)length) return false;
    if (!length) return true;
    const char* text = SciCall_GetRangePointer(0, (DocPos)length);
    return text && memcmp(text, expected, length) == 0;
}
static bool _WorkspaceTestPanelBrush(HWND panel, HWND child, COLORREF background, COLORREF foreground)
{
    HDC dc=CreateCompatibleDC(NULL); if (!dc) return false;
    HBRUSH brush=(HBRUSH)SendMessageW(panel,WM_CTLCOLORSTATIC,(WPARAM)dc,(LPARAM)child);
    LOGBRUSH info={0};
    bool matches=brush && GetObjectW(brush,sizeof(info),&info)==sizeof(info) && info.lbColor==background && GetTextColor(dc)==foreground;
    DeleteDC(dc); return matches;
}
static bool _WorkspaceTestToolbarGlyph(UINT listMessage,COLORREF expected)
{
    HIMAGELIST images=(HIMAGELIST)SendMessageW(Globals.hwndToolbar,listMessage,0,0);
    int index=(int)SendMessageW(Globals.hwndToolbar,TB_COMMANDTOINDEX,IDT_VIEW_MARKDOWN_PREVIEW,0);
    TBBUTTON button={0}; int width=0,height=0;
    if(!images || index<0 || !SendMessageW(Globals.hwndToolbar,TB_GETBUTTON,index,(LPARAM)&button) || !ImageList_GetIconSize(images,&width,&height) || width<=0 || height<=0) return false;
    HDC dc=CreateCompatibleDC(NULL); BITMAPINFO info={0}; void* pixels=NULL;
    info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER); info.bmiHeader.biWidth=width; info.bmiHeader.biHeight=-height; info.bmiHeader.biPlanes=1; info.bmiHeader.biBitCount=32; info.bmiHeader.biCompression=BI_RGB;
    HBITMAP bitmap=dc?CreateDIBSection(dc,&info,DIB_RGB_COLORS,&pixels,NULL,0):NULL;
    HGDIOBJ old=bitmap?SelectObject(dc,bitmap):NULL; bool found=false;
    if(bitmap) {
        RECT rect={0,0,width,height}; HBRUSH brush=CreateSolidBrush(RGB(255,0,255)); FillRect(dc,&rect,brush); DeleteObject(brush);
        if(ImageList_Draw(images,button.iBitmap,dc,0,0,ILD_TRANSPARENT)) for(int y=0;y<height && !found;++y) for(int x=0;x<width;++x) if(GetPixel(dc,x,y)==expected) { found=true; break; }
        SelectObject(dc,old); DeleteObject(bitmap);
    }
    if(dc) DeleteDC(dc); return found;
}
static bool _WorkspaceTestTreeReady()
{
    DWORD const start = GetTickCount();
    while (FileTree_IsLoading(Globals.hwndMain) && GetTickCount() - start < 5000) {
        MsgWaitForMultipleObjectsEx(0, NULL, 50, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        MSG message;
        while (PeekMessageW(&message, NULL, 0, 0, PM_REMOVE)) { TranslateMessage(&message); DispatchMessageW(&message); }
    }
    return !FileTree_IsLoading(Globals.hwndMain);
}
static bool _WorkspaceTestSearchReady(HWND window)
{
    DWORD start=GetTickCount(); WCHAR status[256];
    while(IsWindow(window) && GetTickCount()-start<5000){
        GetDlgItemTextW(window,107,status,COUNTOF(status));
        if(wcsstr(status,L"搜索完成") || wcsstr(status,L"已停止"))return true;
        MsgWaitForMultipleObjectsEx(0,NULL,25,QS_ALLINPUT,MWMO_INPUTAVAILABLE);
        MSG message; while(PeekMessageW(&message,NULL,0,0,PM_REMOVE)){TranslateMessage(&message);DispatchMessageW(&message);}
    }
    return false;
}
static bool _WorkspaceTestHistoryReady(HWND window)
{
    DWORD start=GetTickCount();WCHAR status[256];
    while(IsWindow(window) && GetTickCount()-start<6000){
        GetDlgItemTextW(window,107,status,COUNTOF(status));if(wcsstr(status,L"条历史记录"))return true;
        if(wcsstr(status,L"历史读写错误"))return false;
        MsgWaitForMultipleObjectsEx(0,NULL,25,QS_ALLINPUT,MWMO_INPUTAVAILABLE);
        MSG message;while(PeekMessageW(&message,NULL,0,0,PM_REMOVE)){TranslateMessage(&message);DispatchMessageW(&message);}
    }
    return false;
}
static void _WorkspaceTestSearchErrors(HWND search,const WCHAR* searchRoot,const WCHAR* folder,WORKSPACE_TEST_RESULT* result)
{
    WCHAR errorRoots[MAX_PATH_EXPLICIT*2];StringCchPrintfW(errorRoots,COUNTOF(errorRoots),L"%s\r\n%s\\missing-search-root",searchRoot,folder);
    SetDlgItemTextW(search,101,errorRoots);SetDlgItemTextW(search,102,L"*.txt");SetDlgItemTextW(search,103,L"");SetDlgItemTextW(search,110,L"");SendDlgItemMessageW(search,109,BM_SETCHECK,BST_UNCHECKED,0);SendMessageW(search,WM_COMMAND,104,0);
    _WorkspaceTestCheck(result,_WorkspaceTestSearchReady(search) && ListView_GetItemCount(GetDlgItem(search,106))==1,"search retains valid root results alongside missing root error");
    WCHAR errorDetails[4096];GetDlgItemTextW(search,121,errorDetails,COUNTOF(errorDetails));
    WCHAR missingRoot[MAX_PATH_EXPLICIT],errorCode[32];StringCchPrintfW(missingRoot,COUNTOF(missingRoot),L"%s\\missing-search-root",folder);GetFileAttributesW(missingRoot);StringCchPrintfW(errorCode,COUNTOF(errorCode),L"[%lu]",GetLastError());
    _WorkspaceTestCheck(result,wcsstr(errorDetails,L"missing-search-root") && wcsstr(errorDetails,errorCode),"search error details contain actual failing path and Windows error code");
    SendMessageW(search,WM_COMMAND,120,0);
    _WorkspaceTestCheck(result,IsWindowVisible(GetDlgItem(search,121)) && !IsWindowVisible(GetDlgItem(search,106)) && (GetWindowLongPtrW(GetDlgItem(search,121),GWL_STYLE)&ES_READONLY),"error details toggle exposes read-only report without destroying results");
    SendMessageW(search,WM_COMMAND,120,0);
    _WorkspaceTestCheck(result,IsWindowVisible(GetDlgItem(search,106)) && ListView_GetItemCount(GetDlgItem(search,106))==1,"return from error details retains valid search results");
    SetDlgItemTextW(search,101,searchRoot);SendMessageW(search,WM_COMMAND,104,0);bool cleanSearch=_WorkspaceTestSearchReady(search);GetDlgItemTextW(search,121,errorDetails,COUNTOF(errorDetails));
    _WorkspaceTestCheck(result,cleanSearch && wcsstr(errorDetails,L"没有搜索错误") && !wcsstr(errorDetails,L"missing-search-root"),"new successful search replaces previous error report");
}
static bool _WorkspaceTestAiReady(void)
{
    DWORD start = GetTickCount();
    while (AiPanel_IsBusy(_WorkspaceAi()) && GetTickCount() - start < 5000) {
        MsgWaitForMultipleObjectsEx(0, NULL, 50, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        MSG message; while (PeekMessageW(&message, NULL, 0, 0, PM_REMOVE)) { TranslateMessage(&message); DispatchMessageW(&message); }
    }
    return !AiPanel_IsBusy(_WorkspaceAi());
}
static void _WorkspaceTestAiCapacity(WORKSPACE_TEST_RESULT* result)
{
    AI_PANEL_SAVE restore={0},request={0};restore.settings=_AiSettings();restore.secret="";request=restore;
    WCHAR section[32],id[65],profile[73];char* key=NULL;
    bool prepared=OpenSettingsFile(__FUNCTION__);
    if(prepared) {
        for(int index=2;index<AI_PROFILE_MAXIMUM;++index) {
            StringCchPrintfW(section,COUNTOF(section),L"AIProfile%02d",index);StringCchPrintfW(id,COUNTOF(id),L"capacity-%lu-%d",GetCurrentProcessId(),index);
            prepared=IniSectionSetString(section,L"Id",id) && IniSectionSetString(section,L"Endpoint",restore.settings.endpoint) && IniSectionSetString(section,L"Model",restore.settings.model) && IniSectionSetInt(section,L"TimeoutMs",60000) && prepared;
        }
        prepared=CloseSettingsFile(__FUNCTION__,prepared) && prepared;
    }
    _WorkspaceTestCheck(result,prepared,"owned fixture fills all 32 saved profile slots");
    StringCchPrintfW(request.settings.configurationId,COUNTOF(request.settings.configurationId),L"capacity-new-%lu-%llu",GetCurrentProcessId(),GetTickCount64());request.secret="owned-capacity-fixture-key";
    bool identity=AiCredential_AccountProfile(request.settings.endpoint,request.settings.configurationId,profile,COUNTOF(profile))==ERROR_SUCCESS;
    if(prepared && identity) {
        _WorkspaceTestCheck(result,!_AiSave(&request,false),"33rd profile is rejected without overwriting saved slots");
        _WorkspaceTestCheck(result,!wcscmp(Settings2.AIConfigurationId,restore.settings.configurationId) && AiCredential_Load(profile,&key)==ERROR_NOT_FOUND && !key,"capacity rejection preserves active account and rolls back newly saved key");AiCredential_Free(key);key=NULL;
        _WorkspaceTestCheck(result,_AiSave(&restore,false),"existing profile can still update when all slots are full");
        AI_PANEL_SETTINGS last={0};bool removed=_AiReadProfile(AI_PROFILE_MAXIMUM-1,&last) && _AiRemoveProfile(AI_PROFILE_MAXIMUM-1,&last);
        _WorkspaceTestCheck(result,removed && _AiSave(&request,false),"removing a profile permits saving a new account in freed slot");
        bool loaded=AiCredential_Load(profile,&key)==ERROR_SUCCESS && !strcmp(key,"owned-capacity-fixture-key");AiCredential_Free(key);key=NULL;
        _WorkspaceTestCheck(result,loaded && _AiReadProfile(AI_PROFILE_MAXIMUM-1,&last) && !wcscmp(last.configurationId,request.settings.configurationId),"reused capacity slot and scoped key independently read back");
        AiCredential_Delete(profile);
    }
    bool cleaned=OpenSettingsFile(__FUNCTION__);
    if(cleaned) {
        for(int index=2;index<AI_PROFILE_MAXIMUM;++index){StringCchPrintfW(section,COUNTOF(section),L"AIProfile%02d",index);IniSectionDelete(section,NULL,true);}
        cleaned=CloseSettingsFile(__FUNCTION__,true);
    }
    _WorkspaceTestCheck(result,cleaned && _AiSave(&restore,false),"owned capacity fixture cleans temporary slots and restores active account");
}
static void _WorkspaceTestAiPresets(HWND panel,WORKSPACE_TEST_RESULT* result)
{
    AI_PANEL_SETTINGS saved=_AiSettings();WCHAR endpoint[2049];
    const WCHAR* expected[]={L"https://api.moonshot.cn/v1/chat/completions",L"https://api.deepseek.com/chat/completions",L"https://dashscope.aliyuncs.com/compatible-mode/v1/chat/completions",L"",L"https://api.openai.com/v1/chat/completions"};
    _WorkspaceTestCheck(result,SendDlgItemMessageW(panel,125,CB_GETCOUNT,0,0)==6,"provider address selector exposes three regional presets and custom endpoint");
    for(int index=1;index<=5;++index) {
        SetWindowTextW(GetDlgItem(panel,102),L"previous-model");SetWindowTextW(GetDlgItem(panel,103),L"owned-draft-key");SetWindowTextW(GetDlgItem(panel,119),L"previous-account");
        SendDlgItemMessageW(panel,125,CB_SETCURSEL,index,0);SendMessageW(panel,WM_COMMAND,MAKEWPARAM(125,CBN_SELCHANGE),0);
        GetWindowTextW(GetDlgItem(panel,101),endpoint,COUNTOF(endpoint));
        _WorkspaceTestCheck(result,!wcscmp(endpoint,expected[index-1]),"provider preset fills exact documented endpoint or clears custom endpoint");
        _WorkspaceTestCheck(result,!GetWindowTextLengthW(GetDlgItem(panel,102)) && !GetWindowTextLengthW(GetDlgItem(panel,103)) && !GetWindowTextLengthW(GetDlgItem(panel,119)),"provider change clears prior model account and unsaved key drafts");
        _WorkspaceTestCheck(result,SendDlgItemMessageW(panel,134,BM_GETCHECK,0,0)==(index==3?BST_CHECKED:BST_UNCHECKED),"only Qwen preset enables native paginated discovery");
        _WorkspaceTestCheck(result,!wcscmp(Settings2.AIEndpoint,saved.endpoint) && !wcscmp(Settings2.AIModel,saved.model) && !AiPanel_IsBusy(_WorkspaceAi()) && _WorkspaceTestText("alpha\r\nA"),"address preset does not activate persist send or edit document");
    }
    SetWindowTextW(GetDlgItem(panel,101),saved.endpoint);SetWindowTextW(GetDlgItem(panel,102),saved.model);SetWindowTextW(GetDlgItem(panel,119),saved.configurationId);SetDlgItemInt(panel,104,saved.timeoutMs,FALSE);SendDlgItemMessageW(panel,125,CB_SETCURSEL,0,0);
    SetWindowTextW(GetDlgItem(panel,102),L"");SetWindowTextW(GetDlgItem(panel,103),L"fake-model-list-key");
    SendMessageW(panel,WM_COMMAND,MAKEWPARAM(132,BN_CLICKED),0);
    _WorkspaceTestCheck(result,AiPanel_IsBusy(_WorkspaceAi()) && !IsWindowEnabled(GetDlgItem(panel,132)),"model discovery uses draft key without requiring model or save");
    _WorkspaceTestCheck(result,_WorkspaceTestAiReady() && SendDlgItemMessageW(panel,102,CB_GETCOUNT,0,0)==2,"actual GET populates editable model combo");
    SendDlgItemMessageW(panel,102,CB_SETCURSEL,1,0);WCHAR picked[257];GetDlgItemTextW(panel,102,picked,COUNTOF(picked));
    _WorkspaceTestCheck(result,!wcscmp(picked,L"fixture-model-two") && !IsWindowEnabled(GetDlgItem(panel,115)) && _WorkspaceTestText("alpha\r\nA"),"discovered model can be selected without applying or editing document");
    _WorkspaceTestCheck(result,!wcscmp(Settings2.AIEndpoint,saved.endpoint) && !wcscmp(Settings2.AIModel,saved.model),"model discovery does not persist draft settings");
    SendDlgItemMessageW(panel,134,BM_SETCHECK,BST_CHECKED,0);SetWindowTextW(GetDlgItem(panel,103),L"fake-model-list-key");
    SendMessageW(panel,WM_COMMAND,MAKEWPARAM(132,BN_CLICKED),0);
    _WorkspaceTestCheck(result,_WorkspaceTestAiReady() && SendDlgItemMessageW(panel,102,CB_GETCOUNT,0,0)==201,"native model selector collects all three pages before publishing");
    SendDlgItemMessageW(panel,102,CB_SETCURSEL,200,0);GetDlgItemTextW(panel,102,picked,COUNTOF(picked));
    _WorkspaceTestCheck(result,!wcscmp(picked,L"native-model-200") && !IsWindowEnabled(GetDlgItem(panel,115)) && _WorkspaceTestText("alpha\r\nA"),"last native page is selectable without document changes");
    SendDlgItemMessageW(panel,134,BM_SETCHECK,BST_UNCHECKED,0);
    SetWindowTextW(GetDlgItem(panel,102),saved.model);SetWindowTextW(GetDlgItem(panel,103),L"");

}
static void _WorkspaceTestAiDiagnostics(HWND panel,WORKSPACE_TEST_RESULT* result)
{
    const WCHAR* instructions[]={L"fixture-http-401",L"fixture-http-403",L"fixture-http-402",L"fixture-http-429",L"fixture-http-404",L"fixture-http-400",L"fixture-http-503",L"fixture-truncated"};
    const WCHAR* labels[]={L"认证失败",L"权限不足",L"付款或额度",L"限流或配额",L"地址或模型",L"请求参数",L"服务端错误",L"截断或未完成"};WCHAR status[512];
    for(int index=0;index<8;++index) {
        SciCall_SetSel(0,5);SetWindowTextW(GetDlgItem(panel,120),instructions[index]);SendMessageW(panel,WM_COMMAND,MAKEWPARAM(112,BN_CLICKED),0);
        bool ready=_WorkspaceTestAiReady();GetWindowTextW(GetDlgItem(panel,130),status,COUNTOF(status));
        _WorkspaceTestCheck(result,ready && wcsstr(status,labels[index]),"actual local service failure displays distinct actionable diagnostic");
        _WorkspaceTestCheck(result,!IsWindowEnabled(GetDlgItem(panel,115)) && !GetWindowTextLengthW(GetDlgItem(panel,122)) && _WorkspaceTestText("alpha\r\nA"),"failed or truncated answer cannot preview or overwrite source");
    }
    SetWindowTextW(GetDlgItem(panel,120),L"private question must not be sent");SciCall_SetSelection(2,1);SciCall_AddSelection(6,5);
    SendMessageW(panel,WM_COMMAND,MAKEWPARAM(126,BN_CLICKED),0);
    _WorkspaceTestCheck(result,_WorkspaceTestAiReady() && _WorkspaceTestText("alpha\r\nA"),"connection test reaches saved service without sending or editing document");
    _WorkspaceTestCheck(result,SciCall_GetSelections()==2,"connection test remains independent of multiple editor selections");
    SciCall_SetSel(0,5);
    GetWindowTextW(GetDlgItem(panel,130),status,COUNTOF(status));
    _WorkspaceTestCheck(result,wcsstr(status,L"连接测试成功") && !IsWindowEnabled(GetDlgItem(panel,115)) && !GetWindowTextLengthW(GetDlgItem(panel,121)),"connection test displays success separately and is view only with empty source");
    SendMessageW(panel,WM_COMMAND,MAKEWPARAM(115,BN_CLICKED),0);
    _WorkspaceTestCheck(result,_WorkspaceTestText("alpha\r\nA"),"synthetic apply cannot replace source with connection response");
    SetWindowTextW(GetDlgItem(panel,120),L"");
}
static void _WorkspaceTestAiPrompts(HWND panel,WORKSPACE_TEST_RESULT* result)
{
    _WorkspaceTestCheck(result,!IsWindowVisible(GetDlgItem(panel,101)) && IsWindowVisible(GetDlgItem(panel,107)),"AI daily layout hides model configuration behind settings button");
    SendMessageW(panel,WM_COMMAND,MAKEWPARAM(107,BN_CLICKED),0);
    _WorkspaceTestCheck(result,IsWindowVisible(GetDlgItem(panel,101)) && IsWindowVisible(GetDlgItem(panel,125)),"model settings button reveals endpoint and provider choices");
    SendMessageW(panel,WM_COMMAND,MAKEWPARAM(107,BN_CLICKED),0);
    _WorkspaceTestCheck(result,!IsWindowVisible(GetDlgItem(panel,101)) && IsWindowVisible(GetDlgItem(panel,120)),"model settings collapse preserves visible prompt editing");
    SetWindowTextW(GetDlgItem(panel,128),L"中文提示词测试");SetWindowTextW(GetDlgItem(panel,120),L"第一行\r\n保留 \"引号\" 与 \\ 路径 😀");
    SendMessageW(panel,WM_COMMAND,MAKEWPARAM(129,BN_CLICKED),0);
    AI_PROMPT prompt={0};prompt.index=0;
    _WorkspaceTestCheck(result,_AiPrompt(0,&prompt) && !wcscmp(prompt.name,L"中文提示词测试") && !wcscmp(prompt.text,L"第一行\r\n保留 \"引号\" 与 \\ 路径 😀"),"saved prompt independently roundtrips Unicode multiline quotes and backslashes");
    _WorkspaceTestCheck(result,SendDlgItemMessageW(panel,127,CB_GETCOUNT,0,0)==2,"saved prompt appears in native selector");
    SetWindowTextW(GetDlgItem(panel,120),L"fixture-prompt-used");SendMessageW(panel,WM_COMMAND,MAKEWPARAM(129,BN_CLICKED),0);
    _WorkspaceTestCheck(result,_AiPrompt(0,&prompt) && !wcscmp(prompt.text,L"fixture-prompt-used") && SendDlgItemMessageW(panel,127,CB_GETCOUNT,0,0)==2,"saving same prompt name updates instead of duplicating it");
    SetWindowTextW(GetDlgItem(panel,120),L"unsaved different text");SendMessageW(panel,WM_COMMAND,MAKEWPARAM(131,BN_CLICKED),0);
    _WorkspaceTestCheck(result,_AiPrompt(0,&prompt),"deleting a modified draft cannot remove stored prompt without original snapshot");
    SendDlgItemMessageW(panel,127,CB_SETCURSEL,1,0);SendMessageW(panel,WM_COMMAND,MAKEWPARAM(127,CBN_SELCHANGE),0);
    WCHAR loaded[128];GetWindowTextW(GetDlgItem(panel,120),loaded,COUNTOF(loaded));
    _WorkspaceTestCheck(result,!wcscmp(loaded,L"fixture-prompt-used"),"prompt selector loads saved content for editing and execution");
    SciCall_SetSel(0,5);SendMessageW(panel,WM_COMMAND,MAKEWPARAM(110,BN_CLICKED),0);
    _WorkspaceTestCheck(result,_WorkspaceTestAiReady() && _WorkspaceTestText("alpha\r\nA"),"saved prompt is used in actual local request without editing source");
    SendMessageW(panel,WM_COMMAND,MAKEWPARAM(131,BN_CLICKED),0);
    _WorkspaceTestCheck(result,!_AiPrompt(0,&prompt) && SendDlgItemMessageW(panel,127,CB_GETCOUNT,0,0)==1,"deleting saved prompt removes persisted record and selector entry");
    SetWindowTextW(GetDlgItem(panel,128),L"");SetWindowTextW(GetDlgItem(panel,120),L"must not save unnamed");SendMessageW(panel,WM_COMMAND,MAKEWPARAM(129,BN_CLICKED),0);
    _WorkspaceTestCheck(result,SendDlgItemMessageW(panel,127,CB_GETCOUNT,0,0)==1,"unnamed prompt cannot be saved");
    SetWindowTextW(GetDlgItem(panel,120),L"");
}
static void _WorkspaceTestAiAutomaticAccount(HWND panel,WORKSPACE_TEST_RESULT* result)
{
    AI_PANEL_SAVE restore={0};restore.settings=_AiSettings();restore.secret="";
    SendDlgItemMessageW(panel,125,CB_SETCURSEL,4,0);SendMessageW(panel,WM_COMMAND,MAKEWPARAM(125,CBN_SELCHANGE),0);
    SetWindowTextW(GetDlgItem(panel,101),restore.settings.endpoint);SetWindowTextW(GetDlgItem(panel,102),restore.settings.model);SetWindowTextW(GetDlgItem(panel,103),L"owned-automatic-account-key");
    SendMessageW(panel,WM_COMMAND,MAKEWPARAM(105,BN_CLICKED),0);
    AI_PANEL_SETTINGS saved=_AiSettings();WCHAR profile[73];char* key=NULL;
    bool generated=saved.configurationId[0] && wcsstr(saved.configurationId,L"account-")==saved.configurationId;
    _WorkspaceTestCheck(result,generated,"new provider account save generates internal ID without manual entry");
    bool identity=generated && AiCredential_AccountProfile(saved.endpoint,saved.configurationId,profile,COUNTOF(profile))==ERROR_SUCCESS;
    bool loaded=identity && AiCredential_Load(profile,&key)==ERROR_SUCCESS && !strcmp(key,"owned-automatic-account-key");AiCredential_Free(key);
    _WorkspaceTestCheck(result,loaded,"automatic account retains independent credential identity");
    bool removed=false;AI_PANEL_SETTINGS candidate={0};
    if(identity){for(int slot=0;slot<AI_PROFILE_MAXIMUM;++slot){if(_AiReadProfile(slot,&candidate) && !wcscmp(candidate.configurationId,saved.configurationId)){removed=_AiRemoveProfile(slot,&candidate);break;}}AiCredential_Delete(profile);}
    _WorkspaceTestCheck(result,removed && _AiSave(&restore,false),"automatic account fixture cleans only generated profile and restores previous service");
    SetWindowTextW(GetDlgItem(panel,101),restore.settings.endpoint);SetWindowTextW(GetDlgItem(panel,102),restore.settings.model);SetWindowTextW(GetDlgItem(panel,119),restore.settings.configurationId);SetWindowTextW(GetDlgItem(panel,103),L"");SendDlgItemMessageW(panel,125,CB_SETCURSEL,0,0);
}
static void _WorkspaceTestAiMarkdown(HWND panel,WORKSPACE_TEST_RESULT* result)
{
    _WorkspaceTestCheck(result,SendDlgItemMessageW(panel,116,CB_GETCOUNT,0,0)==11,"AI action menu exposes five Markdown transformations");
    for(int index=6;index<=10;++index){
        SciCall_SetSel(0,5);SetWindowTextW(GetDlgItem(panel,120),L"fixture-markdown");SendDlgItemMessageW(panel,116,CB_SETCURSEL,index,0);SendMessageW(panel,WM_COMMAND,MAKEWPARAM(117,BN_CLICKED),0);
        _WorkspaceTestCheck(result,_WorkspaceTestAiReady() && IsWindowEnabled(GetDlgItem(panel,115)) && _WorkspaceTestText("alpha\r\nA"),"Markdown action previews proposal without editing source");
    }
    SendMessageW(panel,WM_COMMAND,MAKEWPARAM(115,BN_CLICKED),0);
    _WorkspaceTestCheck(result,_WorkspaceTestText("模拟结果\r\nA"),"Markdown proposal requires explicit apply");SciCall_Undo();
    _WorkspaceTestCheck(result,_WorkspaceTestText("alpha\r\nA"),"Markdown apply remains one undo transaction");SetWindowTextW(GetDlgItem(panel,120),L"");SendDlgItemMessageW(panel,116,CB_SETCURSEL,0,0);
}
static void _WorkspaceTestAiTokenLimit(HWND panel,WORKSPACE_TEST_RESULT* result)
{
    SetDlgItemInt(panel,135,1024,FALSE);SendMessageW(panel,WM_COMMAND,MAKEWPARAM(105,BN_CLICKED),0);
    _WorkspaceTestCheck(result,Settings2.AIMaxTokens==1024 && IniFileGetLong(Paths.IniFile,L"Settings2",L"AIMaxTokens",-1)==1024,"output token cap saved with independent disk readback");
    SetDlgItemInt(panel,135,65537,FALSE);SendMessageW(panel,WM_COMMAND,MAKEWPARAM(105,BN_CLICKED),0);
    _WorkspaceTestCheck(result,Settings2.AIMaxTokens==1024,"oversized output cap cannot change saved configuration");SetDlgItemInt(panel,135,1024,FALSE);
    SendMessageW(panel,WM_COMMAND,MAKEWPARAM(126,BN_CLICKED),0);
    _WorkspaceTestCheck(result,_WorkspaceTestAiReady() && !IsWindowEnabled(GetDlgItem(panel,115)) && _WorkspaceTestText("alpha\r\nA"),"actual saved output cap reaches fixed connection request without editing source");
    SetDlgItemInt(panel,135,0,FALSE);SendMessageW(panel,WM_COMMAND,MAKEWPARAM(105,BN_CLICKED),0);
    _WorkspaceTestCheck(result,Settings2.AIMaxTokens==0 && IniFileGetLong(Paths.IniFile,L"Settings2",L"AIMaxTokens",-1)==0,"zero cap restores default and persists without secret");
}
static HWND _WorkspaceTestAiAccounts(HWND panel,const WCHAR* endpoint,WORKSPACE_TEST_RESULT* result)
{
    _WorkspaceTestAiPrompts(panel,result);
    _WorkspaceTestAiDiagnostics(panel,result);
    _WorkspaceTestAiPresets(panel,result);
    _WorkspaceTestAiMarkdown(panel,result);
    _WorkspaceTestAiTokenLimit(panel,result);
    WCHAR idA[65],idB[65],profileA[73],profileB[73],saved[65]; char* key=NULL;
    StringCchPrintfW(idA,COUNTOF(idA),L"fixture-%lu-%llu-a",GetCurrentProcessId(),GetTickCount64());
    StringCchPrintfW(idB,COUNTOF(idB),L"fixture-%lu-%llu-b",GetCurrentProcessId(),GetTickCount64());
    bool identities=AiCredential_AccountProfile(endpoint,idA,profileA,COUNTOF(profileA))==ERROR_SUCCESS && AiCredential_AccountProfile(endpoint,idB,profileB,COUNTOF(profileB))==ERROR_SUCCESS;
    _WorkspaceTestCheck(result,identities,"native account test owns two isolated credential identities");
    if(!identities) return panel;
    SetWindowTextW(GetDlgItem(panel,119),L"bad/id"); SetWindowTextW(GetDlgItem(panel,103),L"");
    SendMessageW(panel,WM_COMMAND,MAKEWPARAM(105,BN_CLICKED),0);
    _WorkspaceTestCheck(result,!Settings2.AIConfigurationId[0],"invalid configuration ID cannot replace legacy settings");
    for(int index=0;index<2;++index) {
        SetDlgItemInt(panel,104,index?32000:31000,FALSE);
        const WCHAR* id=index?idB:idA; const char* expected=index?"fake-panel-test-key-b":"fake-panel-test-key-a";
        SetWindowTextW(GetDlgItem(panel,119),id); SetWindowTextW(GetDlgItem(panel,103),index?L"fake-panel-test-key-b":L"fake-panel-test-key-a");
        SendMessageW(panel,WM_COMMAND,MAKEWPARAM(105,BN_CLICKED),0);
        IniFileGetString(Paths.IniFile,L"Settings2",L"AIConfigurationId",L"",saved,COUNTOF(saved));
        _WorkspaceTestCheck(result,!wcscmp(saved,id) && !wcscmp(Settings2.AIConfigurationId,id),"account ID persists without key in ordinary settings");
        bool loaded=AiCredential_Load(index?profileB:profileA,&key)==ERROR_SUCCESS && !strcmp(key,expected); AiCredential_Free(key);key=NULL;
        _WorkspaceTestCheck(result,loaded && !GetWindowTextLengthW(GetDlgItem(panel,103)),"account save independently reads scoped key and clears key edit");
        _AiToggle(); _AiToggle(); panel=FindWindowExW(Globals.hwndMain,NULL,L"NP3AiPanel",NULL);
        GetWindowTextW(GetDlgItem(panel,119),saved,COUNTOF(saved));
        _WorkspaceTestCheck(result,!wcscmp(saved,id),"reopened AI panel restores saved account identity");
        _WorkspaceTestCheck(result,SendDlgItemMessageW(panel,123,CB_GETCURSEL,0,0)>0,"daily account selector highlights restored active profile");
        SciCall_SetSel(0,5); SetWindowTextW(GetDlgItem(panel,120),L"");
        SendMessageW(panel,WM_COMMAND,MAKEWPARAM(110,BN_CLICKED),0);
        _WorkspaceTestCheck(result,_WorkspaceTestAiReady() && _WorkspaceTestText("alpha\r\nA"),"selected account reaches local service without editing source");
    }
    _WorkspaceTestCheck(result,SendDlgItemMessageW(panel,123,CB_GETCOUNT,0,0)==3,"saved account selector lists two persisted profiles plus placeholder");
    int itemA=-1,itemB=-1; AI_PANEL_SETTINGS selected={0};
    for(int item=1;item<3;++item) {
        int slot=(int)SendDlgItemMessageW(panel,123,CB_GETITEMDATA,item,0);
        if(_AiReadProfile(slot,&selected)) {
            if(!wcscmp(selected.configurationId,idA)) itemA=item;
            if(!wcscmp(selected.configurationId,idB)) itemB=item;
        }
    }
    _WorkspaceTestCheck(result,itemA>0 && itemB>0,"account selector item identities independently read from persisted profiles");
    SetWindowTextW(GetDlgItem(panel,103),L"unsaved-key-must-not-transfer");SetWindowTextW(GetDlgItem(panel,102),L"unsaved-model");
    SendDlgItemMessageW(panel,123,CB_SETCURSEL,itemA,0);SendMessageW(panel,WM_COMMAND,MAKEWPARAM(123,CBN_SELCHANGE),0);
    _WorkspaceTestCheck(result,!wcscmp(Settings2.AIConfigurationId,idA) && Settings2.AITimeoutMs==31000 && !wcscmp(Settings2.AIModel,L"panel-test-model"),"selector switches stored account model and timeout without resaving draft fields");
    _WorkspaceTestCheck(result,!GetWindowTextLengthW(GetDlgItem(panel,103)),"switching saved account clears unsaved key instead of transferring it");
    SciCall_SetSel(0,5);SendMessageW(panel,WM_COMMAND,MAKEWPARAM(110,BN_CLICKED),0);
    _WorkspaceTestCheck(result,_WorkspaceTestAiReady() && _WorkspaceTestText("alpha\r\nA"),"saved account quick switch uses original scoped key for actual local request");
    SendDlgItemMessageW(panel,123,CB_SETCURSEL,itemB,0);SendMessageW(panel,WM_COMMAND,MAKEWPARAM(123,CBN_SELCHANGE),0);
    _WorkspaceTestCheck(result,!wcscmp(Settings2.AIConfigurationId,idB) && Settings2.AITimeoutMs==32000,"saved second account switch restores its independent timeout");
    int slotB=(int)SendDlgItemMessageW(panel,123,CB_GETITEMDATA,itemB,0);
    selected=_AiSettings();AI_PANEL_SETTINGS stale=selected;stale.timeoutMs++;
    _WorkspaceTestCheck(result,!_AiRemoveProfile(slotB,&stale) && _AiReadProfile(slotB,&stale),"stale profile snapshot cannot remove changed configuration");
    SendMessageW(panel,WM_COMMAND,MAKEWPARAM(124,BN_CLICKED),0);
    _WorkspaceTestCheck(result,SendDlgItemMessageW(panel,123,CB_GETCOUNT,0,0)==2 && !_AiReadProfile(slotB,&stale),"native remove profile frees only selected saved slot");
    bool keyKept=AiCredential_Load(profileB,&key)==ERROR_SUCCESS && !strcmp(key,"fake-panel-test-key-b");AiCredential_Free(key);key=NULL;
    _WorkspaceTestCheck(result,keyKept && !wcscmp(Settings2.AIConfigurationId,idB),"removing profile keeps credential and current active settings");
    SendMessageW(panel,WM_COMMAND,MAKEWPARAM(105,BN_CLICKED),0);
    _WorkspaceTestCheck(result,SendDlgItemMessageW(panel,123,CB_GETCOUNT,0,0)==3 && _AiReadProfile(slotB,&stale) && !wcscmp(stale.configurationId,idB),"saving removed active profile reuses freed slot");
    _WorkspaceTestAiCapacity(result);
    SendMessageW(panel,WM_COMMAND,MAKEWPARAM(106,BN_CLICKED),0);
    _WorkspaceTestCheck(result,AiCredential_Load(profileB,&key)==ERROR_NOT_FOUND && !key,"delete key removes selected account only"); AiCredential_Free(key);key=NULL;
    bool retained=AiCredential_Load(profileA,&key)==ERROR_SUCCESS && !strcmp(key,"fake-panel-test-key-a"); AiCredential_Free(key);
    _WorkspaceTestCheck(result,retained,"other account survives native delete key action");
    AiCredential_Delete(profileA); AiCredential_Delete(profileB);
    SetWindowTextW(GetDlgItem(panel,119),L""); SetWindowTextW(GetDlgItem(panel,103),L"");
    SendMessageW(panel,WM_COMMAND,MAKEWPARAM(105,BN_CLICKED),0);
    _WorkspaceTestCheck(result,!Settings2.AIConfigurationId[0],"clearing account ID explicitly returns to legacy service identity");
    _WorkspaceTestAiAutomaticAccount(panel,result);
    return panel;
}

static bool _WorkspaceTestBatchReady(void)
{
    DWORD start = GetTickCount();
    while (BatchPanel_IsBusy(_WorkspaceBatch()) && GetTickCount() - start < 5000) {
        MsgWaitForMultipleObjectsEx(0, NULL, 50, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        MSG message; while (PeekMessageW(&message, NULL, 0, 0, PM_REMOVE)) { TranslateMessage(&message); DispatchMessageW(&message); }
    }
    return !BatchPanel_IsBusy(_WorkspaceBatch());
}
static bool _WorkspaceTestPreviewReady(bool capture)
{
    DWORD start=GetTickCount();
    while(GetTickCount()-start<15000) {
        if(capture?MarkdownPreview_IsCaptureDone(Globals.hwndMain):(MarkdownPreview_IsRendered(Globals.hwndMain) && MarkdownPreview_IsThemeApplied(Globals.hwndMain))) return true;
        MsgWaitForMultipleObjectsEx(0,NULL,50,QS_ALLINPUT,MWMO_INPUTAVAILABLE); MSG message;
        while(PeekMessageW(&message,NULL,0,0,PM_REMOVE)) { TranslateMessage(&message); DispatchMessageW(&message); }
    }
    return false;
}
static bool _WorkspaceTestLogReady(void)
{
    DWORD start = GetTickCount();
    while (LogPanel_IsBusy(Globals.hwndMain) && GetTickCount() - start < 5000) {
        MsgWaitForMultipleObjectsEx(0, NULL, 50, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        MSG message;
        while (PeekMessageW(&message, NULL, 0, 0, PM_REMOVE)) { TranslateMessage(&message); DispatchMessageW(&message); }
    }
    return !LogPanel_IsBusy(Globals.hwndMain);
}
static HTREEITEM _WorkspaceTestTreeChild(HWND tree, HTREEITEM parent, const WCHAR* name)
{
    for (HTREEITEM item = TreeView_GetChild(tree, parent); item; item = TreeView_GetNextSibling(tree, item)) {
        WCHAR label[256]; TVITEMW entry = { 0 }; entry.mask = TVIF_TEXT; entry.hItem = item; entry.pszText = label; entry.cchTextMax = COUNTOF(label);
        if (TreeView_GetItem(tree, &entry) && wcscmp(label, name) == 0) return item;
    }
    return NULL;
}
static void _WorkspaceTestEncryptedSession(WORKSPACE_TEST_RESULT* result, NP3_SESSION session, const WCHAR* folder, bool mixed)
{
    HPATHL path=Path_Allocate(folder); Path_Append(path,mixed?L"mixed-encryption.np3session":L"two-encrypted.np3session");
    uint32_t error=0; NP3_SESSION loaded=NULL;
    bool saved=session && Session_Save(session,Path_Get(path),&error);
    _WorkspaceTestCheck(result,saved && Session_Load(Path_Get(path),&loaded,&error),"protected encrypted-source session writes and reads actual disk archive");
    NP3_SESSION_DOCUMENT first={0},second={0};
    _WorkspaceTestCheck(result,loaded && Session_Count(loaded)==2 && Session_Active(loaded)==0 && Session_Get(loaded,0,&first) && Session_Get(loaded,1,&second) && (first.flags&NP3_SESSION_ENCRYPTED_SOURCE) && !!(second.flags&NP3_SESSION_ENCRYPTED_SOURCE)==!mixed && first.textLength==8 && !memcmp(first.text,"alpha\r\nA",8) && second.textLength==7 && !memcmp(second.text,"beta\r\nB",7),"disk session readback retains encryption flags active tab and exact document bytes");
    Session_Free(loaded); Path_Release(path);
}
static void _WorkspaceSelfTest()
{
    WCHAR folder[MAX_PATH_EXPLICIT];
    DWORD const length = GetEnvironmentVariableW(L"NP3_WORKSPACE_SELFTEST_DIR", folder, COUNTOF(folder));
    if (!length || length >= COUNTOF(folder)) return;
    HPATHL marker = Path_Allocate(folder); Path_Append(marker, L"workspace-test.marker");
    if (!Path_IsExistingFile(marker)) { Path_Release(marker); return; }
    Path_Release(marker);
    HPATHL logPath = Path_Allocate(folder); Path_Append(logPath, L"workspace-test.log");
    WORKSPACE_TEST_RESULT result = { 0 };
    result.log = Path_CreateFile(logPath, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    Path_Release(logPath);
    if (!IS_VALID_HANDLE(result.log)) return;
    DOCUMENT_WORKSPACE* workspace = _Workspace();
    bool const isolated = workspace && workspace->count == 1 && Path_IsEmpty(Paths.CurrentFile) && !IsSaveNeeded() && SciCall_GetLength() == 0;
    _WorkspaceTestCheck(&result, isolated, "isolated empty startup");
    if (!isolated) { CloseHandle(result.log); return; }
    WCHAR searchSettingsStage[32]={0};
    WCHAR historyStage[32]={0};
    if(GetEnvironmentVariableW(L"NP3_HISTORY_TEST",historyStage,COUNTOF(historyStage))){
        WCHAR store[32768];unsigned long long count=0;
        HPATHL source=Path_Allocate(folder);Path_Append(source,L"中文.md");
        bool enabled=Settings2.HistoryEnabled && Settings.SaveRecentFiles;
        _WorkspaceTestCheck(&result,_WorkspaceHistoryStore(store,COUNTOF(store)),"history sidecar follows isolated INI path");
        _WorkspaceTestCheck(&result,Settings2.HistoryMaximum==(!lstrcmpW(historyStage,L"high")?10000:!lstrcmpW(historyStage,L"low")?1:500),"history capacity defaults and bounds loaded from INI");
        _WorkspaceTestCheck(&result,FileLoad(source,FLF_None,0,0) && _WorkspaceTestText("history fixture"),"successful first open retains original document bytes");
        _WorkspaceTestCheck(&result,FileLoad(source,FLF_None,0,0) && workspace->count==1,"reopening existing file reuses original tab");
        _WorkspaceTestCheck(&result,HistoryService_Flush(5000) && (enabled?(HistoryService_GetEntry(store,Path_Get(source),&count) && count==2):(GetFileAttributesW(store)==INVALID_FILE_ATTRIBUTES)),"enabled opens persist twice while disabled history creates no sidecar");
        if(!lstrcmpW(historyStage,L"enabled")){
            HWND panel=(HWND)SendMessageW(Globals.hwndMain,NP3_WM_HISTORY_SHOW,0,0);
            _WorkspaceTestCheck(&result,panel && _WorkspaceTestHistoryReady(panel) && ListView_GetItemCount(GetDlgItem(panel,106))==1,"history window asynchronously displays persisted metadata");
            _WorkspaceTestCheck(&result,(HWND)SendMessageW(Globals.hwndMain,NP3_WM_HISTORY_SHOW,0,0)==panel,"history window is reused for its owner");
            WCHAR display[32768];ListView_GetItemText(GetDlgItem(panel,106),0,1,display,COUNTOF(display));
            _WorkspaceTestCheck(&result,!lstrcmpW(display,Path_Get(source)),"history virtual row contains exact Unicode document path");
            SetDlgItemTextW(panel,101,L"no-matching-document");
            _WorkspaceTestCheck(&result,ListView_GetItemCount(GetDlgItem(panel,106))==0,"history keyword filters metadata without document changes");
            SetDlgItemTextW(panel,101,L"中文");
            _WorkspaceTestCheck(&result,ListView_GetItemCount(GetDlgItem(panel,106))==1,"history keyword accepts Chinese path");
            SendDlgItemMessageW(panel,109,BM_SETCHECK,BST_UNCHECKED,0);SendMessageW(panel,WM_COMMAND,109,0);
            _WorkspaceTestCheck(&result,!Settings2.HistoryEnabled,"history recording checkbox publishes disabled preference");
            HWND list=GetDlgItem(panel,106);ListView_SetItemState(list,0,LVIS_SELECTED,LVIS_SELECTED);NMHDR notice={list,106,NM_RETURN};SendMessageW(panel,WM_NOTIFY,106,(LPARAM)&notice);
            _WorkspaceTestCheck(&result,workspace->count==1 && _WorkspaceTestText("history fixture") && HistoryService_Flush(5000) && HistoryService_GetEntry(store,Path_Get(source),&count) && count==2,"history Enter reuses tab without overwriting or recording while disabled");
            SendDlgItemMessageW(panel,109,BM_SETCHECK,BST_CHECKED,0);SendMessageW(panel,WM_COMMAND,109,0);
            _WorkspaceTestCheck(&result,Settings2.HistoryEnabled,"history checkbox re-enables recording");
            DestroyWindow(panel);panel=(HWND)SendMessageW(Globals.hwndMain,NP3_WM_HISTORY_SHOW,0,0);
            _WorkspaceTestCheck(&result,_WorkspaceTestHistoryReady(panel) && SendDlgItemMessageW(panel,109,BM_GETCHECK,0,0)==BST_CHECKED && ListView_GetItemCount(GetDlgItem(panel,106))==1,"recreated history window retains metadata and recording preference");
            HPATHL other=Path_Allocate(folder);Path_Append(other,L"other.txt");
            _WorkspaceTestCheck(&result,HistoryService_Record(store,Path_Get(other),500) && HistoryService_Flush(5000),"second owned fixture creates independent history metadata");
            SendMessageW(panel,WM_COMMAND,104,0);
            _WorkspaceTestCheck(&result,_WorkspaceTestHistoryReady(panel) && ListView_GetItemCount(GetDlgItem(panel,106))==2,"refresh merges newly persisted history entries");
            SetDlgItemTextW(panel,102,L"*.TXT");
            _WorkspaceTestCheck(&result,ListView_GetItemCount(GetDlgItem(panel,106))==1,"history type filter matches extension case insensitively");
            ListView_GetItemText(GetDlgItem(panel,106),0,1,display,COUNTOF(display));
            _WorkspaceTestCheck(&result,!lstrcmpW(display,Path_Get(other)),"type filter selects exact second document path");
            SetDlgItemTextW(panel,102,L"*.md;bad/path");GetDlgItemTextW(panel,107,display,COUNTOF(display));
            _WorkspaceTestCheck(&result,ListView_GetItemCount(GetDlgItem(panel,106))==1 && wcsstr(display,L"类型格式无效"),"invalid history type preserves previous valid list");
            SetDlgItemTextW(panel,102,L"*.txt");list=GetDlgItem(panel,106);ListView_SetItemState(list,0,LVIS_SELECTED,LVIS_SELECTED);SendMessageW(panel,WM_COMMAND,110,0);
            bool pinnedReady=_WorkspaceTestHistoryReady(panel);ListView_GetItemText(list,0,4,display,COUNTOF(display));
            _WorkspaceTestCheck(&result,pinnedReady && !lstrcmpW(display,L"置顶"),"history pin button persists and displays pinned state");
            ListView_SetItemState(list,0,LVIS_SELECTED,LVIS_SELECTED);SendMessageW(panel,WM_COMMAND,110,0);bool unpinnedReady=_WorkspaceTestHistoryReady(panel);ListView_GetItemText(list,0,4,display,COUNTOF(display));
            _WorkspaceTestCheck(&result,unpinnedReady && !lstrcmpW(display,L"可打开"),"pin button also removes pinned state");
            ListView_SetItemState(list,0,LVIS_SELECTED,LVIS_SELECTED);SendMessageW(panel,WM_COMMAND,111,0);
            _WorkspaceTestCheck(&result,_WorkspaceTestHistoryReady(panel) && ListView_GetItemCount(list)==0 && Path_IsExistingFile(other),"remove button deletes only history metadata and retains owned source file");
            SetDlgItemTextW(panel,102,L"*");ListView_SetItemState(list,0,LVIS_SELECTED,LVIS_SELECTED);SendMessageW(panel,WM_COMMAND,110,0);
            _WorkspaceTestCheck(&result,_WorkspaceTestHistoryReady(panel) && HistoryService_GetEntry(store,Path_Get(source),&count) && count==2,"pinning original record preserves open count");
            HPATHL missing=Path_Allocate(folder);Path_Append(missing,L"missing.md");HistoryService_Record(store,Path_Get(missing),500);HistoryService_Flush(5000);SendMessageW(panel,WM_COMMAND,104,0);
            _WorkspaceTestCheck(&result,_WorkspaceTestHistoryReady(panel) && ListView_GetItemCount(list)==2,"missing history path stays visible after refresh");
            ListView_GetItemText(list,0,1,display,COUNTOF(display));
            _WorkspaceTestCheck(&result,!lstrcmpW(display,Path_Get(source)),"pinned record precedes newer unpinned record");
            SetDlgItemTextW(panel,101,L"missing.md");ListView_SetItemState(list,0,LVIS_SELECTED,LVIS_SELECTED);notice.hwndFrom=list;SendMessageW(panel,WM_NOTIFY,106,(LPARAM)&notice);GetDlgItemTextW(panel,107,display,COUNTOF(display));
            _WorkspaceTestCheck(&result,wcsstr(display,L"文件不存在") && workspace->count==1 && _WorkspaceTestText("history fixture"),"missing history Enter preserves active document and reports failure");
            SendMessageW(panel,WM_COMMAND,111,0);
            _WorkspaceTestCheck(&result,_WorkspaceTestHistoryReady(panel) && ListView_GetItemCount(list)==0 && !Path_IsExistingFile(missing),"missing history entry can be removed without creating a file");
            Path_Release(missing);Path_Release(other);
            DestroyWindow(panel);
        }
        Settings2.HistoryEnabled=false;
        _WorkspaceTestCheck(&result,FileLoad(source,FLF_None,0,0) && HistoryService_Flush(5000) && (!enabled || (HistoryService_GetEntry(store,Path_Get(source),&count) && count==2)),"recording switch prevents repeated open count changes");
        Settings2.HistoryEnabled=enabled;
        _WorkspaceTestCheck(&result,FileLoad(source,FLF_Reload|FLF_DontSave,0,0) && HistoryService_Flush(5000) && (!enabled || (HistoryService_GetEntry(store,Path_Get(source),&count) && count==2)),"reload does not add a new history open");
        _WorkspaceTestCheck(&result,FileLoad(source,FLF_New|FLF_DontSave,0,0) && HistoryService_Flush(5000) && (!enabled || (HistoryService_GetEntry(store,Path_Get(source),&count) && count==2)),"untitled document creation does not record supplied path");
        if(!lstrcmpW(historyStage,L"clear")){
            HWND panel=(HWND)SendMessageW(Globals.hwndMain,NP3_WM_HISTORY_SHOW,0,0);WCHAR display[256];
            _WorkspaceTestCheck(&result,_WorkspaceTestHistoryReady(panel) && ListView_GetItemCount(GetDlgItem(panel,106))==1,"clear fixture starts with a persisted history record");
            SetDlgItemTextW(panel,114,L"2");SendMessageW(panel,WM_COMMAND,115,0);
            _WorkspaceTestCheck(&result,Settings2.HistoryMaximum==2,"capacity button applies valid history bound");
            const WCHAR* invalid[]={L"0",L"10001",L"oops",L""};
            for(int i=0;i<4;++i){SetDlgItemTextW(panel,114,invalid[i]);SendMessageW(panel,WM_COMMAND,115,0);GetDlgItemTextW(panel,107,display,COUNTOF(display));_WorkspaceTestCheck(&result,Settings2.HistoryMaximum==2 && wcsstr(display,L"1–10000"),"invalid capacity keeps previous valid setting");}
            DestroyWindow(panel);panel=(HWND)SendMessageW(Globals.hwndMain,NP3_WM_HISTORY_SHOW,0,0);GetDlgItemTextW(panel,114,display,COUNTOF(display));
            _WorkspaceTestCheck(&result,_WorkspaceTestHistoryReady(panel) && !lstrcmpW(display,L"2"),"recreated history panel restores applied capacity");
            SendMessageW(panel,WM_COMMAND,112,0);
            _WorkspaceTestCheck(&result,IsWindowVisible(GetDlgItem(panel,116)) && HistoryService_GetEntry(store,Path_Get(source),&count) && count==2,"first clear click asks inline confirmation without mutation");
            SendMessageW(panel,WM_COMMAND,116,0);
            _WorkspaceTestCheck(&result,!IsWindowVisible(GetDlgItem(panel,116)) && HistoryService_GetEntry(store,Path_Get(source),&count),"cancel clear retains persisted metadata");
            SendMessageW(panel,WM_COMMAND,112,0);SendMessageW(panel,WM_COMMAND,112,0);
            _WorkspaceTestCheck(&result,_WorkspaceTestHistoryReady(panel) && ListView_GetItemCount(GetDlgItem(panel,106))==0 && !HistoryService_GetEntry(store,Path_Get(source),&count),"confirmed clear removes all history records on disk and in window");
            _WorkspaceTestCheck(&result,Path_IsExistingFile(source) && workspace->count==2 && SciCall_GetLength()==0,"clear preserves original file and independent untitled document");
            DestroyWindow(panel);
        }
        Path_Release(source);
        if(!lstrcmpW(historyStage,L"relocate")){
            HPATHL missing=Path_Allocate(folder);Path_Append(missing,L"missing-relocate.md");HPATHL destination=Path_Allocate(folder);Path_Append(destination,L"other.txt");HPATHL collision=Path_Allocate(folder);Path_Append(collision,L"中文.md");
            HistoryService_Record(store,Path_Get(missing),500);HistoryService_Position(store,Path_Get(missing),500,7,9);HistoryService_Flush(5000);
            HWND panel=(HWND)SendMessageW(Globals.hwndMain,NP3_WM_HISTORY_SHOW,0,0);WCHAR display[32768];
            _WorkspaceTestCheck(&result,_WorkspaceTestHistoryReady(panel),"relocation panel reads missing source record");SetDlgItemTextW(panel,101,L"missing-relocate.md");HWND list=GetDlgItem(panel,106);ListView_SetItemState(list,0,LVIS_SELECTED,LVIS_SELECTED);SendMessageW(panel,WM_COMMAND,110,0);_WorkspaceTestHistoryReady(panel);ListView_SetItemState(list,0,LVIS_SELECTED,LVIS_SELECTED);
            SetDlgItemTextW(panel,118,L"relative.txt");SendMessageW(panel,WM_COMMAND,117,0);GetDlgItemTextW(panel,107,display,COUNTOF(display));
            _WorkspaceTestCheck(&result,wcsstr(display,L"完整路径") && HistoryService_GetEntry(store,Path_Get(missing),&count),"relative relocation input is rejected without mutation");
            SetDlgItemTextW(panel,118,Path_Get(collision));SendMessageW(panel,WM_COMMAND,117,0);bool collisionRejected=!_WorkspaceTestHistoryReady(panel);GetDlgItemTextW(panel,107,display,COUNTOF(display));
            _WorkspaceTestCheck(&result,collisionRejected && wcsstr(display,L"183") && HistoryService_GetEntry(store,Path_Get(missing),&count),"existing destination history conflict preserves old record and reports error");
            SetDlgItemTextW(panel,118,Path_Get(destination));ListView_SetItemState(list,0,LVIS_SELECTED,LVIS_SELECTED);SendMessageW(panel,WM_COMMAND,117,0);
            _WorkspaceTestCheck(&result,_WorkspaceTestHistoryReady(panel) && !HistoryService_GetEntry(store,Path_Get(missing),&count) && HistoryService_GetEntry(store,Path_Get(destination),&count) && count==1,"relocate button replaces old path on disk without incrementing opens");
            unsigned long long line=0,column=0;
            _WorkspaceTestCheck(&result,HistoryService_GetPosition(store,Path_Get(destination),&line,&column) && line==7 && column==9,"relocation preserves reading coordinates");
            SetDlgItemTextW(panel,101,L"other.txt");ListView_GetItemText(list,0,4,display,COUNTOF(display));
            _WorkspaceTestCheck(&result,ListView_GetItemCount(list)==1 && !lstrcmpW(display,L"置顶") && Path_IsExistingFile(destination) && !Path_IsExistingFile(missing),"relocated record remains pinned and only metadata changes");
            ListView_SetItemState(list,0,LVIS_SELECTED,LVIS_SELECTED);NMHDR notice={list,106,NM_RETURN};SendMessageW(panel,WM_NOTIFY,106,(LPARAM)&notice);
            _WorkspaceTestCheck(&result,_WorkspaceTestText("owned history removal fixture") && !IsSaveNeeded(),"relocated history opens selected existing document");
            DestroyWindow(panel);Path_Release(missing);Path_Release(destination);Path_Release(collision);
        }
        if(!lstrcmpW(historyStage,L"position")){
            HPATHL reading=Path_Allocate(folder);Path_Append(reading,L"position.md");
            _WorkspaceTestCheck(&result,FileLoad(reading,FLF_None,0,0),"position fixture opens in independent document");
            DocPos caret=SciCall_FindColumn(1,6);SciCall_SetSel(caret,caret);unsigned long long expectedColumn=(unsigned long long)SciCall_GetColumn(caret)+1;
            int index=workspace->active;unsigned long long savedLine=0,savedColumn=0;
            _WorkspaceTestCheck(&result,_WorkspaceSwitch(0) && HistoryService_Flush(5000) && HistoryService_GetPosition(store,Path_Get(reading),&savedLine,&savedColumn) && savedLine==2 && savedColumn==expectedColumn,"tab switch persists Unicode and tab-aware reading coordinates");
            _WorkspaceTestCheck(&result,_WorkspaceSwitch(index) && _WorkspaceCloseActive() && HistoryService_Flush(5000),"closing read document persists position without changing other tabs");
            HWND panel=(HWND)SendMessageW(Globals.hwndMain,NP3_WM_HISTORY_SHOW,0,0);
            _WorkspaceTestCheck(&result,_WorkspaceTestHistoryReady(panel),"history window reloads recorded reading position from disk");
            SetDlgItemTextW(panel,101,L"position.md");HWND list=GetDlgItem(panel,106);ListView_SetItemState(list,0,LVIS_SELECTED,LVIS_SELECTED);NMHDR notice={list,106,NM_RETURN};SendMessageW(panel,WM_NOTIFY,106,(LPARAM)&notice);
            _WorkspaceTestCheck(&result,SciCall_GetCurrentPos()==caret && SciCall_LineFromPosition(SciCall_GetCurrentPos())==1 && (unsigned long long)SciCall_GetColumn(SciCall_GetCurrentPos())+1==expectedColumn,"history Enter restores position after actual close and reopen");
            SciCall_SetSel(0,3);int countBefore=workspace->count;SendMessageW(panel,WM_NOTIFY,106,(LPARAM)&notice);
            _WorkspaceTestCheck(&result,workspace->count==countBefore && SciCall_GetAnchor()==0 && SciCall_GetCurrentPos()==3,"history activation of already open tab preserves current selection");
            int readingIndex=workspace->active;_WorkspaceCloseActive();HISTORY_LOCATION beyond={Path_Get(reading),~0ULL,~0ULL};
            _WorkspaceTestCheck(&result,SendMessageW(Globals.hwndMain,NP3_WM_HISTORY_OPEN,0,(LPARAM)&beyond) && SciCall_GetCurrentPos()==SciCall_GetLength(),"stale oversized reading coordinates clamp safely to document end");
            _WorkspaceTestCheck(&result,workspace->active==readingIndex && !IsSaveNeeded() && _WorkspaceTestText("first\r\n\t\xe4\xb8\xad\xe6\x96\x87 abc\r\nlast"),"position restore retains exact UTF8 bytes and saved state");
            DestroyWindow(panel);Path_Release(reading);
        }
        char summary[128];DWORD written;StringCchPrintfA(summary,COUNTOF(summary),"RESULT checks=%i failures=%i\r\n",result.checks,result.failures);WriteFile(result.log,summary,(DWORD)strlen(summary),&written,NULL);CloseHandle(result.log);
        _WorkspaceDispose();DestroyWindow(Globals.hwndMain);return;
    }
    if(GetEnvironmentVariableW(L"NP3_SEARCH_SETTINGS_TEST",searchSettingsStage,COUNTOF(searchSettingsStage))){
        HWND searchSettings=FileSearch_Show(Globals.hwndMain,folder);
        bool write=!lstrcmpW(searchSettingsStage,L"write"),custom=write || !lstrcmpW(searchSettingsStage,L"read");
        if(write){SetDlgItemTextW(searchSettings,102,L"*.md;*.txt");SetDlgItemTextW(searchSettings,108,L"缓存;.git");const int ids[]={109,111,113,114};for(int i=0;i<4;++i)SendDlgItemMessageW(searchSettings,ids[i],BM_SETCHECK,i?BST_CHECKED:BST_UNCHECKED,0);SendMessageW(searchSettings,WM_COMMAND,104,0);
            _WorkspaceTestCheck(&result,_WorkspaceTestSearchReady(searchSettings) && ListView_GetItemCount(GetDlgItem(searchSettings,106))==1,"accepted search settings execute actual fixture scan");
            DestroyWindow(searchSettings);searchSettings=FileSearch_Show(Globals.hwndMain,folder);
        }
        WCHAR types[4097],excluded[4097],query[64];GetDlgItemTextW(searchSettings,102,types,COUNTOF(types));GetDlgItemTextW(searchSettings,108,excluded,COUNTOF(excluded));GetDlgItemTextW(searchSettings,110,query,COUNTOF(query));
        UINT expected=custom?14:!lstrcmpW(searchSettingsStage,L"high")?15:1,actual=0;const int ids[]={109,111,113,114};for(int i=0;i<4;++i)if(SendDlgItemMessageW(searchSettings,ids[i],BM_GETCHECK,0,0)==BST_CHECKED)actual|=1u<<i;
        _WorkspaceTestCheck(&result,searchSettings && !lstrcmpW(types,custom?L"*.md;*.txt":L"*.md") && !lstrcmpW(excluded,custom?L"缓存;.git":L".git;node_modules;.np3-extension-backups;render-assets"),"search window restores custom or default file filters and exclusions");
        _WorkspaceTestCheck(&result,actual==expected && Settings2.SearchFlags==(int)expected,"search window restores persisted flags and clamps unsupported flag range");
        _WorkspaceTestCheck(&result,!query[0] && ListView_GetItemCount(GetDlgItem(searchSettings,106))==0 && workspace->count==1 && SciCall_GetLength()==0 && !IsSaveNeeded(),"settings restoration does not auto scan persist body queries or change document");
        DestroyWindow(searchSettings);char summary[128];DWORD written;StringCchPrintfA(summary,COUNTOF(summary),"RESULT checks=%i failures=%i\r\n",result.checks,result.failures);WriteFile(result.log,summary,(DWORD)strlen(summary),&written,NULL);CloseHandle(result.log);
        _WorkspaceDispose();DestroyWindow(Globals.hwndMain);return;
    }
    WCHAR reopenStage[32]={0};
    if(GetEnvironmentVariableW(L"NP3_WORKSPACE_OPTION_TEST",reopenStage,COUNTOF(reopenStage))) {
        SendMessageW(Globals.hwndMain,WM_INITMENU,(WPARAM)Globals.hMainMenu,0);
        bool initial=Settings2.ReopenWorkspace;
        WCHAR label[160]={0}; GetMenuStringW(Globals.hMainMenu,IDM_SET_REOPEN_WORKSPACE,label,COUNTOF(label),MF_BYCOMMAND);
        _WorkspaceTestCheck(&result,label[0] && IsCmdEnabled(Globals.hwndMain,IDM_SET_REOPEN_WORKSPACE),"workspace reopen option is present and enabled in actual menu");
        _WorkspaceTestCheck(&result,!!(GetMenuState(Globals.hMainMenu,IDM_SET_REOPEN_WORKSPACE,MF_BYCOMMAND)&MF_CHECKED)==initial,"workspace reopen menu check reflects loaded INI setting");
        bool readStage=!lstrcmpW(reopenStage,L"read-on") || !lstrcmpW(reopenStage,L"read-off");
        if(readStage) {
            _WorkspaceTestCheck(&result,initial==!lstrcmpW(reopenStage,L"read-on"),"new process reads independently saved workspace reopen preference");
        } else {
            unsigned long long optionVersion=_WorkspaceVersion(); sptr_t optionHandle=SciCall_GetDocPointer();
            SendWMCommand(Globals.hwndMain,IDM_SET_REOPEN_WORKSPACE);
            _WorkspaceTestCheck(&result,Settings2.ReopenWorkspace!=initial && !!(GetMenuState(Globals.hMainMenu,IDM_SET_REOPEN_WORKSPACE,MF_BYCOMMAND)&MF_CHECKED)==!initial,"menu command toggles reopen preference and visible check immediately");
            _WorkspaceTestCheck(&result,workspace->count==1 && SciCall_GetDocPointer()==optionHandle && _WorkspaceVersion()==optionVersion && !IsSaveNeeded() && SciCall_GetLength()==0,"workspace preference command preserves document identity text and saved state");
            if(!lstrcmpW(reopenStage,L"force")) _WorkspaceTestCheck(&result,SaveAllSettings(true),"explicit settings save persists workspace preference with automatic saving disabled");
        }
        char summary[128]; DWORD written;
        StringCchPrintfA(summary,COUNTOF(summary),"RESULT checks=%i failures=%i\r\n",result.checks,result.failures); WriteFile(result.log,summary,(DWORD)strlen(summary),&written,NULL); CloseHandle(result.log);
        _WorkspaceDispose(); DestroyWindow(Globals.hwndMain); return;
    }
    WCHAR treeStage[32]={0};
    if (GetEnvironmentVariableW(L"NP3_FILETREE_SETTINGS_TEST",treeStage,COUNTOF(treeStage))) {
        WCHAR chosen[32768]; StringCchPrintfW(chosen,COUNTOF(chosen),L"%s\\自选目录",folder);
        FILETREE_LABELS labels={L"Choose",L"Refresh",L"Loading",L"More"};
        bool writeStage=lstrcmpW(treeStage,L"write")==0 || lstrcmpW(treeStage,L"disabled")==0;
        int initialWidth=Settings2.FileTreeWidth;
        FileTree_Toggle(Globals.hwndMain,folder,&labels);
        _WorkspaceTestCheck(&result,FileTree_IsOpen(Globals.hwndMain) && _WorkspaceTestTreeReady(),"settings test opens real directory tree");
        if (writeStage) {
            _WorkspaceTestCheck(&result,FileTree_SetRoot(Globals.hwndMain,chosen) && _WorkspaceTestTreeReady(),"settings test selects Unicode root");
            HWND panel=FindWindowExW(Globals.hwndMain,NULL,L"NP3WorkspaceFileTree",NULL);
            HWND grip=FindWindowExW(panel,NULL,L"STATIC",NULL);
            if(grip) { SendMessageW(grip,WM_LBUTTONDOWN,MK_LBUTTON,0); SendMessageW(grip,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(40,0)); SendMessageW(grip,WM_LBUTTONUP,0,0); }
            _WorkspaceTestCheck(&result,grip && Settings2.FileTreeWidth>initialWidth && lstrcmpW(Settings2.FileTreeRoot,chosen)==0,"confirmed drag and root publish to settings while panel remains open");
        } else {
            WCHAR actual[32768]={0}; HWND panel=FindWindowExW(Globals.hwndMain,NULL,L"NP3WorkspaceFileTree",NULL); HWND tree=FindWindowExW(panel,NULL,WC_TREEVIEWW,NULL);
            TVITEMW item={0}; item.mask=TVIF_TEXT; item.hItem=TreeView_GetRoot(tree); item.pszText=actual; item.cchTextMax=COUNTOF(actual); TreeView_GetItem(tree,&item);
            const WCHAR* expected=lstrcmpW(treeStage,L"read")==0?chosen:folder;
            _WorkspaceTestCheck(&result,lstrcmpW(actual,expected)==0 && lstrcmpW(Settings2.FileTreeRoot,expected)==0,"new process restores root or replaces unavailable root with current directory");
            int width=FileTree_Layout(Globals.hwndMain,0,0,40000,700);
            _WorkspaceTestCheck(&result,width==MulDiv(Settings2.FileTreeWidth,GetDpiForWindow(Globals.hwndMain),96),"new process renders saved logical width at current DPI");
            int expectedWidth=lstrcmpW(treeStage,L"low")==0?120:lstrcmpW(treeStage,L"high")==0?10000:initialWidth;
            _WorkspaceTestCheck(&result,Settings2.FileTreeWidth==expectedWidth,"loaded tree width stays within supported bounds");
        }
        char summary[128]; DWORD written;
        StringCchPrintfA(summary,COUNTOF(summary),"RESULT checks=%i failures=%i\r\n",result.checks,result.failures); WriteFile(result.log,summary,(DWORD)strlen(summary),&written,NULL); CloseHandle(result.log);
        _WorkspaceDispose(); DestroyWindow(Globals.hwndMain); return;
    }
    int markdownButtonIndex=(int)SendMessageW(Globals.hwndToolbar,TB_COMMANDTOINDEX,IDT_VIEW_MARKDOWN_PREVIEW,0);
    TBBUTTON markdownButton={0}; HIMAGELIST toolbarImages=(HIMAGELIST)SendMessageW(Globals.hwndToolbar,TB_GETIMAGELIST,0,0);
    _WorkspaceTestCheck(&result,markdownButtonIndex>=0 && SendMessageW(Globals.hwndToolbar,TB_GETBUTTON,markdownButtonIndex,(LPARAM)&markdownButton) && toolbarImages && markdownButton.iBitmap>=0 && markdownButton.iBitmap<ImageList_GetImageCount(toolbarImages) && !(markdownButton.fsStyle&BTNS_SHOWTEXT),"Markdown toolbar shortcut uses an actual image without text label");
    PANEL_COLORS toolbarPalette={0}; _PanelColors(&toolbarPalette);
    _WorkspaceTestCheck(&result,_WorkspaceTestToolbarGlyph(TB_GETIMAGELIST,GetModeTextColor(UseDarkMode())) && _WorkspaceTestToolbarGlyph(TB_GETHOTIMAGELIST,GetModeTextColor(UseDarkMode())) && _WorkspaceTestToolbarGlyph(TB_GETDISABLEDIMAGELIST,toolbarPalette.disabled),"M normal hot and disabled image pixels follow active application palette");
    WCHAR savedToolbarButtons[COUNTOF(Settings.ToolbarButtons)]; StringCchCopyW(savedToolbarButtons,COUNTOF(savedToolbarButtons),Settings.ToolbarButtons);
    StringCchCopyW(Settings.ToolbarButtons,COUNTOF(Settings.ToolbarButtons),L"1 2 4 3");
    NMHDR toolbarNotice={Globals.hwndToolbar,IDC_TOOLBAR,TBN_BEGINADJUST}; SendMessageW(Globals.hwndMain,WM_NOTIFY,IDC_TOOLBAR,(LPARAM)&toolbarNotice);
    toolbarNotice.code=TBN_RESET; SendMessageW(Globals.hwndMain,WM_NOTIFY,IDC_TOOLBAR,(LPARAM)&toolbarNotice);
    _WorkspaceTestCheck(&result,SendMessageW(Globals.hwndToolbar,TB_COMMANDTOINDEX,IDT_VIEW_MARKDOWN_PREVIEW,0)==0 && SendMessageW(Globals.hwndToolbar,TB_BUTTONCOUNT,0,0)==5,"restoring legacy custom toolbar retains M shortcut and four existing buttons");
    SendMessageW(Globals.hwndMain,WM_NOTIFY,IDC_TOOLBAR,(LPARAM)&toolbarNotice);
    _WorkspaceTestCheck(&result,SendMessageW(Globals.hwndToolbar,TB_COMMANDTOINDEX,IDT_VIEW_MARKDOWN_PREVIEW,0)>=0,"second toolbar reset also retains Markdown shortcut");
    StringCchCopyW(Settings.ToolbarButtons,COUNTOF(Settings.ToolbarButtons),savedToolbarButtons);
    toolbarNotice.code=TBN_BEGINADJUST; SendMessageW(Globals.hwndMain,WM_NOTIFY,IDC_TOOLBAR,(LPARAM)&toolbarNotice);
    toolbarNotice.code=TBN_RESET; SendMessageW(Globals.hwndMain,WM_NOTIFY,IDC_TOOLBAR,(LPARAM)&toolbarNotice);
    toolbarNotice.code=TBN_ENDADJUST; SendMessageW(Globals.hwndMain,WM_NOTIFY,IDC_TOOLBAR,(LPARAM)&toolbarNotice);
    HPATHL a = Path_Allocate(folder); Path_Append(a, L"a.txt");
    HPATHL b = Path_Allocate(folder); Path_Append(b, L"b.txt");
    _WorkspaceTestCheck(&result, FileLoad(a, FLF_None, 0, 0), "load A");
    _WorkspaceTestCheck(&result, workspace->count == 1 && _WorkspaceTestText("alpha\r\n"), "reuse empty tab");
    _WorkspaceTestCheck(&result, TabCtrl_GetItemCount(workspace->tabs) == 1 && IsWindow(workspace->closeButton), "native tab and close controls");
    WCHAR label[128] = { 0 }; TCITEMW tab = { 0 }; tab.mask = TCIF_TEXT; tab.pszText = label; tab.cchTextMax = COUNTOF(label);
    TabCtrl_GetItem(workspace->tabs, 0, &tab);
    _WorkspaceTestCheck(&result, wcscmp(label, L"a.txt") == 0, "tab uses filename label");
    sptr_t const handleA = SciCall_GetDocPointer();
    unsigned long long const idA = workspace->documents[0].id;
    Encoding_Current(CPI_UTF8SIGN); SciCall_SetEOLMode(SC_EOL_CRLF);
    SciCall_AppendText(1, "A"); SciCall_SetSel(1, 3);
    _WorkspaceTestCheck(&result, IsSaveNeeded() && SciCall_CanUndo(), "A dirty and undoable");
    _WorkspaceTestCheck(&result, FileLoad(b, FLF_None, 0, 0), "load B without saving A");
    _WorkspaceTestCheck(&result, workspace->count == 2 && SciCall_GetDocPointer() != handleA, "separate document handles");
    _WorkspaceTestCheck(&result, workspace->documents[1].id != idA, "stable document IDs");
    _WorkspaceTestCheck(&result, Encoding_GetCurrent() == CPI_UNICODEBOM && _WorkspaceTestText("beta\r\n"), "B UTF16 decoding");
    _WorkspaceTestCheck(&result, workspace->documents[0].modified, "inactive A remains dirty");
    _WorkspaceTestCheck(&result, SendMessage(Globals.hwndMain, WM_FILECHANGEDNOTIFY, (WPARAM)idA, 0) == 0 && _WorkspaceTestText("beta\r\n"), "stale file notification ignored");
    _WorkspaceTestCheck(&result, _WorkspaceOwnsPath(a) && _WorkspaceOwnsPath(b), "inactive snapshot paths remain owned");
    SciCall_AppendText(1, "B"); SciCall_Undo();
    _WorkspaceTestCheck(&result, _WorkspaceTestText("beta\r\n"), "B independent undo");
    SciCall_Redo();
    _WorkspaceTestCheck(&result, _WorkspaceTestText("beta\r\nB"), "B redo");
    SciCall_SetReadOnly(true);
    TabCtrl_SetCurSel(workspace->tabs, 0);
    NMHDR selection = { workspace->tabs, IDC_WORKSPACE_TABS, TCN_SELCHANGE };
    SendMessage(Globals.hwndMain, WM_NOTIFY, IDC_WORKSPACE_TABS, (LPARAM)&selection);
    _WorkspaceTestCheck(&result, workspace->active == 0, "native tab selection activates A");
    _WorkspaceTestCheck(&result, SciCall_GetDocPointer() == handleA && _WorkspaceTestText("alpha\r\nA"), "A content and handle preserved");
    _WorkspaceTestCheck(&result, Encoding_GetCurrent() == CPI_UTF8SIGN && SciCall_GetEOLMode() == SC_EOL_CRLF, "A encoding and EOL restored");
    _WorkspaceTestCheck(&result, !SciCall_GetReadOnly() && SciCall_GetAnchor() == 1 && SciCall_GetCurrentPos() == 3, "A read-only and selection restored");
    SciCall_Undo(); _WorkspaceTestCheck(&result, _WorkspaceTestText("alpha\r\n"), "A undo unaffected by B");
    SciCall_Redo(); _WorkspaceTestCheck(&result, _WorkspaceTestText("alpha\r\nA"), "A redo");
    SciCall_SetSelection(2, 1); SciCall_AddSelection(6, 5);
    _WorkspaceSwitch(1); _WorkspaceSwitch(0);
    _WorkspaceTestCheck(&result, SciCall_GetSelections() == 2 && SciCall_GetSelectionNAnchor(1) == 5 && SciCall_GetSelectionNCaret(1) == 6, "multiple selections preserved");
    SciCall_SetSel(1, 3);
    _WorkspaceTestCheck(&result, FileSave(FSF_SaveAlways), "save A");
    _WorkspaceTestCheck(&result, !IsSaveNeeded() && Path_StrgComparePath(Paths.CurrentFile, a, Paths.WorkingDirectory, true) == 0, "A save state and path");
    _WorkspaceSwitch(1);
    _WorkspaceTestCheck(&result, SciCall_GetReadOnly() && IsSaveNeeded() && _WorkspaceTestText("beta\r\nB"), "B state retained after A save");
    SciCall_SetReadOnly(false);
    _WorkspaceTestCheck(&result, FileSave(FSF_SaveAlways), "save B");
    _WorkspaceCapture();
    void* savedEncryptionA=workspace->documents[0].encryption;
    void* savedEncryptionB=workspace->documents[1].encryption;
    void* testEncryptionA=Encryption_CreateContext(); void* testEncryptionB=Encryption_CreateContext();
    bool encryptionReady=Encryption_SeedSelfTestContext(testEncryptionA,false) && Encryption_SeedSelfTestContext(testEncryptionB,true);
    _WorkspaceTestCheck(&result,encryptionReady,"create marker-authorized distinct non-secret encryption fixtures");
    if(encryptionReady) {
        workspace->documents[0].encryption=testEncryptionA; workspace->documents[1].encryption=testEncryptionB;
        Encryption_RestoreContext(testEncryptionB);
        for(int encryptedIndex=0;encryptedIndex<3;++encryptedIndex) {
            int documentIndex=encryptedIndex==1?1:0;
            _WorkspaceSwitch(documentIndex);
            HPATHL encryptedOutput=Path_Allocate(folder);
            Path_Append(encryptedOutput,encryptedIndex==0?L"encrypted-a.bin":encryptedIndex==1?L"encrypted-b.bin":L"encrypted-a-again.bin");
            HANDLE encryptedFile=Path_CreateFile(encryptedOutput,GENERIC_WRITE,0,NULL,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,NULL);
            DocPos encryptedLength=SciCall_GetLength(); size_t encryptedWritten=0;
            bool encryptedSaved=IS_VALID_HANDLE(encryptedFile) && IsEncryptionRequired() && EncryptAndWriteFile(Globals.hwndEdit,encryptedFile,(BYTE*)SciCall_GetRangePointer(0,encryptedLength),(size_t)encryptedLength,&encryptedWritten);
            if(IS_VALID_HANDLE(encryptedFile)) CloseHandle(encryptedFile);
            _WorkspaceTestCheck(&result,encryptedSaved,"actual encryption writer uses active per-tab context across A B A switches");
            _WorkspaceTestCheck(&result,!IsSaveNeeded() && _WorkspaceTestText(documentIndex?"beta\r\nB":"alpha\r\nA"),"encrypted snapshot writing preserves saved source text and savepoint");
            Path_Release(encryptedOutput);
        }
        unsigned long long encryptionVersion=_WorkspaceVersion(); int encryptionUndo=SciCall_GetUndoCurrent();
        uint32_t encryptionError=0; NP3_SESSION encryptionSession=_WorkspaceSessionCapture(&encryptionError);
        NP3_SESSION_DOCUMENT encryptedEntryA={0},encryptedEntryB={0};
        _WorkspaceTestCheck(&result,encryptionSession && Session_Count(encryptionSession)==2 && Session_Active(encryptionSession)==0 && Session_Get(encryptionSession,0,&encryptedEntryA) && Session_Get(encryptionSession,1,&encryptedEntryB) && (encryptedEntryA.flags&NP3_SESSION_ENCRYPTED_SOURCE) && (encryptedEntryB.flags&NP3_SESSION_ENCRYPTED_SOURCE),"session capture marks both distinct encrypted document sources");
        _WorkspaceTestEncryptedSession(&result,encryptionSession,folder,false);
        Session_Free(encryptionSession);
        workspace->documents[1].encryption=savedEncryptionB;
        encryptionSession=_WorkspaceSessionCapture(&encryptionError);
        _WorkspaceTestCheck(&result,encryptionSession && Session_Get(encryptionSession,0,&encryptedEntryA) && Session_Get(encryptionSession,1,&encryptedEntryB) && (encryptedEntryA.flags&NP3_SESSION_ENCRYPTED_SOURCE) && !(encryptedEntryB.flags&NP3_SESSION_ENCRYPTED_SOURCE),"mixed session capture does not mark plain source encrypted");
        _WorkspaceTestEncryptedSession(&result,encryptionSession,folder,true);
        Session_Free(encryptionSession);
        _WorkspaceTestCheck(&result,_WorkspaceVersion()==encryptionVersion && SciCall_GetUndoCurrent()==encryptionUndo && SciCall_GetDocPointer()==handleA && !IsSaveNeeded() && _WorkspaceTestText("alpha\r\nA"),"encrypted and mixed snapshots preserve active document version undo and savepoint");
        HPATHL afterSession=Path_Allocate(folder); Path_Append(afterSession,L"encrypted-after-session.bin");
        HANDLE afterFile=Path_CreateFile(afterSession,GENERIC_WRITE,0,NULL,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,NULL);
        size_t afterWritten=0; DocPos afterLength=SciCall_GetLength();
        _WorkspaceTestCheck(&result,IS_VALID_HANDLE(afterFile) && IsEncryptionRequired() && EncryptAndWriteFile(Globals.hwndEdit,afterFile,(BYTE*)SciCall_GetRangePointer(0,afterLength),(size_t)afterLength,&afterWritten),"session snapshot restores active encryption context before actual writer");
        if(IS_VALID_HANDLE(afterFile)) CloseHandle(afterFile); Path_Release(afterSession);
        workspace->documents[0].encryption=savedEncryptionA; workspace->documents[1].encryption=savedEncryptionB;
        Encryption_RestoreContext(workspace->documents[workspace->active].encryption);
        _WorkspaceTestCheck(&result,!IsEncryptionRequired(),"restoring plain contexts leaves no fixture key active");
    }
    Encryption_FreeContext(testEncryptionA); Encryption_FreeContext(testEncryptionB);
    HPATHL encryptedSaveA=Path_Allocate(folder),encryptedSaveB=Path_Allocate(folder);
    Path_Append(encryptedSaveA,L"save-encrypted-utf8.txt"); Path_Append(encryptedSaveB,L"save-encrypted-utf16.txt");
    bool saveFixtures=CopyFileW(Path_Get(a),Path_Get(encryptedSaveA),TRUE) && CopyFileW(Path_Get(b),Path_Get(encryptedSaveB),TRUE);
    _WorkspaceTestCheck(&result,saveFixtures,"copy independent UTF8 and UTF16 encrypted-save fixtures");
    if(saveFixtures) {
        _WorkspaceTestCheck(&result,FileLoad(encryptedSaveA,FLF_DontSave,0,0),"load independent UTF8 encrypted-save source");
        _WorkspaceTestCheck(&result,Encryption_SeedSelfTestContext(workspace->documents[workspace->active].encryption,false),"seed fixed A key for actual FileSave");
        Encryption_RestoreContext(workspace->documents[workspace->active].encryption);
        SciCall_AppendText(16,"\r\nencrypted-save");
        _WorkspaceTestCheck(&result,FileSave(FSF_SaveAlways) && !IsSaveNeeded(),"actual FileSave encrypts UTF8 source with A context");
        _WorkspaceTestCheck(&result,FileLoad(encryptedSaveB,FLF_DontSave,0,0),"load independent UTF16 encrypted-save source");
        _WorkspaceTestCheck(&result,Encryption_SeedSelfTestContext(workspace->documents[workspace->active].encryption,true),"seed fixed B key for actual FileSave");
        Encryption_RestoreContext(workspace->documents[workspace->active].encryption);
        SciCall_AppendText(16,"\r\nencrypted-save");
        _WorkspaceTestCheck(&result,FileSave(FSF_SaveAlways) && !IsSaveNeeded(),"actual FileSave encrypts UTF16 source with B context");
        _WorkspaceTestCheck(&result,_WorkspaceSwitch(2) && IsEncryptionRequired(),"switching saved encrypted tabs restores A context");
        SciCall_AppendText(10,"\r\nsecond-A");
        _WorkspaceTestCheck(&result,FileSave(FSF_SaveAlways) && !IsSaveNeeded(),"second actual FileSave retains A key after B save");
        _WorkspaceTestCheck(&result,_WorkspaceCloseActive() && _WorkspaceCloseActive() && workspace->count==2 && !IsEncryptionRequired(),"closing encrypted fixtures restores original plain workspace context");
    }
    Path_Release(encryptedSaveA); Path_Release(encryptedSaveB);
    _WorkspaceTestCheck(&result, FileLoad(a, FLF_None, 0, 0), "open existing A activates tab");
    _WorkspaceTestCheck(&result, workspace->count == 2 && workspace->active == 0 && SciCall_GetDocPointer() == handleA, "duplicate open does not add tab");
    FileWatching.FileWatchingMode = FWM_DONT_CARE; InstallFileWatching(false);
    HPATHL c = Path_Allocate(folder); Path_Append(c, L"c.txt");
    _WorkspaceTestCheck(&result, FileLoad(c, FLF_None, 0, 0) && workspace->count == 3, "third document");
    FileWatching.FileWatchingMode = FWM_DONT_CARE; InstallFileWatching(false);
    _WorkspaceSwitch(0);
    HANDLE external = Path_CreateFile(c, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    DWORD externalWritten;
    if (IS_VALID_HANDLE(external)) { WriteFile(external, "changed\r\n", 9, &externalWritten, NULL); CloseHandle(external); }
    _WorkspaceSwitch(2);
    _WorkspaceTestCheck(&result, IsFileChangedFlagSet() && _WorkspaceTestText("gamma\r\n"), "inactive external modification detected without replacing buffer");
    external = Path_CreateFile(c, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (IS_VALID_HANDLE(external)) { WriteFile(external, "gamma\r\n", 7, &externalWritten, NULL); CloseHandle(external); }
    ResetFileObservationData(true);
    _WorkspaceTestCheck(&result, _WorkspaceCloseActive() && workspace->count == 2, "close third document");
    Path_Release(c);
    _WorkspaceSwitch(1);
    SendWMCommand(Globals.hwndMain, IDM_FILE_CLOSE_TAB);
    _WorkspaceTestCheck(&result, workspace->count == 1, "close-tab command routed");
    _WorkspaceTestCheck(&result, workspace->count == 1 && _WorkspaceTestText("alpha\r\nA"), "closing B preserves A");
    HPATHL folded = Path_Allocate(folder); Path_Append(folded, L"fold.json");
    _WorkspaceTestCheck(&result, FileLoad(folded, FLF_None, 0, 0), "load folding fixture");
    SciCall_SetProperty("fold", "1"); Sci_ColouriseAll();
    _WorkspaceTestCheck(&result, (SciCall_GetFoldLevel(0) & SC_FOLDLEVELHEADERFLAG) != 0, "lexer produces folding header");
    SciCall_FoldLine(0, SC_FOLDACTION_CONTRACT);
    _WorkspaceTestCheck(&result, !SciCall_GetFoldExpanded(0) && !SciCall_GetLineVisible(1), "collapse fixture");
    _WorkspaceSwitch(0); _WorkspaceSwitch(1);
    _WorkspaceTestCheck(&result, !SciCall_GetFoldExpanded(0) && !SciCall_GetLineVisible(1), "fold state restored after tab switch");
    SciCall_FoldLine(0, SC_FOLDACTION_EXPAND);
    _WorkspaceSwitch(0); _WorkspaceSwitch(1);
    _WorkspaceTestCheck(&result, SciCall_GetFoldExpanded(0) && SciCall_GetLineVisible(1), "expanded state remains expanded");
    _WorkspaceCloseActive(); Path_Release(folded);
    HPATHL sessionFile = Path_Allocate(folder); Path_Append(sessionFile, L"workspace.np3session");
    uint32_t sessionError = 0;
    sptr_t const originalHandle = SciCall_GetDocPointer();
    SciCall_AppendText(1, "~");
    _WorkspaceTestCheck(&result, _WorkspaceSessionSave(sessionFile, &sessionError), "capture unsaved workspace session");
    _WorkspaceTestCheck(&result, SciCall_GetDocPointer() == originalHandle && _WorkspaceTestText("alpha\r\nA~"), "session capture keeps active document unchanged");
    SciCall_Undo();
    _WorkspaceTestCheck(&result, _WorkspaceTestText("alpha\r\nA") && !IsSaveNeeded(), "session capture keeps undo and savepoint");
    _WorkspaceTestCheck(&result, _WorkspaceSessionLoad(sessionFile, &sessionError), "restore protected workspace session");
    _WorkspaceTestCheck(&result, workspace->count == 2 && _WorkspaceTestText("alpha\r\nA~") && Encoding_GetCurrent() == CPI_UTF8SIGN && IsSaveNeeded(), "restored unsaved content and encoding");
    _WorkspaceSwitch(0);
    _WorkspaceTestCheck(&result, SciCall_GetDocPointer() == originalHandle && _WorkspaceTestText("alpha\r\nA"), "import retains original tab");
    _WorkspaceReleaseDocument(&workspace->documents[1]); workspace->count = 1; _WorkspaceRefresh();
    HANDLE brokenSession = Path_CreateFile(sessionFile, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    DWORD corruptWritten;
    if (IS_VALID_HANDLE(brokenSession)) { WriteFile(brokenSession, "broken", 6, &corruptWritten, NULL); CloseHandle(brokenSession); }
    _WorkspaceTestCheck(&result, !_WorkspaceSessionLoad(sessionFile, &sessionError) && workspace->count == 1 && SciCall_GetDocPointer() == originalHandle && _WorkspaceTestText("alpha\r\nA"), "corrupt session preserves complete current workspace");
    Path_Release(sessionFile);
    HPATHL exported = Path_Allocate(folder); Path_Append(exported, L"buffer-export.html");
    const char* exportHeading = "\r\n# 导出验证\r\n";
    SciCall_AppendText(strlen(exportHeading), exportHeading);
    unsigned long long const exportVersion = workspace->documents[workspace->active].version;
    _WorkspaceTestCheck(&result, _MarkdownExportSave(exported, &sessionError) && Path_IsExistingFile(exported), "export current unsaved buffer as Markdown HTML");
    _WorkspaceTestCheck(&result, SciCall_GetDocPointer() == originalHandle && workspace->documents[workspace->active].version == exportVersion && IsSaveNeeded() && SciCall_CanUndo(), "export preserves document version dirty state and undo");
    _WorkspaceTestCheck(&result, !_MarkdownExportSave(a, &sessionError) && sessionError == ERROR_SHARING_VIOLATION, "export cannot overwrite an open source document");
    SciCall_Undo();
    _WorkspaceTestCheck(&result, _WorkspaceTestText("alpha\r\nA") && !IsSaveNeeded(), "export buffer change remains one undoable edit");
    Path_Release(exported);
    const char* invalidJson = "{\r\n\"键\": 1,\r\n}";
    SciCall_SelectAll(); SciCall_ReplaceSel(invalidJson);
    unsigned long long const checkVersion = workspace->documents[workspace->active].version;
    SciCall_SetReadOnly(true);
    _WorkspaceTestCheck(&result, _ConfigCheck(CONFIG_JSON, false) == 1 && SciCall_LineFromPosition(SciCall_GetSelectionStart()) == 2, "read-only JSON check locates actual syntax error");
    _WorkspaceTestCheck(&result, _WorkspaceTestText(invalidJson) && workspace->documents[workspace->active].version == checkVersion, "configuration check retains text and document version");
    SciCall_SetReadOnly(false);
    _WorkspaceTestCheck(&result, SciCall_CanUndo(), "configuration check retains undo history after unlocking");
    SciCall_Undo();
    _WorkspaceTestCheck(&result, _WorkspaceTestText("alpha\r\nA") && !IsSaveNeeded(), "configuration check creates no extra undo edit");
    const char* invalidIni = "[section]\r\nmissing-equals";
    SciCall_SelectAll(); SciCall_ReplaceSel(invalidIni);
    _WorkspaceTestCheck(&result, _ConfigCheck(CONFIG_INI, false) == 1 && SciCall_GetSelectionStart() == 11 && SciCall_GetColumn(SciCall_GetSelectionStart()) == 0, "INI check locates error in editor byte coordinates");
    SciCall_Undo();
    SciCall_SelectAll(); SciCall_ReplaceSel("[section]\r\nkey=value");
    _WorkspaceTestCheck(&result, _ConfigCheck(CONFIG_INI, false) == 0, "valid INI structure accepted in actual editor");
    SciCall_Undo();
    const char* jsonCommandInput="prefix {\"x\":1,\"s\":\"中文\"} suffix";
    const char* jsonCommandPretty="prefix {\r\n  \"x\": 1,\r\n  \"s\": \"中文\"\r\n} suffix";
    SciCall_SelectAll(); SciCall_ReplaceSel(jsonCommandInput); SciCall_SetSel(7,(DocPos)strlen(jsonCommandInput)-7);
    bool jsonPreview=GetEnvironmentVariableW(L"NP3_MARKDOWN_CHECK_PREVIEW",NULL,0)!=0;
    if(jsonPreview) {
        MarkdownPreview_Toggle(Globals.hwndMain);
        _WorkspaceTestCheck(&result,_WorkspaceTestPreviewReady(false),"JSON command fixture starts actual preview");
        HWND previewGrip=FindWindowExW(Globals.hwndMain,NULL,L"NP3MarkdownPreview",NULL);
        RECT previewBefore={0},previewAfter={0}; GetWindowRect(previewGrip,&previewBefore);
        unsigned long long previewDragVersion=_WorkspaceVersion(); int previewDragUndo=SciCall_GetUndoCurrent(); bool previewDragDirty=IsSaveNeeded();
        SendMessageW(previewGrip,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(2,2));
        SendMessageW(previewGrip,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(-100,2)); GetWindowRect(previewGrip,&previewAfter);
        _WorkspaceTestCheck(&result,previewGrip && GetCapture()==previewGrip && previewAfter.right-previewAfter.left>previewBefore.right-previewBefore.left,"Markdown grip message coordinates widen actual preview without moving global cursor");
        SendMessageW(previewGrip,WM_CANCELMODE,0,0); GetWindowRect(previewGrip,&previewAfter);
        _WorkspaceTestCheck(&result,GetCapture()!=previewGrip && EqualRect(&previewBefore,&previewAfter),"canceling Markdown drag restores initial bounds and releases capture");
        SendMessageW(previewGrip,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(2,2));
        SendMessageW(previewGrip,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(-100,2)); ReleaseCapture(); GetWindowRect(previewGrip,&previewAfter);
        _WorkspaceTestCheck(&result,EqualRect(&previewBefore,&previewAfter),"losing Markdown drag capture restores initial bounds");
        SendMessageW(previewGrip,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(2,2));
        SendMessageW(previewGrip,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(-100,2));
        SendMessageW(previewGrip,WM_LBUTTONUP,0,0); GetWindowRect(previewGrip,&previewAfter);
        _WorkspaceTestCheck(&result,GetCapture()!=previewGrip && previewAfter.right-previewAfter.left>previewBefore.right-previewBefore.left,"releasing Markdown drag commits new width");
        SendMessageW(previewGrip,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(2,2));
        SendMessageW(previewGrip,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(-30000,2)); GetWindowRect(previewGrip,&previewAfter);
        RECT previewMain={0}; GetClientRect(Globals.hwndMain,&previewMain);
        _WorkspaceTestCheck(&result,previewAfter.right-previewAfter.left>=160 && previewAfter.right-previewAfter.left<=previewMain.right-160,"extreme Markdown drag retains minimum preview and editor widths");
        SendMessageW(previewGrip,WM_CANCELMODE,0,0);
        RECT previewCommitted={0},previewMainBounds={0}; GetWindowRect(previewGrip,&previewCommitted); GetWindowRect(Globals.hwndMain,&previewMainBounds);
        int previewZeroWidth=MarkdownPreview_Layout(Globals.hwndMain,7,13,0,0); GetWindowRect(previewGrip,&previewAfter);
        _WorkspaceTestCheck(&result,previewZeroWidth==0 && previewAfter.right==previewAfter.left && previewAfter.bottom==previewAfter.top,"actual Markdown host tolerates zero available size");
        int previewNarrowWidth=MarkdownPreview_Layout(Globals.hwndMain,7,13,100,50); GetWindowRect(previewGrip,&previewAfter);
        MapWindowPoints(NULL,Globals.hwndMain,(POINT*)&previewAfter,2);
        _WorkspaceTestCheck(&result,previewNarrowWidth==33 && previewAfter.left==74 && previewAfter.right==107 && previewAfter.top==13 && previewAfter.bottom==63,"actual narrow Markdown host stays inside offset layout bounds");
        SetWindowPos(Globals.hwndMain,NULL,0,0,480,320,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
        RECT previewResizedMain={0},previewResizedEditor={0}; GetClientRect(Globals.hwndMain,&previewResizedMain);
        GetWindowRect(previewGrip,&previewAfter); MapWindowPoints(NULL,Globals.hwndMain,(POINT*)&previewAfter,2);
        GetWindowRect(Globals.hwndEdit,&previewResizedEditor); MapWindowPoints(NULL,Globals.hwndMain,(POINT*)&previewResizedEditor,2);
        _WorkspaceTestCheck(&result,previewAfter.left>=0 && previewAfter.right<=previewResizedMain.right && previewAfter.top>=0 && previewAfter.bottom<=previewResizedMain.bottom && previewResizedEditor.right<=previewAfter.left && previewResizedEditor.right>previewResizedEditor.left,"real main window resize keeps editor and Markdown host within client bounds without overlap");
        SetWindowPos(Globals.hwndMain,NULL,0,0,previewMainBounds.right-previewMainBounds.left,previewMainBounds.bottom-previewMainBounds.top,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
        GetWindowRect(previewGrip,&previewAfter);
        _WorkspaceTestCheck(&result,EqualRect(&previewCommitted,&previewAfter),"restoring actual main window size retains committed Markdown width ratio");
        _WorkspaceTestCheck(&result,_WorkspaceVersion()==previewDragVersion && SciCall_GetUndoCurrent()==previewDragUndo && _WorkspaceTestText(jsonCommandInput) && IsSaveNeeded()==previewDragDirty,"Markdown drag cancel capture loss and commit preserve source version undo and savepoint");
    }
    unsigned long long jsonVersion=workspace->documents[workspace->active].version;
    SendMessageW(Globals.hwndMain,WM_COMMAND,IDM_EDIT_JSON_FORMAT,0);
    _WorkspaceTestCheck(&result,_WorkspaceTestText(jsonCommandPretty) && SciCall_GetSelectionStart()==7 && SciCall_GetSelectionEnd()==(DocPos)strlen(jsonCommandPretty)-7 && workspace->documents[workspace->active].version==jsonVersion+1,"JSON format command changes only selected Unicode JSON and advances version once");
    if(jsonPreview) _WorkspaceTestCheck(&result,!MarkdownPreview_IsRendered(Globals.hwndMain) && _WorkspaceTestPreviewReady(false),"JSON format quiet transaction refreshes actual WebView preview");
    jsonVersion=workspace->documents[workspace->active].version;
    SendMessageW(Globals.hwndMain,WM_COMMAND,IDM_EDIT_JSON_COMPACT,0);
    _WorkspaceTestCheck(&result,_WorkspaceTestText(jsonCommandInput) && workspace->documents[workspace->active].version==jsonVersion+1,"JSON compact command restores selected bytes and advances version once");
    if(jsonPreview) _WorkspaceTestCheck(&result,!MarkdownPreview_IsRendered(Globals.hwndMain) && _WorkspaceTestPreviewReady(false),"JSON compact quiet transaction refreshes actual WebView preview");
    jsonVersion=workspace->documents[workspace->active].version; int jsonUndoCurrent=SciCall_GetUndoCurrent();
    SendMessageW(Globals.hwndMain,WM_COMMAND,IDM_EDIT_JSON_COMPACT,0);
    _WorkspaceTestCheck(&result,_WorkspaceTestText(jsonCommandInput) && workspace->documents[workspace->active].version==jsonVersion && SciCall_GetUndoCurrent()==jsonUndoCurrent,"idempotent JSON compact leaves version and undo unchanged");
    SciCall_SetReadOnly(TRUE); SendMessageW(Globals.hwndMain,WM_COMMAND,IDM_EDIT_JSON_FORMAT,0);
    _WorkspaceTestCheck(&result,_WorkspaceTestText(jsonCommandInput) && workspace->documents[workspace->active].version==jsonVersion,"JSON command respects editor read-only state"); SciCall_SetReadOnly(FALSE);
    SciCall_Undo(); _WorkspaceTestCheck(&result,_WorkspaceTestText(jsonCommandPretty),"JSON compact command has one independent undo");
    SciCall_Undo(); _WorkspaceTestCheck(&result,_WorkspaceTestText(jsonCommandInput),"JSON format command has one independent undo");
    if(jsonPreview) MarkdownPreview_Toggle(Globals.hwndMain);
    SciCall_Undo(); _WorkspaceTestCheck(&result,_WorkspaceTestText("alpha\r\nA") && !IsSaveNeeded(),"JSON command fixture restores original saved document");
    bool quietPreview=GetEnvironmentVariableW(L"NP3_MARKDOWN_CHECK_PREVIEW",NULL,0)!=0;
    if(quietPreview) { MarkdownPreview_Toggle(Globals.hwndMain); _WorkspaceTestCheck(&result,_WorkspaceTestPreviewReady(false),"generic quiet command fixture starts actual preview"); }
    unsigned long long quietVersion=_WorkspaceVersion();
    AI_PANEL_SOURCE outsideSource={workspace->documents[workspace->active].id,quietVersion,7,8,7,8};
    AI_PANEL_APPLY outsideApply={outsideSource,"A",1,"Z",1};
    SciCall_SetSel(0,5); SendWMCommand(Globals.hwndMain,IDM_EDIT_INVERTCASE);
    _WorkspaceTestCheck(&result,_WorkspaceTestText("ALPHA\r\nA") && _WorkspaceVersion()==quietVersion+1,"native invert-case quiet transaction advances document version once");
    if(quietPreview) _WorkspaceTestCheck(&result,!MarkdownPreview_IsRendered(Globals.hwndMain) && _WorkspaceTestPreviewReady(false),"generic quiet edit command refreshes actual WebView preview");
    SciCall_SetSel(7,8); bool outsideApplied=_AiApply(&outsideApply);
    _WorkspaceTestCheck(&result,!outsideApplied && _WorkspaceTestText("ALPHA\r\nA"),"quiet change outside AI selected range invalidates old proposal");
    if(outsideApplied) SciCall_Undo();
    SciCall_Undo(); _WorkspaceTestCheck(&result,_WorkspaceTestText("alpha\r\nA") && !IsSaveNeeded(),"generic quiet command retains one independent undo and original savepoint");
    quietVersion=_WorkspaceVersion(); int quietUndo=SciCall_GetUndoCurrent();
    LONG quietOuter=BeginUndoActionSelection(); SciCall_AppendText(1,"!");
    LONG quietInner=BeginUndoActionSelection(); SciCall_AppendText(1,"?"); EndUndoActionSelection(quietInner);
    _WorkspaceTestCheck(&result,_WorkspaceVersion()==quietVersion,"nested quiet edit defers version notification to outer completion");
    EndUndoActionSelection(quietOuter);
    _WorkspaceTestCheck(&result,_WorkspaceVersion()==quietVersion+1 && _WorkspaceTestText("alpha\r\nA!?"),"nested quiet transaction advances version once for complete edit");
    SciCall_Undo(); _WorkspaceTestCheck(&result,_WorkspaceTestText("alpha\r\nA") && !IsSaveNeeded(),"nested quiet transaction remains one undoable action");
    quietVersion=_WorkspaceVersion(); quietUndo=SciCall_GetUndoCurrent();
    quietOuter=BeginUndoActionSelection(); EndUndoActionSelection(quietOuter);
    _WorkspaceTestCheck(&result,_WorkspaceVersion()==quietVersion && SciCall_GetUndoCurrent()==quietUndo,"empty quiet transaction retains version and undo cursor");
    SciCall_SetSel(7,8); SciCall_SetReadOnly(TRUE); quietOuter=BeginUndoActionSelection(); bool deniedQuietAi=!_AiApply(&outsideApply); EndUndoActionSelection(quietOuter); SciCall_SetReadOnly(FALSE);
    _WorkspaceTestCheck(&result,deniedQuietAi && _WorkspaceVersion()==quietVersion && SciCall_GetUndoCurrent()==quietUndo && _WorkspaceTestText("alpha\r\nA"),"read-only quiet proposal cannot alter version text or undo");
    int oldChangeHistory=SciCall_GetChangeHistory(); SciCall_SetChangeHistory(SC_CHANGE_HISTORY_DISABLED); SciCall_SetSel(0,5);
    AI_PANEL_SOURCE noopSource={workspace->documents[workspace->active].id,quietVersion,0,5,0,5}; AI_PANEL_APPLY noopApply={noopSource,"alpha",5,"alpha",5};
    _WorkspaceTestCheck(&result,_AiApply(&noopApply) && _WorkspaceVersion()==quietVersion && SciCall_GetUndoCurrent()==quietUndo && !IsSaveNeeded(),"identical AI result creates no edit even with change history disabled");
    if(SciCall_GetUndoCurrent()!=quietUndo) SciCall_Undo();
    quietVersion=_WorkspaceVersion(); quietUndo=SciCall_GetUndoCurrent();
    BATCH_CHANGE noopChange={workspace->documents[workspace->active].id,quietVersion,"alpha\r\nA",8,"alpha\r\nA",8}; BATCH_APPLY noopBatch={&noopChange,1};
    _WorkspaceTestCheck(&result,_BatchApply(&noopBatch)==1 && _WorkspaceVersion()==quietVersion && SciCall_GetUndoCurrent()==quietUndo && !IsSaveNeeded(),"identical batch result creates no edit with change history disabled");
    if(SciCall_GetUndoCurrent()!=quietUndo) SciCall_Undo();
    SciCall_SetChangeHistory(oldChangeHistory);
    if(quietPreview) MarkdownPreview_Toggle(Globals.hwndMain);
    SciCall_SetSel(1,3); SciCall_AddSelection(5,4);
    unsigned long long singleDpiVersion=_WorkspaceVersion();
    int singleDpiUndo=SciCall_GetUndoCurrent(); RECT singleDpiRect; GetWindowRect(Globals.hwndMain,&singleDpiRect);
    UINT singleDpi=GetDpiForWindow(Globals.hwndMain);
    SendMessageW(Globals.hwndMain,WM_DPICHANGED,MAKEWPARAM(singleDpi,singleDpi),(LPARAM)&singleDpiRect);
    _WorkspaceTestCheck(&result,SciCall_GetSelections()==2 && SciCall_GetSelectionNStart(0)==1 && SciCall_GetSelectionNEnd(0)==3 && SciCall_GetSelectionNStart(1)==4 && SciCall_GetSelectionNEnd(1)==5,"DPI change handler preserves multiple selections in unsplit editor");
    _WorkspaceTestCheck(&result,_WorkspaceVersion()==singleDpiVersion && SciCall_GetUndoCurrent()==singleDpiUndo && _WorkspaceTestText("alpha\r\nA") && !IsSaveNeeded(),"DPI change handler preserves unsplit text version undo and savepoint");
    SciCall_ClearSelections(); SciCall_SetSel(0,0);
    HPATHL closeFixture=Path_Copy(a); Path_RemoveFileSpec(closeFixture); Path_Append(closeFixture,L"close-flow.txt");
    HANDLE closeFile=Path_CreateFile(closeFixture,GENERIC_WRITE,FILE_SHARE_READ,NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);
    DWORD closeWritten=0;
    bool closeCreated=IS_VALID_HANDLE(closeFile) && WriteFile(closeFile,"close fixture\r\n",15,&closeWritten,NULL) && closeWritten==15;
    if(IS_VALID_HANDLE(closeFile)) CloseHandle(closeFile);
    bool closeLoaded=closeCreated && FileLoad(closeFixture,FLF_None,0,0);
    _WorkspaceTestCheck(&result,closeLoaded && workspace->count==2 && _WorkspaceTestText("close fixture\r\n"),"close dialog fixture opens in independent saved tab");
    if(closeLoaded) {
        Encoding_Current(CPI_UTF8); SciCall_AppendText(5,"draft"); SciCall_SetSel(3,8); SciCall_SetZoom(3);
        unsigned long long closeId=workspace->documents[workspace->active].id, closeVersion=_WorkspaceVersion();
        sptr_t closeHandle=SciCall_GetDocPointer(); int closeUndo=SciCall_GetUndoCurrent();
        bool closeAnswered=_WorkspaceTestCloseChoice(IDCANCEL);
        _WorkspaceTestCheck(&result,closeAnswered && workspace->count==2 && workspace->documents[workspace->active].id==closeId && SciCall_GetDocPointer()==closeHandle && _WorkspaceVersion()==closeVersion && SciCall_GetUndoCurrent()==closeUndo && _WorkspaceTestText("close fixture\r\ndraft") && IsSaveNeeded() && SciCall_GetSelectionStart()==3 && SciCall_GetSelectionEnd()==8 && SciCall_GetZoom()==3,"actual Cancel button preserves dirty tab identity text selection zoom version and undo");
        closeAnswered=_WorkspaceTestCloseChoice(IDNO);
        _WorkspaceTestCheck(&result,closeAnswered && workspace->count==1 && _WorkspaceTestText("alpha\r\nA") && !IsSaveNeeded(),"actual Discard button closes only dirty fixture tab");
        char closeDisk[32]={0}; DWORD closeRead=0;
        closeFile=Path_CreateFile(closeFixture,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);
        bool closeUnchanged=IS_VALID_HANDLE(closeFile) && ReadFile(closeFile,closeDisk,sizeof(closeDisk),&closeRead,NULL) && closeRead==15 && !memcmp(closeDisk,"close fixture\r\n",15);
        if(IS_VALID_HANDLE(closeFile)) CloseHandle(closeFile);
        _WorkspaceTestCheck(&result,closeUnchanged,"discard leaves original file bytes unchanged on disk");
        if(workspace->count==2) { SciCall_SetSavePoint(); _WorkspaceCloseActive(); }
        closeLoaded=FileLoad(closeFixture,FLF_None,0,0);
        _WorkspaceTestCheck(&result,closeLoaded && workspace->count==2 && workspace->documents[workspace->active].id!=closeId && _WorkspaceTestText("close fixture\r\n"),"discarded fixture reopens original bytes with fresh document identity");
        if(closeLoaded) {
            Encoding_Current(CPI_UTF8); SciCall_AppendText(5,"saved");
            closeAnswered=_WorkspaceTestCloseChoice(IDYES);
            _WorkspaceTestCheck(&result,closeAnswered && workspace->count==1 && _WorkspaceTestText("alpha\r\nA") && !IsSaveNeeded(),"actual Save button writes and closes fixture while preserving original tab");
            if(workspace->count==2) { SciCall_SetSavePoint(); _WorkspaceCloseActive(); }
            closeLoaded=FileLoad(closeFixture,FLF_None,0,0);
            _WorkspaceTestCheck(&result,closeLoaded && _WorkspaceTestText("close fixture\r\nsaved") && !IsSaveNeeded(),"saved close fixture reopens complete committed content");
            if(closeLoaded) _WorkspaceCloseActive();
        }
    }
    if(closeCreated && workspace->count==1){
        unsigned long long remainingId=workspace->documents[0].id;
        closeLoaded=FileLoad(closeFixture,FLF_None,0,0);
        RECT tabRect;TabCtrl_GetItemRect(workspace->tabs,workspace->active,&tabRect);
        SendMessageW(workspace->tabs,WM_LBUTTONDBLCLK,MK_LBUTTON,MAKELPARAM(tabRect.left+8,(tabRect.top+tabRect.bottom)/2));
        _WorkspaceTestCheck(&result,closeLoaded && workspace->count==1 && workspace->documents[0].id==remainingId,"double click closes target saved tab and retains other document");
        closeLoaded=FileLoad(closeFixture,FLF_None,0,0);_WorkspaceSwitch(0);
        RECT closeRect=_WorkspaceTabCloseRect(workspace->tabs,1);
        SendMessageW(workspace->tabs,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM((closeRect.left+closeRect.right)/2,(closeRect.top+closeRect.bottom)/2));
        _WorkspaceTestCheck(&result,closeLoaded && workspace->count==1 && workspace->documents[0].id==remainingId,"per tab close glyph closes inactive target without closing original document");
        closeLoaded=FileLoad(closeFixture,FLF_None,0,0);SciCall_AppendText(5,"dirty");
        workspaceCloseTestChoice=IDCANCEL;workspaceCloseTestClicks=0;workspaceCloseTestTimer=SetTimer(NULL,0,25,_WorkspaceTestCloseTimer);
        TabCtrl_GetItemRect(workspace->tabs,workspace->active,&tabRect);
        if(workspaceCloseTestTimer)SendMessageW(workspace->tabs,WM_LBUTTONDBLCLK,MK_LBUTTON,MAKELPARAM(tabRect.left+8,(tabRect.top+tabRect.bottom)/2));
        if(workspaceCloseTestTimer){KillTimer(NULL,workspaceCloseTestTimer);workspaceCloseTestTimer=0;}
        _WorkspaceTestCheck(&result,closeLoaded && workspaceCloseTestClicks==1 && workspace->count==2 && IsSaveNeeded() && _WorkspaceTestText("close fixture\r\nsaveddirty"),"double click dirty tab honors actual Cancel and retains unsaved text");
        SciCall_SetSavePoint();_WorkspaceCloseActive();
    }
    Path_Release(closeFixture);
    _WorkspaceTestCheck(&result, _WorkspaceCanClose(FSF_Ask), "all saved tabs can close");
    _SplitToggle();
    HWND primaryView = workspace->primaryView, secondView = workspace->splitView;
    _WorkspaceTestCheck(&result, secondView && Sci_ViewCall(secondView, SCI_GETDOCPOINTER, 0, 0) == originalHandle, "split view shares actual Scintilla document");
    _SplitLayout(0, 0, 800, 400);
    int initialRatio = workspace->splitRatio;
    SendMessageW(workspace->splitGutter, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(2, 2));
    SendMessageW(workspace->splitGutter, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(100, 2));
    _WorkspaceTestCheck(&result, workspace->splitRatio > initialRatio && workspace->splitDragging, "split gutter drag changes view width ratio");
    SendMessageW(workspace->splitGutter, WM_CANCELMODE, 0, 0);
    _WorkspaceTestCheck(&result, workspace->splitRatio == initialRatio && !workspace->splitDragging && GetCapture() != workspace->splitGutter, "canceling split drag restores ratio and releases capture");
    _SplitLayout(0, 0, 800, 400);
    SendMessageW(workspace->splitGutter, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(2, 2));
    SendMessageW(workspace->splitGutter, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(-30000, 2));
    SendMessageW(workspace->splitGutter, WM_LBUTTONUP, 0, 0);
    _WorkspaceTestCheck(&result, workspace->splitRatio > 0 && workspace->splitRatio < initialRatio && !workspace->splitDragging, "split drag clamps extreme positions to minimum view width");
    workspace->splitRatio = initialRatio;
    _WorkspaceTestCheck(&result, _SplitLayout(0, 0, 0, 0) == 0 && _SplitLayout(0, 0, 100, 20) <= 100, "split layout tolerates zero and narrow editor space");
    _SplitRelayout();
    SciCall_SetSel(0, 0); Sci_ViewCall(secondView, SCI_SETSEL, 3, 3);
    _WorkspaceTestCheck(&result, SciCall_GetCurrentPos() == 0 && Sci_ViewCall(secondView, SCI_GETCURRENTPOS, 0, 0) == 3, "split selections remain independent");
    SendMessageW(secondView, WM_SETFOCUS, 0, 0);
    _WorkspaceTestCheck(&result, Globals.hwndEdit == secondView && g_hwndEditWindow == secondView, "split focus routes editor commands to second view");
    unsigned long long beforeSplitEdit = workspace->documents[workspace->active].version;
    SciCall_AppendText(1, "+");
    _WorkspaceTestCheck(&result, Sci_ViewCall(primaryView, SCI_GETLENGTH, 0, 0) == 9 && IsSaveNeeded(), "second view edit updates shared primary content and dirty state");
    _WorkspaceTestCheck(&result, workspace->documents[workspace->active].version == beforeSplitEdit + 1, "shared document edit produces one version increment");
    SendMessageW(primaryView, WM_SETFOCUS, 0, 0); SciCall_Undo();
    _WorkspaceTestCheck(&result, _WorkspaceTestText("alpha\r\nA") && !IsSaveNeeded() && Sci_ViewCall(secondView, SCI_GETLENGTH, 0, 0) == 8, "primary undo reverses second view edit in shared history");
    SciCall_SetReadOnly(true); _SplitSync();
    _WorkspaceTestCheck(&result, Sci_ViewCall(secondView, SCI_GETREADONLY, 0, 0), "split read-only state protects both views");
    SciCall_SetReadOnly(false); _SplitSync();
    SciCall_SetSel(1, 2); Sci_ViewCall(secondView, SCI_SETSEL, 4, 6);
    SciCall_SetZoom(1); Sci_ViewCall(secondView, SCI_SETZOOM, 3, 0);
    SendMessageW(secondView, WM_SETFOCUS, 0, 0);
    SciCall_AppendText(1, "+");
    _WorkspaceTestCheck(&result, FileSave(FSF_None) && !IsSaveNeeded(), "saving edited right view writes its active source document");
    SciCall_Undo(); FileSave(FSF_None);
    _WorkspaceTestCheck(&result, _WorkspaceTestText("alpha\r\nA") && !IsSaveNeeded(), "undo and resave from right view restores original saved bytes");
    Sci_ViewCall(primaryView, SCI_SETSEL, 1, 2); SciCall_SetSel(4, 6);
    _WorkspaceTestCheck(&result, FileLoad(b, FLF_DontSave, 0, 0) && _WorkspaceTestText("beta\r\nB") && Sci_ViewCall(primaryView, SCI_GETDOCPOINTER, 0, 0) == SciCall_GetDocPointer(), "opening another tab while split focused updates both views");
    _WorkspaceSwitch(0);
    _WorkspaceTestCheck(&result, _WorkspaceTestText("alpha\r\nA") && Sci_ViewCall(primaryView, SCI_GETDOCPOINTER, 0, 0) == originalHandle, "split tab switch restores original shared document");
    _WorkspaceTestCheck(&result, Sci_ViewCall(primaryView, SCI_GETANCHOR, 0, 0) == 1 && Sci_ViewCall(primaryView, SCI_GETCURRENTPOS, 0, 0) == 2 && SciCall_GetAnchor() == 4 && SciCall_GetCurrentPos() == 6, "tab return restores each split selection separately");
    _WorkspaceTestCheck(&result, Sci_ViewCall(primaryView, SCI_GETZOOM, 0, 0) == 1 && SciCall_GetZoom() == 3, "tab return restores independent split zoom levels");
    char longSplitText[8192] = {0};
    for (int line = 0; line < 180; ++line) StringCchCatA(longSplitText, COUNTOF(longSplitText), "\r\n0123456789012345678901234567890123456789");
    SciCall_AppendText(strlen(longSplitText), longSplitText);
    Sci_ViewCall(primaryView, SCI_SETWRAPMODE, SC_WRAP_NONE, 0); SciCall_SetWrapMode(SC_WRAP_NONE);
    Sci_ViewCall(primaryView, SCI_SETFIRSTVISIBLELINE, 10, 0); SciCall_SetFirstVisibleLine(70);
    DocLn leftScroll = Sci_ViewCall(primaryView, SCI_GETFIRSTVISIBLELINE, 0, 0), rightScroll = SciCall_GetFirstVisibleLine();
    _WorkspaceSwitch(1); _WorkspaceSwitch(0);
    _WorkspaceTestCheck(&result, leftScroll != rightScroll && Sci_ViewCall(primaryView, SCI_GETFIRSTVISIBLELINE, 0, 0) == leftScroll && SciCall_GetFirstVisibleLine() == rightScroll, "long document restores independent split scroll positions");
    uint64_t const styleVersion = workspace->documents[workspace->active].version;
    SciCall(SCI_STYLESETFORE, 100, RGB(12,34,56));
    SciCall(SCI_STYLESETBACK, 100, RGB(65,43,21));
    SciCall(SCI_STYLESETUNDERLINE, 100, TRUE);
    SciCall(SCI_STYLESETSIZEFRACTIONAL, 100, 1350);
    SciCall(SCI_SETELEMENTCOLOUR, SC_ELEMENT_CARET, 0xff123456);
    SciCall(SCI_INDICSETSTYLE, INDIC_MAX, INDIC_ROUNDBOX); SciCall(SCI_INDICSETFORE, INDIC_MAX, RGB(30,60,90));
    SciCall(SCI_INDICSETALPHA, INDIC_MAX, 75); SciCall(SCI_INDICSETOUTLINEALPHA, INDIC_MAX, 120);
    SciCall(SCI_INDICSETSTROKEWIDTH, INDIC_MAX, 175);
    SciCall(SCI_MARKERDEFINE, 6, SC_MARK_CIRCLE); SciCall(SCI_MARKERSETFORETRANSLUCENT, 6, 0xcc123456);
    SciCall(SCI_MARKERSETBACKTRANSLUCENT, 6, 0xdd654321); SciCall(SCI_MARKERSETBACKSELECTEDTRANSLUCENT, 6, 0xaa456789);
    SciCall(SCI_MARKERSETALPHA, 6, 127); SciCall(SCI_MARKERSETLAYER, 6, SC_LAYER_UNDER_TEXT); SciCall(SCI_MARKERSETSTROKEWIDTH, 6, 225);
    SciCall(SCI_SETFOLDMARGINCOLOUR, TRUE, RGB(33,44,55)); SciCall(SCI_SETFOLDMARGINHICOLOUR, TRUE, RGB(66,77,88));
    SciCall(SCI_SETMARGINBACKN, 1, RGB(90,80,70));

    SendMessageW(Globals.hwndMain, NP3_WM_EDITOR_STYLE_CHANGED, (WPARAM)secondView, 0);
    _WorkspaceTestCheck(&result, Sci_ViewCall(primaryView, SCI_STYLEGETFORE, 100, 0) == RGB(12,34,56) && Sci_ViewCall(primaryView, SCI_STYLEGETBACK, 100, 0) == RGB(65,43,21) && Sci_ViewCall(primaryView, SCI_STYLEGETUNDERLINE, 100, 0) && Sci_ViewCall(primaryView, SCI_STYLEGETSIZEFRACTIONAL, 100, 0) == 1350, "style refresh copies colors underline and fractional size to primary");
    _WorkspaceTestCheck(&result, Sci_ViewCall(primaryView, SCI_GETELEMENTCOLOUR, SC_ELEMENT_CARET, 0) == SciCall(SCI_GETELEMENTCOLOUR, SC_ELEMENT_CARET, 0), "style refresh copies explicit caret element color");
    _WorkspaceTestCheck(&result, Sci_ViewCall(primaryView, SCI_INDICGETSTYLE, INDIC_MAX, 0) == INDIC_ROUNDBOX && Sci_ViewCall(primaryView, SCI_INDICGETFORE, INDIC_MAX, 0) == RGB(30,60,90) && Sci_ViewCall(primaryView, SCI_INDICGETALPHA, INDIC_MAX, 0) == 75 && Sci_ViewCall(primaryView, SCI_INDICGETOUTLINEALPHA, INDIC_MAX, 0) == 120 && Sci_ViewCall(primaryView, SCI_INDICGETSTROKEWIDTH, INDIC_MAX, 0) == 175, "split copies indicator color alpha outline style and stroke width");
    _WorkspaceTestCheck(&result, Sci_ViewCall(primaryView, SCI_MARKERSYMBOLDEFINED, 6, 0) == SC_MARK_CIRCLE && Sci_ViewCall(primaryView, SCI_MARKERGETFORETRANSLUCENT, 6, 0) == SciCall(SCI_MARKERGETFORETRANSLUCENT, 6, 0) && Sci_ViewCall(primaryView, SCI_MARKERGETBACKTRANSLUCENT, 6, 0) == SciCall(SCI_MARKERGETBACKTRANSLUCENT, 6, 0) && Sci_ViewCall(primaryView, SCI_MARKERGETBACKSELECTEDTRANSLUCENT, 6, 0) == SciCall(SCI_MARKERGETBACKSELECTEDTRANSLUCENT, 6, 0) && Sci_ViewCall(primaryView, SCI_MARKERGETALPHA, 6, 0) == 127 && Sci_ViewCall(primaryView, SCI_MARKERGETLAYER, 6, 0) == SC_LAYER_UNDER_TEXT && Sci_ViewCall(primaryView, SCI_MARKERGETSTROKEWIDTH, 6, 0) == 225, "split copies native marker symbol translucent colors alpha layer and stroke");
    _WorkspaceTestCheck(&result, Sci_ViewCall(primaryView, SCI_GETFOLDMARGINCOLOUR, 0, 0) == RGB(33,44,55) && Sci_ViewCall(primaryView, SCI_GETFOLDMARGINHICOLOUR, 0, 0) == RGB(66,77,88) && Sci_ViewCall(primaryView, SCI_GETMARGINBACKN, 1, 0) == RGB(90,80,70), "split copies fold and marker margin colors without copying widths");
    SciCall(SCI_SETFOLDMARGINCOLOUR, FALSE, 0); SciCall(SCI_SETFOLDMARGINHICOLOUR, FALSE, 0);
    SciCall(SCI_RESETELEMENTCOLOUR, SC_ELEMENT_CARET, 0);
    SciCall(SCI_RESETELEMENTCOLOUR, SC_ELEMENT_WHITE_SPACE_BACK, 0);
    Sci_ViewCall(primaryView, SCI_SETELEMENTCOLOUR, SC_ELEMENT_WHITE_SPACE_BACK, 0xff654321);
    SendMessageW(Globals.hwndMain, NP3_WM_EDITOR_STYLE_CHANGED, (WPARAM)secondView, 0);
    _WorkspaceTestCheck(&result, Sci_ViewCall(primaryView, SCI_GETFOLDMARGINCOLOUR, 0, 0) == -1 && Sci_ViewCall(primaryView, SCI_GETFOLDMARGINHICOLOUR, 0, 0) == -1 && SciCall(SCI_MARKERGETSTROKEWIDTH, MARKER_MAX+1, 0) == 0, "fold margin override reset and marker getter bounds");
    _WorkspaceTestCheck(&result, !Sci_ViewCall(primaryView, SCI_GETELEMENTISSET, SC_ELEMENT_WHITE_SPACE_BACK, 0) && Sci_ViewCall(primaryView, SCI_GETELEMENTCOLOUR, SC_ELEMENT_CARET, 0) == SciCall(SCI_GETELEMENTCOLOUR, SC_ELEMENT_CARET, 0), "style refresh restores caret default and clears stale optional element overrides");
    _WorkspaceTestCheck(&result, Sci_ViewCall(primaryView, SCI_GETFIRSTVISIBLELINE, 0, 0) == leftScroll && SciCall_GetFirstVisibleLine() == rightScroll && Sci_ViewCall(primaryView, SCI_GETZOOM, 0, 0) == 1 && SciCall_GetZoom() == 3 && workspace->documents[workspace->active].version == styleVersion && SciCall_CanUndo(), "style refresh preserves independent scroll zoom document version and undo");
    SendMessageW(primaryView, WM_SETFOCUS, 0, 0);
    SciCall(SCI_STYLESETFORE, 100, RGB(98,76,54));
    SendMessageW(Globals.hwndMain, NP3_WM_EDITOR_STYLE_CHANGED, (WPARAM)primaryView, 0);
    _WorkspaceTestCheck(&result, Sci_ViewCall(secondView, SCI_STYLEGETFORE, 100, 0) == RGB(98,76,54), "style refresh routes from primary to secondary");
    SendMessageW(secondView, WM_SETFOCUS, 0, 0);
    Style_ResetCurrentLexer(Globals.hwndEdit);
    _WorkspaceTestCheck(&result, Sci_ViewCall(primaryView, SCI_STYLEGETFORE, STYLE_DEFAULT, 0) == SciCall(SCI_STYLEGETFORE, STYLE_DEFAULT, 0) && Sci_ViewCall(primaryView, SCI_STYLEGETBACK, STYLE_DEFAULT, 0) == SciCall(SCI_STYLEGETBACK, STYLE_DEFAULT, 0), "actual lexer reset refreshes both split default styles");
    Sci_ViewCall(primaryView, SCI_SETSEL, 1, 2); SciCall_SetSel(4, 6);
    Sci_ViewCall(primaryView, SCI_SETFIRSTVISIBLELINE, 10, 0); SciCall_SetFirstVisibleLine(70);
    SendMessageW(Globals.hwndMain, WM_THEMECHANGED, 0, 0);
    _WorkspaceTestCheck(&result, Sci_ViewCall(primaryView, SCI_GETFIRSTVISIBLELINE, 0, 0) == 10 && SciCall_GetFirstVisibleLine() == 70 && Sci_ViewCall(primaryView, SCI_GETZOOM, 0, 0) == 1 && SciCall_GetZoom() == 3 && Sci_ViewCall(primaryView, SCI_GETANCHOR, 0, 0) == 1 && Sci_ViewCall(primaryView, SCI_GETCURRENTPOS, 0, 0) == 2 && SciCall_GetAnchor() == 4 && SciCall_GetCurrentPos() == 6, "actual theme message preserves independent split scroll zoom and selections");
    _WorkspaceTestCheck(&result, Sci_ViewCall(primaryView, SCI_MARKERGETFORETRANSLUCENT, MARKER_NP3_BOOKMARK, 0) == SciCall(SCI_MARKERGETFORETRANSLUCENT, MARKER_NP3_BOOKMARK, 0) && Sci_ViewCall(primaryView, SCI_GETFOLDMARGINCOLOUR, 0, 0) == SciCall(SCI_GETFOLDMARGINCOLOUR, 0, 0) && workspace->documents[workspace->active].version == styleVersion && SciCall_CanUndo(), "actual theme message synchronizes bookmark fold palette and preserves document version undo");
    RECT dpiRect; GetWindowRect(Globals.hwndMain,&dpiRect);
    UINT testDpi=GetDpiForWindow(Globals.hwndMain);
    int primaryDpiMessages=0,secondaryDpiMessages=0;
    bool primaryDpiHook=SetWindowSubclass(primaryView,_WorkspaceTestDpiMessage,1,(DWORD_PTR)&primaryDpiMessages)!=FALSE;
    bool secondaryDpiHook=SetWindowSubclass(secondView,_WorkspaceTestDpiMessage,1,(DWORD_PTR)&secondaryDpiMessages)!=FALSE;
    SendMessageW(Globals.hwndMain,WM_DPICHANGED,MAKEWPARAM(testDpi,testDpi),(LPARAM)&dpiRect);
    if(primaryDpiHook) RemoveWindowSubclass(primaryView,_WorkspaceTestDpiMessage,1);
    if(secondaryDpiHook) RemoveWindowSubclass(secondView,_WorkspaceTestDpiMessage,1);
    _WorkspaceTestCheck(&result,primaryDpiHook && secondaryDpiHook && primaryDpiMessages>=1 && secondaryDpiMessages>=1,"DPI notification is delivered to both real Scintilla views independently of focus");
    _WorkspaceTestCheck(&result,Sci_ViewCall(primaryView,SCI_GETANCHOR,0,0)==1 && Sci_ViewCall(primaryView,SCI_GETCURRENTPOS,0,0)==2 && SciCall_GetAnchor()==4 && SciCall_GetCurrentPos()==6 && Sci_ViewCall(primaryView,SCI_GETZOOM,0,0)==1 && SciCall_GetZoom()==3,"DPI change handler preserves independent split selection and zoom");
    _WorkspaceTestCheck(&result,workspace->documents[workspace->active].version==styleVersion && SciCall_CanUndo() && Globals.hwndEdit==secondView,"DPI change handler preserves document version undo and focused view");
    RECT gutterRect; GetClientRect(workspace->splitGutter,&gutterRect);
    BITMAPINFO gutterInfo = {0}; gutterInfo.bmiHeader.biSize=sizeof(BITMAPINFOHEADER); gutterInfo.bmiHeader.biWidth=max_i(1,gutterRect.right);
    gutterInfo.bmiHeader.biHeight=-4; gutterInfo.bmiHeader.biPlanes=1; gutterInfo.bmiHeader.biBitCount=32; gutterInfo.bmiHeader.biCompression=BI_RGB;
    HDC gutterDc=CreateCompatibleDC(NULL); void* gutterPixels=NULL;
    HBITMAP gutterBitmap=gutterDc ? CreateDIBSection(gutterDc,&gutterInfo,DIB_RGB_COLORS,&gutterPixels,NULL,0) : NULL;
    HGDIOBJ gutterOld=gutterBitmap ? SelectObject(gutterDc,gutterBitmap) : NULL;
    PANEL_COLORS gutterColors; _PanelColors(&gutterColors);
    if (gutterBitmap) SendMessageW(workspace->splitGutter,WM_PRINTCLIENT,(WPARAM)gutterDc,PRF_CLIENT);
    _WorkspaceTestCheck(&result,gutterBitmap && gutterRect.right>=3 && GetPixel(gutterDc,0,1)==gutterColors.panel && GetPixel(gutterDc,gutterRect.right/2,1)==gutterColors.disabled,"split gutter prints current panel background and visible divider pixels");
    DWORD const gutterGdiBefore=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);
    if (gutterBitmap) for (int i=0;i<40;++i) SendMessageW(workspace->splitGutter,WM_ERASEBKGND,(WPARAM)gutterDc,0);
    _WorkspaceTestCheck(&result,gutterBitmap && GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS)==gutterGdiBefore && workspace->documents[workspace->active].version==styleVersion,"repeated gutter erase uses no new GDI objects and does not edit document");
    if (gutterOld) SelectObject(gutterDc,gutterOld); if (gutterBitmap) DeleteObject(gutterBitmap); if (gutterDc) DeleteDC(gutterDc);
    SciCall_Undo();
    Sci_ViewCall(primaryView, SCI_SETSEL, 1, 2); SciCall_SetSel(4, 6);
    _WorkspaceSwitch(1); _WorkspaceCloseActive();
    _SplitToggle();
    _WorkspaceTestCheck(&result, !workspace->splitView && Globals.hwndEdit == primaryView && _WorkspaceTestText("alpha\r\nA") && !IsSaveNeeded(), "closing focused split retains document and activates primary view");
    _SplitToggle();
    _WorkspaceTestCheck(&result, Sci_ViewCall(workspace->splitView, SCI_GETANCHOR, 0, 0) == 4 && Sci_ViewCall(workspace->splitView, SCI_GETCURRENTPOS, 0, 0) == 6, "reopening split retains right view selection");
    _SplitToggle();
    _SplitToggle();
    HPATHL splitFoldFile = Path_Allocate(folder); Path_Append(splitFoldFile, L"fold.json");
    bool splitFoldLoaded = FileLoad(splitFoldFile, FLF_DontSave, 0, 0); Path_Release(splitFoldFile);
    Sci_ColouriseAll();
    SciCall_FoldLine(1, SC_FOLDACTION_CONTRACT);
    Sci_ViewCall(workspace->splitView, SCI_FOLDLINE, 1, SC_FOLDACTION_EXPAND);
    _WorkspaceTestCheck(&result, splitFoldLoaded && !SciCall_GetFoldExpanded(1) && Sci_ViewCall(workspace->splitView, SCI_GETFOLDEXPANDED, 1, 0), "split views permit opposite fold states in shared document");
    _WorkspaceSwitch(0); _WorkspaceSwitch(1);
    _WorkspaceTestCheck(&result, !SciCall_GetFoldExpanded(1) && Sci_ViewCall(workspace->splitView, SCI_GETFOLDEXPANDED, 1, 0), "tab return restores independent left and right folds");
    _SplitToggle(); _SplitToggle();
    _WorkspaceTestCheck(&result, !SciCall_GetFoldExpanded(1) && Sci_ViewCall(workspace->splitView, SCI_GETFOLDEXPANDED, 1, 0), "reopening split restores its expanded state beside folded primary");
    _SplitToggle(); _WorkspaceCloseActive();
    _FileTreeToggle();
    _WorkspaceTestCheck(&result, FileTree_IsOpen(Globals.hwndMain) && _WorkspaceTestTreeReady(), "native file tree loads asynchronously");
    HWND panel = FindWindowExW(Globals.hwndMain, NULL, L"NP3WorkspaceFileTree", NULL);
    HWND tree = FindWindowExW(panel, NULL, WC_TREEVIEWW, NULL);
    WCHAR searchRoot[MAX_PATH_EXPLICIT]; StringCchPrintfW(searchRoot,COUNTOF(searchRoot),L"%s\\nested",folder);
    HWND search=FileSearch_Show(Globals.hwndMain,searchRoot);
    _WorkspaceTestCheck(&result,search && FileSearch_Show(Globals.hwndMain,folder)==search && (GetWindowLongPtrW(GetDlgItem(search,106),GWL_STYLE)&LVS_OWNERDATA),"file search creates reusable native virtual result window");
    const WCHAR* presetValues[]={L"*.md;*.markdown",L"*.md;*.markdown;*.txt;*.log",L"*.json;*.ini;*.yaml;*.yml;*.toml",L"*"};WCHAR presetActual[256];
    for(int preset=0;preset<4;++preset){SendMessageW(search,WM_COMMAND,116+preset,0);GetDlgItemTextW(search,102,presetActual,COUNTOF(presetActual));_WorkspaceTestCheck(&result,!lstrcmpW(presetActual,presetValues[preset]) && ListView_GetItemCount(GetDlgItem(search,106))==0,"preset menu command fills editable types without automatic scanning");}SetDlgItemTextW(search,102,L"*.md");
    SendMessageW(search,WM_COMMAND,104,0);
    _WorkspaceTestCheck(&result,_WorkspaceTestSearchReady(search) && ListView_GetItemCount(GetDlgItem(search,106))==0,"file search defaults to markdown and excludes text fixture");
    SetDlgItemTextW(search,102,L"*.txt"); SendMessageW(search,WM_COMMAND,104,0);
    _WorkspaceTestCheck(&result,_WorkspaceTestSearchReady(search) && ListView_GetItemCount(GetDlgItem(search,106))==1,"file search asynchronously lists matching nested text file");
    SetDlgItemTextW(search,103,L"does-not-match");SendMessageW(search,WM_COMMAND,104,0);
    _WorkspaceTestCheck(&result,_WorkspaceTestSearchReady(search) && ListView_GetItemCount(GetDlgItem(search,106))==0,"file search path keyword filters results");
    SetDlgItemTextW(search,102,L"bad?type"); SendMessageW(search,WM_COMMAND,104,0);
    WCHAR searchStatus[256];GetDlgItemTextW(search,107,searchStatus,COUNTOF(searchStatus));
    _WorkspaceTestCheck(&result,wcsstr(searchStatus,L"格式无效")!=NULL,"file search rejects invalid extension input without starting scan");
    SetDlgItemTextW(search,101,folder);SetDlgItemTextW(search,102,L"*.txt");SetDlgItemTextW(search,103,L"child.txt");SetDlgItemTextW(search,108,L"nested");SendMessageW(search,WM_COMMAND,104,0);
    _WorkspaceTestCheck(&result,_WorkspaceTestSearchReady(search) && ListView_GetItemCount(GetDlgItem(search,106))==12,"search exclusion setting skips selected nested folder while retaining other roots");
    SetDlgItemTextW(search,108,L"");SendMessageW(search,WM_COMMAND,104,0);
    _WorkspaceTestCheck(&result,_WorkspaceTestSearchReady(search) && ListView_GetItemCount(GetDlgItem(search,106))==13,"empty exclusion setting searches all fixture child folders");
    SendDlgItemMessageW(search,109,BM_SETCHECK,BST_UNCHECKED,0);SendMessageW(search,WM_COMMAND,104,0);
    _WorkspaceTestCheck(&result,_WorkspaceTestSearchReady(search) && ListView_GetItemCount(GetDlgItem(search,106))==0,"unchecked recursion setting excludes all descendant fixture files");
    SetDlgItemTextW(search,108,L"../nested");SendMessageW(search,WM_COMMAND,104,0);GetDlgItemTextW(search,107,searchStatus,COUNTOF(searchStatus));
    _WorkspaceTestCheck(&result,wcsstr(searchStatus,L"排除目录")!=NULL,"exclusion setting rejects paths instead of silently accepting ineffective names");
    SetDlgItemTextW(search,101,searchRoot);SetDlgItemTextW(search,103,L"");SetDlgItemTextW(search,108,L"");SetDlgItemTextW(search,110,L"fixture");SendMessageW(search,WM_COMMAND,104,0);
    _WorkspaceTestCheck(&result,_WorkspaceTestSearchReady(search) && ListView_GetItemCount(GetDlgItem(search,106))==1,"native content search publishes a matching result row");
    WCHAR matchCell[128];ListView_GetItemText(GetDlgItem(search,106),0,2,matchCell,COUNTOF(matchCell));
    _WorkspaceTestCheck(&result,!lstrcmpW(matchCell,L"1:8"),"native content result exposes actual line and UTF16 column");
    int searchOriginalCount=workspace->count;sptr_t searchOriginalHandle=SciCall_GetDocPointer();
    ListView_SetItemState(GetDlgItem(search,106),0,LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);NMHDR searchOpen={GetDlgItem(search,106),106,NM_RETURN};SendMessageW(search,WM_NOTIFY,106,(LPARAM)&searchOpen);
    _WorkspaceTestCheck(&result,workspace->count==searchOriginalCount+1 && SciCall_GetSelectionStart()==7 && SciCall_GetSelectionEnd()==14 && _WorkspaceTestText("nested fixture"),"content result Enter opens correct tab and selects exact matching bytes");
    SendMessageW(search,WM_NOTIFY,106,(LPARAM)&searchOpen);
    _WorkspaceTestCheck(&result,workspace->count==searchOriginalCount+1,"opening content result twice reuses existing document tab");
    SciCall_SetSel(0,0);SciCall_ReplaceSel("new ");unsigned long long searchDirtyVersion=_WorkspaceVersion();DocPos searchDirtySelection=SciCall_GetSelectionStart();
    SendMessageW(search,WM_NOTIFY,106,(LPARAM)&searchOpen);GetDlgItemTextW(search,107,searchStatus,COUNTOF(searchStatus));
    _WorkspaceTestCheck(&result,wcsstr(searchStatus,L"内容已变化") && _WorkspaceVersion()==searchDirtyVersion && IsSaveNeeded() && SciCall_GetSelectionStart()==searchDirtySelection && _WorkspaceTestText("new nested fixture"),"stale content result refuses relocation and preserves unsaved text selection and version");
    SciCall_SetSavePoint();_WorkspaceCloseActive();
    _WorkspaceTestCheck(&result,SciCall_GetDocPointer()==searchOriginalHandle && workspace->count==searchOriginalCount,"content result close returns to original independent document");
    SetDlgItemTextW(search,110,L"FIXTURE");SendDlgItemMessageW(search,111,BM_SETCHECK,BST_CHECKED,0);SendMessageW(search,WM_COMMAND,104,0);
    _WorkspaceTestCheck(&result,_WorkspaceTestSearchReady(search) && ListView_GetItemCount(GetDlgItem(search,106))==0,"native case sensitive content option filters uppercase query");
    WCHAR unicodeSearchPath[MAX_PATH_EXPLICIT];StringCchPrintfW(unicodeSearchPath,COUNTOF(unicodeSearchPath),L"%s\\定位搜索.txt",folder);
    const char unicodeSearchText[]="prefix\r\n中文 fixture\r\n";DWORD unicodeSearchWritten=0;
    HANDLE unicodeSearchFile=CreateFileW(unicodeSearchPath,GENERIC_WRITE,0,NULL,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,NULL);
    bool unicodeSearchCreated=unicodeSearchFile!=INVALID_HANDLE_VALUE && WriteFile(unicodeSearchFile,unicodeSearchText,sizeof(unicodeSearchText)-1,&unicodeSearchWritten,NULL) && unicodeSearchWritten==sizeof(unicodeSearchText)-1;
    if(unicodeSearchFile!=INVALID_HANDLE_VALUE)CloseHandle(unicodeSearchFile);
    SetDlgItemTextW(search,101,folder);SetDlgItemTextW(search,103,L"定位搜索");SetDlgItemTextW(search,110,L"fixture");SendDlgItemMessageW(search,109,BM_SETCHECK,BST_CHECKED,0);SendMessageW(search,WM_COMMAND,104,0);
    _WorkspaceTestCheck(&result,unicodeSearchCreated && _WorkspaceTestSearchReady(search) && ListView_GetItemCount(GetDlgItem(search,106))==1,"native content search reads explicit Unicode filename fixture");
    ListView_GetItemText(GetDlgItem(search,106),0,2,matchCell,COUNTOF(matchCell));
    _WorkspaceTestCheck(&result,!lstrcmpW(matchCell,L"2:4"),"native content search reports character column after Chinese text");
    ListView_SetItemState(GetDlgItem(search,106),0,LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);SendMessageW(search,WM_NOTIFY,106,(LPARAM)&searchOpen);
    _WorkspaceTestCheck(&result,workspace->count==searchOriginalCount+1 && SciCall_GetSelectionStart()==15 && SciCall_GetSelectionEnd()==22 && _WorkspaceTestText(unicodeSearchText),"content location converts Chinese UTF16 column into exact UTF8 selection bytes");
    if(workspace->count>searchOriginalCount){SciCall_SetSavePoint();_WorkspaceCloseActive();}
    SendDlgItemMessageW(search,114,BM_SETCHECK,BST_CHECKED,0);SetDlgItemTextW(search,110,L"fixt(?:ure)");SendMessageW(search,WM_COMMAND,104,0);
    _WorkspaceTestCheck(&result,_WorkspaceTestSearchReady(search) && ListView_GetItemCount(GetDlgItem(search,106))==1,"native regex checkbox invokes PCRE2 grouped expression search");
    SetDlgItemTextW(search,110,L"(");SendMessageW(search,WM_COMMAND,104,0);GetDlgItemTextW(search,107,searchStatus,COUNTOF(searchStatus));
    _WorkspaceTestCheck(&result,wcsstr(searchStatus,L"正则表达式无效") && ListView_GetItemCount(GetDlgItem(search,106))==1,"native invalid regex rejects scan and preserves prior results");
    SetDlgItemTextW(search,110,L"\\p{Han}+");SendMessageW(search,WM_COMMAND,104,0);
    _WorkspaceTestCheck(&result,_WorkspaceTestSearchReady(search) && ListView_GetItemCount(GetDlgItem(search,106))==1,"native regex supports Unicode property match");
    ListView_SetItemState(GetDlgItem(search,106),0,LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);SendMessageW(search,WM_NOTIFY,106,(LPARAM)&searchOpen);
    _WorkspaceTestCheck(&result,workspace->count==searchOriginalCount+1 && SciCall_GetSelectionStart()==8 && SciCall_GetSelectionEnd()==14 && _WorkspaceTestText(unicodeSearchText),"native regex Unicode match selects actual Chinese UTF8 bytes");
    if(workspace->count>searchOriginalCount){SciCall_SetSavePoint();_WorkspaceCloseActive();}
    _WorkspaceTestSearchErrors(search,searchRoot,folder,&result);
    DestroyWindow(search);
    search=FileSearch_Show(Globals.hwndMain,searchRoot);
    _WorkspaceTestCheck(&result,search && ListView_GetItemCount(GetDlgItem(search,106))==0,"file search closes and recreates without stale results");
    DestroyWindow(search);
    HWND treeGrip=FindWindowExW(panel,NULL,L"STATIC",NULL); RECT treeBefore,treeAfter; GetWindowRect(panel,&treeBefore);
    if(treeGrip) { SendMessageW(treeGrip,WM_LBUTTONDOWN,MK_LBUTTON,0); SendMessageW(treeGrip,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(80,0)); }
    GetWindowRect(panel,&treeAfter);
    _WorkspaceTestCheck(&result,treeGrip && GetCapture()==treeGrip && treeAfter.right-treeAfter.left>treeBefore.right-treeBefore.left,"file tree grip drag widens directory panel");
    if(treeGrip) SendMessageW(treeGrip,WM_CANCELMODE,0,0); GetWindowRect(panel,&treeAfter);
    _WorkspaceTestCheck(&result,treeGrip && GetCapture()!=treeGrip && treeAfter.right-treeAfter.left==treeBefore.right-treeBefore.left,"canceling tree width drag restores width and releases capture");
    int treeNarrow=FileTree_Layout(Globals.hwndMain,0,0,1,1),treeZero=FileTree_Layout(Globals.hwndMain,0,0,0,0);
    _WorkspaceTestCheck(&result,treeNarrow>=0 && treeNarrow<=1 && treeZero==0,"file tree width safely clamps zero and narrow layout");
    _SplitRelayout();
    if(treeGrip) { SendMessageW(treeGrip,WM_LBUTTONDOWN,MK_LBUTTON,0); SendMessageW(treeGrip,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(40,0)); SendMessageW(treeGrip,WM_LBUTTONUP,0,0); }
    GetWindowRect(panel,&treeBefore);
    _FileTreeToggle(); _FileTreeToggle();
    panel=FindWindowExW(Globals.hwndMain,NULL,L"NP3WorkspaceFileTree",NULL);
    tree=FindWindowExW(panel,NULL,WC_TREEVIEWW,NULL); GetWindowRect(panel,&treeAfter);
    _WorkspaceTestCheck(&result,_WorkspaceTestTreeReady() && treeAfter.right-treeAfter.left==treeBefore.right-treeBefore.left,"file tree committed width survives close and reopen");
    wchar_t rememberedRoot[32768]={0}; TVITEMW rememberedItem={0}; rememberedItem.mask=TVIF_TEXT; rememberedItem.hItem=TreeView_GetRoot(tree); rememberedItem.pszText=rememberedRoot; rememberedItem.cchTextMax=COUNTOF(rememberedRoot); TreeView_GetItem(tree,&rememberedItem);
    _WorkspaceTestCheck(&result,lstrcmpiW(rememberedRoot,folder)==0,"file tree root survives close and reopen");
    wchar_t customTreeRoot[32768]; StringCchPrintfW(customTreeRoot,COUNTOF(customTreeRoot),L"%s\\nested",folder);
    bool customRootLoaded=FileTree_SetRoot(Globals.hwndMain,customTreeRoot) && _WorkspaceTestTreeReady();
    _FileTreeToggle(); _FileTreeToggle();
    panel=FindWindowExW(Globals.hwndMain,NULL,L"NP3WorkspaceFileTree",NULL); tree=FindWindowExW(panel,NULL,WC_TREEVIEWW,NULL);
    bool customRootReady=_WorkspaceTestTreeReady(); rememberedItem.hItem=TreeView_GetRoot(tree); TreeView_GetItem(tree,&rememberedItem);
    _WorkspaceTestCheck(&result,customRootLoaded && customRootReady && lstrcmpiW(rememberedRoot,customTreeRoot)==0 && _WorkspaceTestTreeChild(tree,rememberedItem.hItem,L"child.txt"),"chosen tree root survives reopen independently of active file directory");
    _WorkspaceTestCheck(&result,FileTree_SetRoot(Globals.hwndMain,folder) && _WorkspaceTestTreeReady(),"file tree can return to original root after remembered custom directory");
    HTREEITEM root = TreeView_GetRoot(tree);
    HIMAGELIST treeIcons=TreeView_GetImageList(tree,TVSIL_NORMAL); int treeIconWidth=0,treeIconHeight=0;
    if(treeIcons) ImageList_GetIconSize(treeIcons,&treeIconWidth,&treeIconHeight);
    _WorkspaceTestCheck(&result,treeIcons && ImageList_GetImageCount(treeIcons)==2 && treeIconWidth==GetSystemMetricsForDpi(SM_CXSMICON,GetDpiForWindow(panel)) && treeIconHeight==GetSystemMetricsForDpi(SM_CYSMICON,GetDpiForWindow(panel)),"file tree owns folder and document icons at current DPI size");
    TVITEMW rootIcon={0}; rootIcon.mask=TVIF_IMAGE|TVIF_SELECTEDIMAGE; rootIcon.hItem=root; TreeView_GetItem(tree,&rootIcon);
    HTREEITEM treeFile=_WorkspaceTestTreeChild(tree,root,L"a.txt"); TVITEMW fileIcon={0}; fileIcon.mask=TVIF_IMAGE|TVIF_SELECTEDIMAGE; fileIcon.hItem=treeFile; TreeView_GetItem(tree,&fileIcon);
    _WorkspaceTestCheck(&result,root && treeFile && rootIcon.iImage==0 && rootIcon.iSelectedImage==0 && fileIcon.iImage==1 && fileIcon.iSelectedImage==1,"file tree distinguishes folder and file nodes with stable icons");

    HTREEITEM folderItem = _WorkspaceTestTreeChild(tree, root, L"nested");
    HTREEITEM lazyPlaceholder=TreeView_GetChild(tree,folderItem); TVITEMW lazyItem={0}; lazyItem.mask=TVIF_PARAM; lazyItem.hItem=lazyPlaceholder; TreeView_GetItem(tree,&lazyItem);
    _WorkspaceTestCheck(&result, folderItem && lazyPlaceholder && !lazyItem.lParam && !TreeView_GetNextSibling(tree,lazyPlaceholder) && !_WorkspaceTestTreeChild(tree,folderItem,L"child.txt"), "directory children remain lazy with only a loading placeholder before expansion");
    TreeView_Expand(tree, folderItem, TVE_EXPAND);
    _WorkspaceTestCheck(&result, _WorkspaceTestTreeReady() && _WorkspaceTestTreeChild(tree, folderItem, L"child.txt"), "expanding directory loads its children");
    _WorkspaceTestCheck(&result,(TreeView_GetItemState(tree,root,TVIS_EXPANDED)&TVIS_EXPANDED) && (TreeView_GetItemState(tree,folderItem,TVIS_EXPANDED)&TVIS_EXPANDED),"asynchronous root and first directory load retain requested expansion");
    TreeView_SelectItem(tree,treeFile); DWORD treeGdiBefore=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);
    SendMessageW(panel,NP3_WM_PANEL_REFRESH,0,0); HIMAGELIST refreshedIcons=TreeView_GetImageList(tree,TVSIL_NORMAL);
    _WorkspaceTestCheck(&result,refreshedIcons && refreshedIcons!=treeIcons && TreeView_GetRoot(tree)==root && TreeView_GetSelection(tree)==treeFile && (TreeView_GetItemState(tree,folderItem,TVIS_EXPANDED)&TVIS_EXPANDED),"tree theme and DPI resource refresh preserves root selection and expanded nodes");
    treeGdiBefore=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);
    for(int refresh=0;refresh<40;++refresh) SendMessageW(panel,NP3_WM_PANEL_REFRESH,0,0);
    refreshedIcons=TreeView_GetImageList(tree,TVSIL_NORMAL); treeIconWidth=treeIconHeight=0; if(refreshedIcons) ImageList_GetIconSize(refreshedIcons,&treeIconWidth,&treeIconHeight);
    DWORD treeGdiAfter=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);
    char treeRefreshNote[256]; DWORD treeRefreshWritten;
    StringCchPrintfA(treeRefreshNote,COUNTOF(treeRefreshNote),"TREE_REFRESH root=%i selected=%i expanded=%i images=%i size=%ix%i expected=%ix%i GDI=%lu/%lu\r\n",TreeView_GetRoot(tree)==root,TreeView_GetSelection(tree)==treeFile,(TreeView_GetItemState(tree,folderItem,TVIS_EXPANDED)&TVIS_EXPANDED)!=0,refreshedIcons?ImageList_GetImageCount(refreshedIcons):-1,treeIconWidth,treeIconHeight,GetSystemMetricsForDpi(SM_CXSMICON,GetDpiForWindow(panel)),GetSystemMetricsForDpi(SM_CYSMICON,GetDpiForWindow(panel)),(unsigned long)treeGdiBefore,(unsigned long)treeGdiAfter);
    WriteFile(result.log,treeRefreshNote,(DWORD)strlen(treeRefreshNote),&treeRefreshWritten,NULL);
    _WorkspaceTestCheck(&result,treeGdiBefore && treeGdiAfter<=treeGdiBefore+2 && refreshedIcons && ImageList_GetImageCount(refreshedIcons)==2 && treeIconWidth==GetSystemMetricsForDpi(SM_CXSMICON,GetDpiForWindow(panel)) && treeIconHeight==GetSystemMetricsForDpi(SM_CYSMICON,GetDpiForWindow(panel)),"repeated tree resource refresh matches current DPI and releases old image lists");
    HTREEITEM queuedFolders[12] = {0};
    for (int i = 0; i < 12; ++i) {
        wchar_t queuedLabel[32]; StringCchPrintfW(queuedLabel, COUNTOF(queuedLabel), L"queued-%02i", i);
        queuedFolders[i] = _WorkspaceTestTreeChild(tree, root, queuedLabel);
        TreeView_Expand(tree, queuedFolders[i], TVE_EXPAND);
    }
    UINT pendingBefore=TreeView_GetItemState(tree,queuedFolders[0],TVIS_EXPANDED);
    BOOL pendingCollapsed=TreeView_Expand(tree,queuedFolders[0],TVE_COLLAPSE);
    UINT pendingAfter=TreeView_GetItemState(tree,queuedFolders[0],TVIS_EXPANDED);
    StringCchPrintfA(treeRefreshNote,COUNTOF(treeRefreshNote),"TREE_PENDING before=%u collapsed=%i after=%u\r\n",pendingBefore,pendingCollapsed,pendingAfter); WriteFile(result.log,treeRefreshNote,(DWORD)strlen(treeRefreshNote),&treeRefreshWritten,NULL);
    bool queuedComplete = _WorkspaceTestTreeReady();
    for (int i = 0; i < 12; ++i) queuedComplete = queuedComplete && queuedFolders[i] && _WorkspaceTestTreeChild(tree, queuedFolders[i], L"child.txt");
    _WorkspaceTestCheck(&result, queuedComplete, "all concurrent directory expansions eventually populate");
    _WorkspaceTestCheck(&result,!(TreeView_GetItemState(tree,queuedFolders[0],TVIS_EXPANDED)&TVIS_EXPANDED),"collapsing pending directory prevents async completion from reopening it");
    TreeView_Expand(tree,queuedFolders[0],TVE_EXPAND);
    _WorkspaceTestCheck(&result,(TreeView_GetItemState(tree,queuedFolders[0],TVIS_EXPANDED)&TVIS_EXPANDED)!=0,"completed directory expands normally after pending collapse");
    HTREEITEM largeItem = _WorkspaceTestTreeChild(tree, root, L"large");
    TreeView_Expand(tree, largeItem, TVE_EXPAND);
    bool firstPageReady = _WorkspaceTestTreeReady();
    int firstPageCount = 0; HTREEITEM moreItem = NULL;
    for (HTREEITEM item = TreeView_GetChild(tree, largeItem); item; item = TreeView_GetNextSibling(tree, item)) { ++firstPageCount; moreItem = item; }
    _WorkspaceTestCheck(&result, firstPageReady && firstPageCount == 5001, "large directory shows bounded first page and continuation action");
    HTREEITEM retainedItem = TreeView_GetChild(tree, largeItem);
    TreeView_SelectItem(tree, moreItem);
    NMHDR nextPage = {tree, 0, NM_RETURN}; SendMessageW(panel, WM_NOTIFY, 0, (LPARAM)&nextPage);
    bool allPagesReady = _WorkspaceTestTreeReady();
    bool seenFiles[5012] = {false}; int fileCount = 0; bool uniqueFiles = true;
    for (HTREEITEM item = TreeView_GetChild(tree, largeItem); item; item = TreeView_GetNextSibling(tree, item)) {
        wchar_t fileLabel[64]; TVITEMW entry = {0}; entry.hItem = item; entry.mask = TVIF_TEXT; entry.pszText = fileLabel; entry.cchTextMax = COUNTOF(fileLabel);
        TreeView_GetItem(tree, &entry);
        wchar_t* suffix = NULL; long number = wcstol(fileLabel + 5, &suffix, 10);
        if (wcsncmp(fileLabel, L"item-", 5) || wcscmp(suffix, L".txt") || number < 0 || number >= 5012 || seenFiles[number]) uniqueFiles = false;
        else seenFiles[number] = true;
        ++fileCount;
    }
    _WorkspaceTestCheck(&result, allPagesReady && uniqueFiles && fileCount == 5012, "continuation returns every file exactly once and removes action at end");
    _WorkspaceTestCheck(&result, retainedItem == TreeView_GetChild(tree, largeItem), "continuation preserves existing tree nodes");
    HTREEITEM fileItem = _WorkspaceTestTreeChild(tree, root, L"b.txt");
    SciCall_AppendText(1, "+");
    TreeView_SelectItem(tree, fileItem);
    NMHDR activate = { tree, 0, NM_RETURN }; SendMessageW(panel, WM_NOTIFY, 0, (LPARAM)&activate);
    _WorkspaceTestCheck(&result, workspace->count == 2 && _WorkspaceTestText("beta\r\nB"), "file-tree activation opens file in another tab");
    _WorkspaceSwitch(0);
    _WorkspaceTestCheck(&result, _WorkspaceTestText("alpha\r\nA+") && IsSaveNeeded(), "file-tree opening preserves unsaved source tab");
    SciCall_Undo(); _WorkspaceSwitch(1); _WorkspaceCloseActive();
    _WorkspaceTestCheck(&result, !FileTree_SetRoot(Globals.hwndMain, L"?:\\invalid") && TreeView_GetRoot(tree) == root, "invalid folder leaves current tree intact");
    _FileTreeToggle();
    _WorkspaceTestCheck(&result, !FileTree_IsOpen(Globals.hwndMain), "file tree closes independently of documents");
    _FileTreeToggle();
    bool refreshed = FileTree_SetRoot(Globals.hwndMain, folder) && FileTree_SetRoot(Globals.hwndMain, folder) && _WorkspaceTestTreeReady();
    panel = FindWindowExW(Globals.hwndMain, NULL, L"NP3WorkspaceFileTree", NULL);
    tree = FindWindowExW(panel, NULL, WC_TREEVIEWW, NULL);
    _WorkspaceTestCheck(&result, refreshed && _WorkspaceTestTreeChild(tree, TreeView_GetRoot(tree), L"nested"), "repeated root refresh discards stale work and loads replacement");
    FileTree_SetRoot(Globals.hwndMain, folder);
    _FileTreeToggle(); // Close before dispatching the pending read result.
    _FileTreeToggle();
    _WorkspaceTestCheck(&result, _WorkspaceTestTreeReady() && _WorkspaceTestText("alpha\r\nA") && !IsSaveNeeded(), "close during read and reopen preserves editor and loads fresh tree");
    _FileTreeToggle();
    SciCall_BeginUndoAction();
    const char* logLines = "\r\nERROR 中文\r\ninfo\r\nERROR end";
    SciCall_AppendText(strlen(logLines), logLines); SciCall_EndUndoAction();
    unsigned long long logVersion = workspace->documents[workspace->active].version;
    _LogFilterToggle();
    HWND logPanel = FindWindowExW(Globals.hwndMain, NULL, L"NP3LogFilterPanel", NULL);
    HWND logList = GetDlgItem(logPanel, 107);
    SetWindowTextW(GetDlgItem(logPanel, 101), L"ERROR");
    SendMessageW(logPanel, WM_COMMAND, MAKEWPARAM(105, BN_CLICKED), (LPARAM)GetDlgItem(logPanel, 105));
    _WorkspaceTestCheck(&result, LogPanel_IsOpen(Globals.hwndMain) && _WorkspaceTestLogReady() && ListView_GetItemCount(logList) == 2, "native log panel asynchronously filters unsaved buffer");
    _WorkspaceTestCheck(&result, workspace->documents[workspace->active].version == logVersion && IsSaveNeeded(), "log filtering retains document version and dirty state");
    FileLoad(b, FLF_DontSave, 0, 0);
    ListView_SetItemState(logList, 0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    NMHDR locate = {logList, 107, NM_RETURN}; SendMessageW(logPanel, WM_NOTIFY, 0, (LPARAM)&locate);
    _WorkspaceTestCheck(&result, workspace->active == 0 && SciCall_GetSelectionStart() == 10 && SciCall_GetSelectionEnd() == 15, "log result activation returns to source tab and selects actual match");
    SciCall_BeginUndoAction(); SciCall_AppendText(1, "!"); SciCall_EndUndoAction();
    _WorkspaceSwitch(1);
    SendMessageW(logPanel, WM_NOTIFY, 0, (LPARAM)&locate);
    WCHAR staleText[256], expectedStale[256]; GetWindowTextW(GetDlgItem(logPanel, 108), staleText, COUNTOF(staleText));
    GetLngString(IDS_MUI_LOG_STALE, expectedStale, COUNTOF(expectedStale));
    _WorkspaceTestCheck(&result, workspace->active == 1 && wcscmp(staleText, expectedStale) == 0, "changed source rejects stale log offsets without switching tabs");
    _WorkspaceCloseActive(); SciCall_Undo(); SciCall_Undo();
    _WorkspaceTestCheck(&result, _WorkspaceTestText("alpha\r\nA") && !IsSaveNeeded(), "log filtering and locating preserve original undo history");
    SetWindowTextW(GetDlgItem(logPanel, 101), L"["); SendMessageW(GetDlgItem(logPanel, 102), BM_SETCHECK, BST_CHECKED, 0);
    _LogFilterRun();
    WCHAR patternError[256], expectedPattern[256];
    bool errorReady = _WorkspaceTestLogReady(); GetWindowTextW(GetDlgItem(logPanel, 108), patternError, COUNTOF(patternError));
    GetLngString(IDS_MUI_LOG_PATTERN_ERROR, expectedPattern, COUNTOF(expectedPattern));
    _WorkspaceTestCheck(&result, errorReady && ListView_GetItemCount(logList) == 0 && wcsncmp(patternError, expectedPattern, wcslen(expectedPattern)) == 0, "log panel reports invalid regex and clears old results");
    SetWindowTextW(GetDlgItem(logPanel, 101), L"alpha"); SendMessageW(GetDlgItem(logPanel, 102), BM_SETCHECK, BST_UNCHECKED, 0);
    _LogFilterRun(); _LogFilterToggle(); _LogFilterToggle();
    logPanel = FindWindowExW(Globals.hwndMain, NULL, L"NP3LogFilterPanel", NULL); SetWindowTextW(GetDlgItem(logPanel, 101), L"alpha");
    _WorkspaceTestLogReady(); _LogFilterRun();
    _WorkspaceTestCheck(&result, _WorkspaceTestLogReady() && ListView_GetItemCount(GetDlgItem(logPanel, 107)) == 1 && _WorkspaceTestText("alpha\r\nA"), "close during log work and reopen loads fresh results without altering editor");
    // Cancel without pumping publication: even a worker that just finished must not publish its old rows.
    _LogFilterRun();
    SendMessageW(logPanel, WM_COMMAND, MAKEWPARAM(106, BN_CLICKED), (LPARAM)GetDlgItem(logPanel, 106));
    bool cancelReady = _WorkspaceTestLogReady(); WCHAR canceledText[256], expectedCanceled[256];
    GetWindowTextW(GetDlgItem(logPanel,108), canceledText, COUNTOF(canceledText)); GetLngString(IDS_MUI_LOG_CANCELED, expectedCanceled, COUNTOF(expectedCanceled));
    _WorkspaceTestCheck(&result, cancelReady && ListView_GetItemCount(GetDlgItem(logPanel,107)) == 0 && wcscmp(canceledText,expectedCanceled) == 0 && IsWindowEnabled(GetDlgItem(logPanel,105)) && !IsWindowEnabled(GetDlgItem(logPanel,106)), "cancel button discards unpublished log results and restores run controls");
    _WorkspaceTestCheck(&result, _WorkspaceTestText("alpha\r\nA") && !IsSaveNeeded(), "canceling log filter preserves source text and saved state");
    _LogFilterRun(); _WorkspaceTestLogReady();
    _WorkspaceTestCheck(&result, ListView_GetItemCount(GetDlgItem(logPanel,107)) == 1, "log filter can rerun after cancellation");
    FileLoad(b, FLF_DontSave, 0, 0); SetWindowTextW(GetDlgItem(logPanel,101),L"beta"); _LogFilterRun(); _WorkspaceTestLogReady();
    HWND closedSourceList=GetDlgItem(logPanel,107); ListView_SetItemState(closedSourceList,0,LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);
    _WorkspaceCloseActive();
    NMHDR closedLocate={closedSourceList,107,NM_RETURN}; SendMessageW(logPanel,WM_NOTIFY,0,(LPARAM)&closedLocate);
    GetWindowTextW(GetDlgItem(logPanel,108),staleText,COUNTOF(staleText));
    _WorkspaceTestCheck(&result, workspace->count == 1 && workspace->active == 0 && wcscmp(staleText,expectedStale) == 0 && _WorkspaceTestText("alpha\r\nA") && !IsSaveNeeded(), "closed log source rejects activation without changing remaining document");
    _LogFilterToggle();
    _AiToggle();
    HWND aiPanel = FindWindowExW(Globals.hwndMain, NULL, L"NP3AiPanel", NULL);
    _WorkspaceTestCheck(&result, AiPanel_IsOpen(workspace->ai) && GetDlgItem(aiPanel, 110) && GetDlgItem(aiPanel, 113) && !IsWindowEnabled(GetDlgItem(aiPanel, 115)), "AI panel exposes four actions and disables apply without a result");
    AI_PANEL_CAPTURE aiRequest = {AI_REWRITE, ""};
    _WorkspaceTestCheck(&result, !_AiRun(&aiRequest) && _WorkspaceTestText("alpha\r\nA") && !AiPanel_IsBusy(workspace->ai), "unconfigured AI never changes source or starts a request");
    SciCall_SetSel(0, 5);
    AI_PANEL_SOURCE aiSource = {workspace->documents[workspace->active].id, workspace->documents[workspace->active].version, 0, 5, 0, 5};
    AI_PANEL_APPLY aiApply = {aiSource, "alpha", 5, "translated", 10};
    SciCall_SetSel(0, 4);
    _WorkspaceTestCheck(&result, !_AiApply(&aiApply) && _WorkspaceTestText("alpha\r\nA"), "AI apply rejects changed selection");
    SciCall_SetSel(0, 5); SciCall_SetReadOnly(true);
    _WorkspaceTestCheck(&result, !_AiApply(&aiApply) && _WorkspaceTestText("alpha\r\nA"), "AI apply rejects read-only document");
    SciCall_SetReadOnly(false);
    aiApply.source.version++;
    _WorkspaceTestCheck(&result, !_AiApply(&aiApply) && _WorkspaceTestText("alpha\r\nA"), "AI apply rejects stale document version");
    aiApply.source = aiSource; aiApply.source.id++;
    _WorkspaceTestCheck(&result, !_AiApply(&aiApply) && _WorkspaceTestText("alpha\r\nA"), "AI apply rejects another source document");
    aiApply.source = aiSource; aiApply.original = "wrong";
    _WorkspaceTestCheck(&result, !_AiApply(&aiApply) && _WorkspaceTestText("alpha\r\nA"), "AI apply independently verifies original bytes");
    aiApply.original = "alpha";
    aiApply.result = "bad\0result"; aiApply.resultLength = 10;
    _WorkspaceTestCheck(&result, !_AiApply(&aiApply) && _WorkspaceTestText("alpha\r\nA") && !IsSaveNeeded(), "AI apply rejects embedded NUL without editing source");
    aiApply.result = "\xc0\xaf"; aiApply.resultLength = 2;
    _WorkspaceTestCheck(&result, !_AiApply(&aiApply) && _WorkspaceTestText("alpha\r\nA"), "AI apply rejects malformed UTF8 before mutation");
    aiApply.result = "x"; aiApply.resultLength = 2 * 1024 * 1024 + 1;
    _WorkspaceTestCheck(&result, !_AiApply(&aiApply) && _WorkspaceTestText("alpha\r\nA"), "AI apply rejects oversized result before reading bytes");
    aiApply.result = "translated"; aiApply.resultLength = 10;
    _WorkspaceTestCheck(&result, _AiApply(&aiApply) && _WorkspaceTestText("translated\r\nA") && IsSaveNeeded(), "confirmed AI replacement updates selected text and dirty state");
    _WorkspaceTestCheck(&result,workspace->documents[workspace->active].version==aiApply.source.version+1,"AI quiet transaction advances source version exactly once");
    _WorkspaceTestCheck(&result, !_AiApply(&aiApply) && _WorkspaceTestText("translated\r\nA"), "AI result cannot be applied twice");
    SciCall_Undo();
    _WorkspaceTestCheck(&result, _WorkspaceTestText("alpha\r\nA") && !IsSaveNeeded(), "AI replacement is undone in one step");
    SciCall_SetSel(aiApply.source.anchor,aiApply.source.caret);
    _WorkspaceTestCheck(&result,!_AiApply(&aiApply) && _WorkspaceTestText("alpha\r\nA") && !IsSaveNeeded(),"undo to identical bytes does not revive old AI source version");
    FileLoad(b, FLF_DontSave, 0, 0); SciCall_SetSel(0,4);
    AI_PANEL_SOURCE closedAiSource = {workspace->documents[workspace->active].id,workspace->documents[workspace->active].version,0,4,0,4};
    AI_PANEL_APPLY closedAiApply = {closedAiSource,"beta",4,"replacement",11};
    _WorkspaceCloseActive();
    _WorkspaceTestCheck(&result, !_AiApply(&closedAiApply) && workspace->count == 1 && _WorkspaceTestText("alpha\r\nA") && !IsSaveNeeded(), "AI apply rejects closed source without editing another document");
    FileLoad(b, FLF_DontSave, 0, 0); SciCall_SetSel(0,4);
    _WorkspaceTestCheck(&result, workspace->documents[workspace->active].id != closedAiSource.id && !_AiApply(&closedAiApply) && _WorkspaceTestText("beta\r\nB") && !IsSaveNeeded(), "AI old result remains stale after reopening identical source path and bytes");
    _WorkspaceCloseActive();
    _AiToggle(); _AiToggle();
    _WorkspaceTestCheck(&result, AiPanel_IsOpen(workspace->ai) && !AiPanel_IsBusy(workspace->ai) && _WorkspaceTestText("alpha\r\nA"), "AI panel closes and reopens with editor state intact");
    _AiToggle();
    if (Path_IsEmpty(Paths.IniFile)) {
        AI_PANEL_SAVE failedSave = {0}; WCHAR failedProfile[73] = {0}; char* failedKey = NULL;
        StringCchPrintfW(failedSave.settings.endpoint,COUNTOF(failedSave.settings.endpoint),L"https://rollback-test.invalid/%lu-%llu",GetCurrentProcessId(),GetTickCount64());
        StringCchCopyW(failedSave.settings.model,COUNTOF(failedSave.settings.model),L"rollback-test-model"); failedSave.settings.timeoutMs=5000; failedSave.secret="fake-rollback-new";
        bool absent=AiCredential_ServiceProfile(failedSave.settings.endpoint,failedProfile,COUNTOF(failedProfile))==ERROR_SUCCESS && AiCredential_Load(failedProfile,&failedKey)==ERROR_NOT_FOUND;
        AiCredential_Free(failedKey); failedKey=NULL;
        _WorkspaceTestCheck(&result,absent,"AI rollback fixture credential starts absent");
        if(absent) {
            bool rejected=!_AiSave(&failedSave,false);
            _WorkspaceTestCheck(&result,rejected && AiCredential_Load(failedProfile,&failedKey)==ERROR_NOT_FOUND && !Settings2.AIEndpoint[0],"failed AI settings save rolls back newly created key and runtime configuration");
            AiCredential_Free(failedKey); failedKey=NULL;
            if(AiCredential_Save(failedProfile,"fake-rollback-existing")==ERROR_SUCCESS) {
                rejected=!_AiSave(&failedSave,false);
                bool restored=AiCredential_Load(failedProfile,&failedKey)==ERROR_SUCCESS && !strcmp(failedKey,"fake-rollback-existing");
                _WorkspaceTestCheck(&result,rejected && restored,"failed AI settings save restores previous scoped credential");
                AiCredential_Free(failedKey); AiCredential_Delete(failedProfile);
            } else _WorkspaceTestCheck(&result,false,"prepare existing rollback fixture key");
        }
    }
    WCHAR aiUrl[2049] = {0};
    if (GetEnvironmentVariableW(L"NP3_AI_SELFTEST_URL", aiUrl, COUNTOF(aiUrl))) {
        WCHAR profile[73] = {0}; char* key = NULL;
        bool isolatedKey = AiCredential_ServiceProfile(aiUrl, profile, COUNTOF(profile)) == ERROR_SUCCESS && AiCredential_Load(profile, &key) == ERROR_NOT_FOUND;
        AiCredential_Free(key); key = NULL;
        _WorkspaceTestCheck(&result, isolatedKey, "AI fixture service credential is initially absent");
        if (isolatedKey) {
            _AiToggle(); aiPanel = FindWindowExW(Globals.hwndMain, NULL, L"NP3AiPanel", NULL);
            SetWindowTextW(GetDlgItem(aiPanel,101),aiUrl); SetWindowTextW(GetDlgItem(aiPanel,102),L"panel-test-model");
            SetWindowTextW(GetDlgItem(aiPanel,103),L"fake-panel-test-key"); SetDlgItemInt(aiPanel,104,5000,FALSE);
            SendMessageW(aiPanel,WM_COMMAND,MAKEWPARAM(105,BN_CLICKED),0);
            WCHAR savedEndpoint[2049] = {0};
            IniFileGetString(Paths.IniFile,L"Settings2",L"AIEndpoint",L"",savedEndpoint,COUNTOF(savedEndpoint));
            bool savedKey = AiCredential_Load(profile,&key) == ERROR_SUCCESS && strcmp(key,"fake-panel-test-key") == 0;
            AiCredential_Free(key); key = NULL;
            _WorkspaceTestCheck(&result, savedKey && !wcscmp(savedEndpoint,aiUrl) && GetWindowTextLengthW(GetDlgItem(aiPanel,103)) == 0, "AI panel persists service and scoped key with independent readback");
            for (int action=AI_EXPLAIN;action<=AI_CORRECT;++action) {
                SciCall_SetSel(0,5); SetWindowTextW(GetDlgItem(aiPanel,120),action == AI_GENERATE ? L"生成中文" : L"");
                if(action>=AI_SUMMARIZE) SendDlgItemMessageW(aiPanel,116,CB_SETCURSEL,action-AI_SUMMARIZE,0);
                SendMessageW(aiPanel,WM_COMMAND,MAKEWPARAM(action>=AI_SUMMARIZE?117:110+action,BN_CLICKED),0);
                bool ready=_WorkspaceTestAiReady(); WCHAR output[128] = {0}; GetWindowTextW(GetDlgItem(aiPanel,122),output,COUNTOF(output));
                _WorkspaceTestCheck(&result,ready && !wcscmp(output,L"模拟结果") && _WorkspaceTestText("alpha\r\nA"),"AI action reaches local service and previews result without editing");
                _WorkspaceTestCheck(&result,IsWindowEnabled(GetDlgItem(aiPanel,115)) == (action != AI_EXPLAIN),"AI explain remains view-only while edit results allow manual apply");
                if(action>=AI_SUMMARIZE) {
                    SendMessageW(aiPanel,WM_COMMAND,MAKEWPARAM(115,BN_CLICKED),0);
                    _WorkspaceTestCheck(&result,_WorkspaceTestText("模拟结果\r\nA"),"new AI text action applies only captured selection");
                    SciCall_Undo();
                    _WorkspaceTestCheck(&result,_WorkspaceTestText("alpha\r\nA") && !IsSaveNeeded(),"new AI text action undoes in one step");
                }
                if(action==AI_REWRITE) {
                    PANEL_COLORS aiColors={0}; _PanelColors(&aiColors);
                    CHARFORMAT2W originalFormat = {0}, resultFormat = {0}; originalFormat.cbSize=sizeof(originalFormat); resultFormat.cbSize=sizeof(resultFormat);
                    SendMessageW(GetDlgItem(aiPanel,121),EM_SETSEL,0,1); SendMessageW(GetDlgItem(aiPanel,121),EM_GETCHARFORMAT,SCF_SELECTION,(LPARAM)&originalFormat);
                    SendMessageW(GetDlgItem(aiPanel,122),EM_SETSEL,0,1); SendMessageW(GetDlgItem(aiPanel,122),EM_GETCHARFORMAT,SCF_SELECTION,(LPARAM)&resultFormat);
                    _WorkspaceTestCheck(&result,(originalFormat.dwMask & CFM_BACKCOLOR) && originalFormat.crBackColor==aiColors.removed,"AI original changed range has themed difference highlighting");
                    _WorkspaceTestCheck(&result,(resultFormat.dwMask & CFM_BACKCOLOR) && resultFormat.crBackColor==aiColors.added,"AI result changed range has themed difference highlighting");
                    CHARRANGE diffSelection={0}; SendMessageW(GetDlgItem(aiPanel,122),EM_EXGETSEL,0,(LPARAM)&diffSelection);
                    unsigned long long themeSourceVersion=workspace->documents[workspace->active].version;
                    SendMessageW(aiPanel,NP3_WM_PANEL_REFRESH,0,0);
                    CHARRANGE afterThemeSelection={0}; SendMessageW(GetDlgItem(aiPanel,122),EM_EXGETSEL,0,(LPARAM)&afterThemeSelection);
                    SendMessageW(GetDlgItem(aiPanel,122),EM_GETCHARFORMAT,SCF_SELECTION,(LPARAM)&resultFormat);
                    GetWindowTextW(GetDlgItem(aiPanel,122),output,COUNTOF(output));
                    _WorkspaceTestCheck(&result,!wcscmp(output,L"模拟结果") && diffSelection.cpMin==afterThemeSelection.cpMin && diffSelection.cpMax==afterThemeSelection.cpMax && resultFormat.crBackColor==aiColors.added && resultFormat.crTextColor==aiColors.highlightText,"AI theme refresh retains result selection and themed difference colors");
                    _WorkspaceTestCheck(&result,workspace->documents[workspace->active].version==themeSourceVersion && _WorkspaceTestText("alpha\r\nA"),"AI theme refresh does not edit source document");

                    SendMessageW(aiPanel,WM_COMMAND,MAKEWPARAM(115,BN_CLICKED),0);
                    _WorkspaceTestCheck(&result,_WorkspaceTestText("模拟结果\r\nA") && !IsWindowEnabled(GetDlgItem(aiPanel,115)),"AI preview apply button changes original selection once");
                    SciCall_Undo(); _WorkspaceTestCheck(&result,_WorkspaceTestText("alpha\r\nA") && !IsSaveNeeded(),"actual AI response replacement undoes in one step");
                }
            }
            SciCall_SetSel(0,5); SetWindowTextW(GetDlgItem(aiPanel,120),L"");
            SendDlgItemMessageW(aiPanel,116,CB_SETCURSEL,AI_ASK-AI_SUMMARIZE,0);
            SendMessageW(aiPanel,WM_COMMAND,MAKEWPARAM(117,BN_CLICKED),0);
            _WorkspaceTestCheck(&result,!AiPanel_IsBusy(workspace->ai),"current document question requires a nonempty question before requesting");
            SetWindowTextW(GetDlgItem(aiPanel,120),L"文档有什么内容？");
            SendMessageW(aiPanel,WM_COMMAND,MAKEWPARAM(117,BN_CLICKED),0);
            _WorkspaceTestCheck(&result,_WorkspaceTestAiReady() && _WorkspaceTestText("alpha\r\nA"),"current document answer arrives without modifying selected or unselected text");
            _WorkspaceTestCheck(&result,!IsWindowEnabled(GetDlgItem(aiPanel,115)),"current document answer is view only");
            SendMessageW(aiPanel,WM_COMMAND,MAKEWPARAM(115,BN_CLICKED),0);
            _WorkspaceTestCheck(&result,_WorkspaceTestText("alpha\r\nA"),"synthetic apply command cannot apply a question answer");
            UndoTransActionBegin(); SciCall_SetTargetRange(0,SciCall_GetLength()); Sci_ReplaceTargetTestChgHist(17,"old\r\nkeep\r\nsecond"); EndUndoTransAction();
            SciCall_SetSel(0,17); SetWindowTextW(GetDlgItem(aiPanel,120),L"");
            SendMessageW(aiPanel,WM_COMMAND,MAKEWPARAM(112,BN_CLICKED),0);
            _WorkspaceTestCheck(&result,_WorkspaceTestAiReady() && _WorkspaceTestText("old\r\nkeep\r\nsecond"),"multi-hunk AI response arrives without editing source");
            PANEL_COLORS multiColors; _PanelColors(&multiColors);
            for(int side=0;side<2;++side) {
                HWND preview=GetDlgItem(aiPanel,121+side); CHARFORMAT2W format={0}; format.cbSize=sizeof(format);
                SendMessageW(preview,EM_SETSEL,0,1); SendMessageW(preview,EM_GETCHARFORMAT,SCF_SELECTION,(LPARAM)&format);
                bool firstChanged=format.crBackColor==(side ? multiColors.added : multiColors.removed);
                SendMessageW(preview,EM_SETSEL,4,5); SendMessageW(preview,EM_GETCHARFORMAT,SCF_SELECTION,(LPARAM)&format);
                bool middlePlain=format.crBackColor==multiColors.field && format.crTextColor==multiColors.text;
                SendMessageW(preview,EM_SETSEL,9,10); SendMessageW(preview,EM_GETCHARFORMAT,SCF_SELECTION,(LPARAM)&format);
                _WorkspaceTestCheck(&result,firstChanged && middlePlain && format.crBackColor==(side ? multiColors.added : multiColors.removed),side ? "AI result highlights separate changes and preserves middle paragraph color" : "AI source highlights separate changes and preserves middle paragraph color");
            }
            SendMessageW(GetDlgItem(aiPanel,122),EM_SETSEL,4,5); SendMessageW(aiPanel,NP3_WM_PANEL_REFRESH,0,0);
            CHARFORMAT2W middleFormat={0}; middleFormat.cbSize=sizeof(middleFormat); SendMessageW(GetDlgItem(aiPanel,122),EM_GETCHARFORMAT,SCF_SELECTION,(LPARAM)&middleFormat);
            _WorkspaceTestCheck(&result,middleFormat.crBackColor==multiColors.field,"multi-hunk theme refresh keeps unchanged context unhighlighted");
            SendMessageW(aiPanel,WM_COMMAND,MAKEWPARAM(115,BN_CLICKED),0);
            _WorkspaceTestCheck(&result,_WorkspaceTestText("new\r\nkeep\r\nthird"),"multi-hunk preview applies exact response bytes only after confirmation");
            SciCall_Undo(); _WorkspaceTestCheck(&result,_WorkspaceTestText("old\r\nkeep\r\nsecond"),"multi-hunk response is one undoable edit");
            SciCall_Undo(); _WorkspaceTestCheck(&result,_WorkspaceTestText("alpha\r\nA") && !IsSaveNeeded(),"multi-hunk fixture restores original saved document");
            char groupedSource[6001]={0}; for(int line=0;line<1200;++line) CopyMemory(groupedSource+line*5,"old\r\n",5);
            UndoTransActionBegin(); SciCall_SetTargetRange(0,SciCall_GetLength()); Sci_ReplaceTargetTestChgHist(6000,groupedSource); EndUndoTransAction();
            SciCall_SetSel(0,6000); SendMessageW(aiPanel,WM_COMMAND,MAKEWPARAM(112,BN_CLICKED),0);
            bool groupedReady=_WorkspaceTestAiReady(); WCHAR groupedStatus[256],expectedGrouped[256];
            GetWindowTextW(GetDlgItem(aiPanel,130),groupedStatus,COUNTOF(groupedStatus)); GetLngString(IDS_MUI_AI_GROUPED,expectedGrouped,COUNTOF(expectedGrouped));
            _WorkspaceTestCheck(&result,groupedReady && !wcscmp(groupedStatus,expectedGrouped) && _WorkspaceTestText(groupedSource),"AI large grouped difference explicitly warns before any source edit");
            SendMessageW(aiPanel,NP3_WM_PANEL_REFRESH,0,0); GetWindowTextW(GetDlgItem(aiPanel,130),groupedStatus,COUNTOF(groupedStatus));
            _WorkspaceTestCheck(&result,!wcscmp(groupedStatus,expectedGrouped),"grouped difference warning survives theme refresh");
            size_t const groupedLength=600000;
            WCHAR* groupedExpected=AllocMem((groupedLength+1)*sizeof(WCHAR),HEAP_ZERO_MEMORY);
            WCHAR* groupedPreview=AllocMem((groupedLength+1)*sizeof(WCHAR),HEAP_ZERO_MEMORY);
            char* groupedResult=AllocMem(groupedLength+1,HEAP_ZERO_MEMORY);
            bool groupedBuffers=groupedExpected && groupedPreview && groupedResult;
            if(groupedBuffers) for(size_t line=0;line<120000;++line) { CopyMemory(groupedExpected+line*5,L"new\r\n",5*sizeof(WCHAR)); CopyMemory(groupedResult+line*5,"new\r\n",5); }
            GETTEXTEX groupedRead={0}; groupedRead.cb=(DWORD)((groupedLength+1)*sizeof(WCHAR)); groupedRead.flags=GT_USECRLF; groupedRead.codepage=1200;
            LRESULT groupedCharacters=groupedBuffers ? SendMessageW(GetDlgItem(aiPanel,122),EM_GETTEXTEX,(WPARAM)&groupedRead,(LPARAM)groupedPreview) : 0;
            _WorkspaceTestCheck(&result,groupedBuffers && groupedCharacters==(LRESULT)groupedLength && !wcscmp(groupedPreview,groupedExpected),"large AI preview retains all 120000 result lines and final newline");
            SendMessageW(aiPanel,WM_COMMAND,MAKEWPARAM(115,BN_CLICKED),0);
            _WorkspaceTestCheck(&result,groupedBuffers && _WorkspaceTestText(groupedResult) && IsSaveNeeded() && !IsWindowEnabled(GetDlgItem(aiPanel,115)),"large AI result applies all exact bytes only after manual confirmation");
            SendMessageW(aiPanel,WM_COMMAND,MAKEWPARAM(115,BN_CLICKED),0);
            _WorkspaceTestCheck(&result,groupedBuffers && _WorkspaceTestText(groupedResult),"large AI response cannot apply a second time");
            SciCall_Undo(); _WorkspaceTestCheck(&result,_WorkspaceTestText(groupedSource),"large AI confirmed replacement restores all original lines with one undo");
            FreeMem(groupedResult); FreeMem(groupedPreview); FreeMem(groupedExpected);
            SciCall_Undo(); _WorkspaceTestCheck(&result,_WorkspaceTestText("alpha\r\nA") && !IsSaveNeeded(),"large grouped preview restores fixture with original undo and saved state");
            SciCall_SetSel(0,5); SetWindowTextW(GetDlgItem(aiPanel,120),L"delay");
            SendMessageW(aiPanel,WM_COMMAND,MAKEWPARAM(112,BN_CLICKED),0);
            _WorkspaceTestCheck(&result,AiPanel_IsBusy(workspace->ai) && !IsWindowEnabled(GetDlgItem(aiPanel,105)),"AI running request disables configuration changes");
            _WorkspaceTestCheck(&result,!IsWindowEnabled(GetDlgItem(aiPanel,125)),"provider preset cannot change drafts while request is running");
            DWORD waitStart=GetTickCount(); while(GetTickCount()-waitStart<200) { MsgWaitForMultipleObjectsEx(0,NULL,20,QS_ALLINPUT,MWMO_INPUTAVAILABLE); MSG message; while(PeekMessageW(&message,NULL,0,0,PM_REMOVE)) { TranslateMessage(&message); DispatchMessageW(&message); } }
            SendMessageW(aiPanel,WM_COMMAND,MAKEWPARAM(114,BN_CLICKED),0);
            _WorkspaceTestCheck(&result,_WorkspaceTestAiReady() && _WorkspaceTestText("alpha\r\nA") && !IsWindowEnabled(GetDlgItem(aiPanel,115)),"AI panel cancel finishes promptly without applying delayed response");
            aiPanel=_WorkspaceTestAiAccounts(aiPanel,aiUrl,&result);
            SendMessageW(aiPanel,WM_COMMAND,MAKEWPARAM(106,BN_CLICKED),0);
            _WorkspaceTestCheck(&result,AiCredential_Load(profile,&key) == ERROR_NOT_FOUND && !key,"AI delete-key button removes only fixture service credential");
            AiCredential_Free(key); AiCredential_Delete(profile); _AiToggle();
        }
    }
    if(GetEnvironmentVariableW(L"NP3_MARKDOWN_CHECK_PREVIEW",NULL,0)) {
        const char* markdown="# 原生 Markdown 图表与公式\n\n```mermaid\nflowchart LR\n A[开始] --> B[结束]\n```\n\n行内公式 $E=mc^2$\n\n$$\\frac{1}{2}+\\sum_{i=1}^{n} i$$\n";
        UndoTransActionBegin(); SciCall_SetTargetRange(0,SciCall_GetLength()); Sci_ReplaceTargetTestChgHist((DocPos)strlen(markdown),markdown); EndUndoTransAction();
        SendWMCommand(Globals.hwndMain,IDT_VIEW_MARKDOWN_PREVIEW); bool ready=_WorkspaceTestPreviewReady(false);
        _WorkspaceTestCheck(&result,MarkdownPreview_IsOpen(Globals.hwndMain) && (SendMessageW(Globals.hwndToolbar,TB_GETSTATE,IDT_VIEW_MARKDOWN_PREVIEW,0)&TBSTATE_CHECKED),"M toolbar command opens preview and updates checked state");
        _WorkspaceTestCheck(&result,ready,"actual WebView2 preview completes offline advanced rendering");
        WCHAR capturePath[32768]; StringCchPrintfW(capturePath,COUNTOF(capturePath),L"%s\\preview.png",folder);
        bool capture=ready && MarkdownPreview_Capture(Globals.hwndMain,capturePath) && _WorkspaceTestPreviewReady(true);
        _WorkspaceTestCheck(&result,capture && MarkdownPreview_IsCaptureSuccessful(Globals.hwndMain) && GetFileAttributesW(capturePath)!=INVALID_FILE_ATTRIBUTES,"actual native preview captures PNG after graph and formula layout");
        char* longMarkdown=AllocMem(65536,HEAP_ZERO_MEMORY);
        if(longMarkdown) for(int paragraph=0;paragraph<300;++paragraph) StringCchCatA(longMarkdown,65536,"Long document paragraph with enough content to verify browser scroll retention after live editing.\n\n");
        SciCall_BeginUndoAction(); SciCall_SetTargetRange(0,SciCall_GetLength()); if(longMarkdown) Sci_ReplaceTargetTestChgHist((DocPos)strlen(longMarkdown),longMarkdown); SciCall_EndUndoAction(); FreeMem(longMarkdown);
        double previewScroll=0;
        _WorkspaceTestCheck(&result,_WorkspaceTestPreviewReady(false) && MarkdownPreview_ScrollTo(Globals.hwndMain,800) && _WorkspaceTestPreviewReady(false) && MarkdownPreview_GetScroll(Globals.hwndMain,&previewScroll) && previewScroll>=799 && previewScroll<=801,"actual WebView long document scrolls to requested position");
        SciCall_BeginUndoAction(); SciCall_AppendText(8,"\nupdated"); SciCall_EndUndoAction();
        _WorkspaceTestCheck(&result,_WorkspaceTestPreviewReady(false) && MarkdownPreview_GetScroll(Globals.hwndMain,&previewScroll) && previewScroll>=799 && previewScroll<=801,"live editor update retains actual long preview scroll after render completion");
        _WorkspaceTestCheck(&result,FileLoad(b,FLF_None,0,0) && _WorkspaceTestPreviewReady(false) && MarkdownPreview_GetScroll(Globals.hwndMain,&previewScroll) && previewScroll==0,"new preview document does not inherit another tab scroll position");
        char* secondMarkdown=AllocMem(32768,HEAP_ZERO_MEMORY); if(secondMarkdown) for(int paragraph=0;paragraph<200;++paragraph) StringCchCatA(secondMarkdown,32768,"Second document independent preview paragraph.\n\n");
        SciCall_BeginUndoAction(); SciCall_SelectAll(); if(secondMarkdown) SciCall_ReplaceSel(secondMarkdown); SciCall_EndUndoAction(); FreeMem(secondMarkdown);
        _WorkspaceTestCheck(&result,_WorkspaceTestPreviewReady(false) && MarkdownPreview_GetScroll(Globals.hwndMain,&previewScroll) && previewScroll==0 && MarkdownPreview_ScrollTo(Globals.hwndMain,1400) && _WorkspaceTestPreviewReady(false),"second long document starts independently and accepts its own scroll position");
        _WorkspaceTestCheck(&result,_WorkspaceSwitch(0) && _WorkspaceTestPreviewReady(false) && MarkdownPreview_GetScroll(Globals.hwndMain,&previewScroll) && previewScroll>=799 && previewScroll<=801,"returning to long document restores its own preview reading position");
        _WorkspaceTestCheck(&result,_WorkspaceSwitch(1) && _WorkspaceTestPreviewReady(false) && MarkdownPreview_GetScroll(Globals.hwndMain,&previewScroll) && previewScroll>=1399 && previewScroll<=1401,"second document restores separate preview position after visiting first tab");
        unsigned long long const reloadVersion=_WorkspaceVersion(), reloadId=workspace->documents[workspace->active].id;
        _WorkspaceTestCheck(&result,FileLoad(b,FLF_Reload|FLF_DontSave,0,0),"reload reads saved source into current tab");
        _WorkspaceTestCheck(&result,workspace->documents[workspace->active].id==reloadId && _WorkspaceVersion()==reloadVersion+1,"reload preserves document identity and invalidates its previous version");
        _WorkspaceTestCheck(&result,_WorkspaceTestText("beta\r\nB"),"reload restores original UTF-16 source bytes in editor");
        _WorkspaceTestCheck(&result,_WorkspaceTestPreviewReady(false) && MarkdownPreview_GetScroll(Globals.hwndMain,&previewScroll) && previewScroll==0,"reload refreshes actual preview and clamps scroll to short document");
        SciCall_SetSavePoint(); _WorkspaceCloseActive();
        _WorkspaceTestCheck(&result,workspace->count==1 && _WorkspaceTestPreviewReady(false) && MarkdownPreview_GetScroll(Globals.hwndMain,&previewScroll) && previewScroll>=799 && previewScroll<=801,"closing other tab restores surviving preview document position");
        SciCall_SetSel(0,4); AI_PANEL_SOURCE previewSource={workspace->documents[workspace->active].id,workspace->documents[workspace->active].version,0,4,0,4};
        AI_PANEL_APPLY previewAi={previewSource,"Long",4,"AI!!",4};
        _WorkspaceTestCheck(&result,_AiApply(&previewAi) && !MarkdownPreview_IsRendered(Globals.hwndMain) && _WorkspaceTestPreviewReady(false) && MarkdownPreview_GetScroll(Globals.hwndMain,&previewScroll) && previewScroll>=799 && previewScroll<=801,"AI quiet application schedules actual preview refresh and retains scroll");
        DocPos previewLength=SciCall_GetLength(); char* previewOriginal=AllocMem((size_t)previewLength+1,HEAP_ZERO_MEMORY); if(previewOriginal) SciCall_GetText(previewLength+1,previewOriginal);
        BATCH_CHANGE previewChange={workspace->documents[workspace->active].id,workspace->documents[workspace->active].version,previewOriginal,(size_t)previewLength,"# Batch preview\n\nconfirmed.\n",27}; BATCH_APPLY previewBatch={&previewChange,1};
        _WorkspaceTestCheck(&result,previewOriginal && _BatchApply(&previewBatch)==1 && workspace->documents[workspace->active].version==previewChange.version+1 && !MarkdownPreview_IsRendered(Globals.hwndMain) && _WorkspaceTestPreviewReady(false) && MarkdownPreview_GetScroll(Globals.hwndMain,&previewScroll) && previewScroll==0,"single-document batch quiet application advances version and refreshes preview");
        FreeMem(previewOriginal); SciCall_Undo(); SciCall_Undo();
        SciCall_Undo(); SciCall_Undo();
        _WorkspaceTestCheck(&result,_WorkspaceTestPreviewReady(false) && MarkdownPreview_GetScroll(Globals.hwndMain,&previewScroll) && previewScroll==0,"shortened preview clamps restored scroll to actual document extent");
        MarkdownPreview_Toggle(Globals.hwndMain); SciCall_Undo();
        _WorkspaceTestCheck(&result,!MarkdownPreview_IsOpen(Globals.hwndMain) && _WorkspaceTestText("alpha\r\nA") && !IsSaveNeeded(),"advanced preview and capture preserve source undo and saved state");
    }
    // Batch UI commands use only the isolated documents created by this runner.
    _WorkspaceTestCheck(&result, FileLoad(b, FLF_None, 0, 0) && workspace->count == 2 && _WorkspaceTestText("beta\r\nB"), "batch fixture opens second saved document");
    unsigned long long batchAId = workspace->documents[0].id, batchBId = workspace->documents[1].id;
    _BatchToggle(); HWND batchPanel = FindWindowExW(Globals.hwndMain, NULL, L"NP3BatchPanel", NULL);
    _WorkspaceTestCheck(&result, batchPanel && BatchPanel_IsOpen(workspace->batch) && GetDlgItem(batchPanel,120) && GetDlgItem(batchPanel,121) && GetDlgItem(batchPanel,122), "batch native document list and result controls exist");
    SetWindowTextW(GetDlgItem(batchPanel,102),L"a"); SetWindowTextW(GetDlgItem(batchPanel,103),L"X");
    unsigned long long batchVersion = workspace->documents[1].version;
    SendMessageW(batchPanel,WM_COMMAND,MAKEWPARAM(110,BN_CLICKED),0);
    _WorkspaceTestCheck(&result,_WorkspaceTestBatchReady() && ListView_GetItemCount(GetDlgItem(batchPanel,120))==2,"batch background preview publishes both document rows");
    _WorkspaceTestCheck(&result,_WorkspaceTestText("beta\r\nB") && workspace->active==1 && workspace->documents[1].version==batchVersion && !IsSaveNeeded(),"batch preview preserves active document, text, version and saved state");
    WCHAR batchOriginal[128]={0},batchProposed[128]={0};
    GetWindowTextW(GetDlgItem(batchPanel,121),batchOriginal,COUNTOF(batchOriginal)); GetWindowTextW(GetDlgItem(batchPanel,122),batchProposed,COUNTOF(batchProposed));
    _WorkspaceTestCheck(&result,wcsstr(batchOriginal,L"alpha") && wcsstr(batchProposed,L"XlphX"),"batch actual result controls show original and transformed text");
    _WorkspaceTestCheck(&result,ListView_GetCheckState(GetDlgItem(batchPanel,120),0) && ListView_GetCheckState(GetDlgItem(batchPanel,120),1),"batch changed documents are checked for explicit confirmation");
    ListView_SetCheckState(GetDlgItem(batchPanel,120),0,FALSE);
    SendMessageW(batchPanel,WM_COMMAND,MAKEWPARAM(112,BN_CLICKED),0);
    _WorkspaceTestCheck(&result,_WorkspaceTestText("betX\r\nB") && IsSaveNeeded() && workspace->active==1,"batch applies only checked document and restores active tab");
    _WorkspaceSwitch(0); _WorkspaceTestCheck(&result,_WorkspaceTestText("alpha\r\nA") && !IsSaveNeeded(),"batch unchecked document remains unchanged"); _WorkspaceSwitch(1);
    SendMessageW(batchPanel,WM_COMMAND,MAKEWPARAM(112,BN_CLICKED),0);
    _WorkspaceTestCheck(&result,!IsWindowEnabled(GetDlgItem(batchPanel,112)) && _WorkspaceTestText("betX\r\nB"),"batch UI refuses repeated application");
    SciCall_Undo(); _WorkspaceTestCheck(&result,_WorkspaceTestText("beta\r\nB") && !IsSaveNeeded(),"batch replacement is one undo and restores save point");
    SendMessageW(batchPanel,WM_COMMAND,MAKEWPARAM(110,BN_CLICKED),0); _WorkspaceTestBatchReady();
    _WorkspaceSwitch(0); SciCall_AppendText(1,"~"); _WorkspaceSwitch(1);
    SendMessageW(batchPanel,WM_COMMAND,MAKEWPARAM(112,BN_CLICKED),0);
    _WorkspaceTestCheck(&result,_WorkspaceTestText("beta\r\nB") && !IsSaveNeeded(),"batch stale inactive source rejects whole transaction before editing other documents");
    _WorkspaceSwitch(0); _WorkspaceTestCheck(&result,_WorkspaceTestText("alpha\r\nA~"),"batch preserves independent edit made after preview"); SciCall_Undo(); _WorkspaceSwitch(1);
    SendMessageW(batchPanel,WM_COMMAND,MAKEWPARAM(110,BN_CLICKED),0); _WorkspaceTestBatchReady();
    SciCall_SetReadOnly(TRUE); SendMessageW(batchPanel,WM_COMMAND,MAKEWPARAM(112,BN_CLICKED),0);
    _WorkspaceTestCheck(&result,_WorkspaceTestText("beta\r\nB") && !IsSaveNeeded(),"batch apply rechecks current read-only status for entire selection"); SciCall_SetReadOnly(FALSE);
    SetWindowTextW(GetDlgItem(batchPanel,103),L"Y");
    _WorkspaceTestCheck(&result,!IsWindowEnabled(GetDlgItem(batchPanel,112)),"changing batch options invalidates earlier confirmation");
    SendMessageW(batchPanel,WM_COMMAND,MAKEWPARAM(110,BN_CLICKED),0); _WorkspaceTestBatchReady();
    unsigned long long batchVersionBeforeA=workspace->documents[0].version,batchVersionBeforeB=workspace->documents[1].version;
    SendMessageW(batchPanel,WM_COMMAND,MAKEWPARAM(112,BN_CLICKED),0);
    _WorkspaceTestCheck(&result,_WorkspaceTestText("betY\r\nB") && IsSaveNeeded(),"batch confirms replacement in second document");
    _WorkspaceTestCheck(&result,workspace->documents[0].version==batchVersionBeforeA+1 && workspace->documents[1].version==batchVersionBeforeB+1,"batch quiet transactions advance each changed document version exactly once");
    _WorkspaceSwitch(0); _WorkspaceTestCheck(&result,_WorkspaceTestText("YlphY\r\nA") && IsSaveNeeded(),"batch confirms replacement in first document");
    SciCall_Undo(); _WorkspaceTestCheck(&result,_WorkspaceTestText("alpha\r\nA") && !IsSaveNeeded(),"batch first document has independent single undo"); _WorkspaceSwitch(1);
    SciCall_Undo(); _WorkspaceTestCheck(&result,_WorkspaceTestText("beta\r\nB") && !IsSaveNeeded(),"batch second document has independent single undo");
    BATCH_CHANGE rollbackChanges[2]={{batchAId,workspace->documents[0].version,"alpha\r\nA",8,"changed A",9},{batchBId,workspace->documents[1].version,"beta\r\nB",7,"changed B",9}};
    BATCH_APPLY rollbackRequest={rollbackChanges,2};
    _WorkspaceTestCheck(&result,_BatchApplyTransaction(&rollbackRequest,1)==-1 && _WorkspaceTestText("beta\r\nB") && workspace->active==1 && !IsSaveNeeded(),"batch injected mid-transaction failure rolls back and restores active document");
    _WorkspaceSwitch(0); _WorkspaceTestCheck(&result,_WorkspaceTestText("alpha\r\nA") && !IsSaveNeeded(),"batch rollback independently restores original bytes and save point"); _WorkspaceSwitch(1);
    BATCH_CHANGE emptyChange={batchBId,workspace->documents[1].version,"beta\r\nB",7,"",0}; BATCH_APPLY emptyRequest={&emptyChange,1};
    _WorkspaceTestCheck(&result,_BatchApply(&emptyRequest)==1 && SciCall_GetLength()==0 && IsSaveNeeded(),"batch permits confirmed empty replacement without truncation ambiguity");
    SciCall_Undo(); _WorkspaceTestCheck(&result,_WorkspaceTestText("beta\r\nB") && !IsSaveNeeded(),"batch empty replacement restores all bytes with one undo");
    SendMessageW(batchPanel,WM_COMMAND,MAKEWPARAM(110,BN_CLICKED),0); _WorkspaceTestBatchReady();
    _WorkspaceTestCheck(&result,_WorkspaceCloseActive() && workspace->count==1,"batch source tab may close after preview");
    SendMessageW(batchPanel,WM_COMMAND,MAKEWPARAM(112,BN_CLICKED),0);
    _WorkspaceTestCheck(&result,_WorkspaceTestText("alpha\r\nA") && !IsSaveNeeded(),"batch closed source rejects whole application without editing remaining document");
    _WorkspaceTestCheck(&result,FileLoad(b,FLF_None,0,0) && _WorkspaceTestText("beta\r\nB"),"batch fixture reopens unchanged saved source");
    SendMessageW(batchPanel,WM_COMMAND,MAKEWPARAM(110,BN_CLICKED),0); SendMessageW(batchPanel,WM_COMMAND,MAKEWPARAM(111,BN_CLICKED),0);
    _WorkspaceTestCheck(&result,_WorkspaceTestBatchReady() && _WorkspaceTestText("beta\r\nB") && !IsWindowEnabled(GetDlgItem(batchPanel,112)),"batch cancellation discards proposals without applying");
    _BatchToggle(); _BatchToggle();
    _WorkspaceTestCheck(&result,BatchPanel_IsOpen(workspace->batch) && _WorkspaceTestText("beta\r\nB"),"batch panel closes and reopens without changing editor");
    _BatchToggle(); _WorkspaceCloseActive();
    _WorkspaceTestCheck(&result,workspace->count==1 && _WorkspaceTestText("alpha\r\nA") && !IsSaveNeeded(),"batch regression preserves original saved workspace");
    _BatchToggle(); batchPanel=FindWindowExW(Globals.hwndMain,NULL,L"NP3BatchPanel",NULL);
    _WorkspaceTestCheck(&result,_WorkspaceTestBatchReady() && GetDlgItem(batchPanel,113) && IsWindowEnabled(GetDlgItem(batchPanel,113)),"batch native file chooser button is available");
    SciCall_AppendText(1,"~");
    WCHAR missingBatchFile[32768]; StringCchPrintfW(missingBatchFile,COUNTOF(missingBatchFile),L"%s\\batch-missing-file.txt",folder);
    const WCHAR* missingBatchPaths[]={Path_Get(b),missingBatchFile}; BATCH_FILES batchFiles={missingBatchPaths,2};
    _WorkspaceTestCheck(&result,_BatchOpenFiles(&batchFiles)==0 && workspace->count==1 && _WorkspaceTestText("alpha\r\nA~") && IsSaveNeeded(),"batch selected missing path rejects whole admission before opening valid files");
    const WCHAR* directoryBatchPaths[]={folder}; batchFiles.paths=directoryBatchPaths; batchFiles.count=1;
    _WorkspaceTestCheck(&result,_BatchOpenFiles(&batchFiles)==0 && workspace->count==1,"batch directory selection is rejected without adding a tab");
    batchFiles.count=65; _WorkspaceTestCheck(&result,_BatchOpenFiles(&batchFiles)==0 && workspace->count==1,"batch oversized file selection rejected before reading path array");
    const WCHAR* duplicateBatchPaths[]={Path_Get(b),Path_Get(b),Path_Get(a)}; batchFiles.paths=duplicateBatchPaths; batchFiles.count=3;
    _WorkspaceTestCheck(&result,_BatchOpenFiles(&batchFiles)==1 && workspace->count==2 && workspace->active==0 && _WorkspaceTestText("alpha\r\nA~"),"batch file admission deduplicates selected files and restores original active tab");
    _WorkspaceTestCheck(&result,IsSaveNeeded() && SciCall_CanUndo(),"batch choosing an already open file preserves unsaved edits and undo");
    bool selectedBatchTab=workspace->count==2 && _WorkspaceSwitch(1);
    _WorkspaceTestCheck(&result,selectedBatchTab && _WorkspaceTestText("beta\r\nB") && Encoding_GetCurrent()==CPI_UNICODEBOM && !IsSaveNeeded(),"batch selected UTF16 file retains native decoding and encoding");
    if (selectedBatchTab) _WorkspaceCloseActive(); SciCall_Undo();
    _WorkspaceTestCheck(&result,_WorkspaceTestText("alpha\r\nA") && !IsSaveNeeded(),"batch file admission leaves existing text edit independently undoable");
    PANEL_COLORS themeColors={0}; _PanelColors(&themeColors);
    HWND batchList=GetDlgItem(batchPanel,120);
    _WorkspaceTestCheck(&result,ListView_GetBkColor(batchList)==themeColors.field && ListView_GetTextColor(batchList)==themeColors.text,"batch list uses current panel palette");
    _WorkspaceTestCheck(&result,_WorkspaceTestPanelBrush(batchPanel,GetDlgItem(batchPanel,121),themeColors.field,themeColors.text),"batch read-only preview brush and foreground use current palette");
    SetWindowTextW(GetDlgItem(batchPanel,102),L"a"); SetWindowTextW(GetDlgItem(batchPanel,103),L"主题");
    SendMessageW(batchPanel,WM_COMMAND,MAKEWPARAM(110,BN_CLICKED),0); _WorkspaceTestBatchReady();
    CHARRANGE batchThemeSelection={0}; batchThemeSelection.cpMin=1; batchThemeSelection.cpMax=3;
    SendMessageW(GetDlgItem(batchPanel,121),EM_EXSETSEL,0,(LPARAM)&batchThemeSelection);
    unsigned long long batchThemeVersion=workspace->documents[0].version;
    MsgThemeChanged(Globals.hwndMain,0,0);
    for(int themeIndex=0;themeIndex<5;++themeIndex) PanelTheme_RefreshChildren(Globals.hwndMain);
    DWORD themeGdiBefore=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);
    for(int themeIndex=0;themeIndex<40;++themeIndex) PanelTheme_RefreshChildren(Globals.hwndMain);
    DWORD themeGdiAfter=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);
    CHARRANGE batchThemeAfter={0}; SendMessageW(GetDlgItem(batchPanel,121),EM_EXGETSEL,0,(LPARAM)&batchThemeAfter);
    _WorkspaceTestCheck(&result,ListView_GetItemCount(batchList)==1 && ListView_GetCheckState(batchList,0) && IsWindowEnabled(GetDlgItem(batchPanel,112)) && batchThemeAfter.cpMin==1 && batchThemeAfter.cpMax==3,"batch theme refresh retains proposals, check state, confirmation and selection");
    _WorkspaceTestCheck(&result,themeGdiBefore && themeGdiAfter && themeGdiAfter<=themeGdiBefore+2,"repeated panel theme refresh has bounded GDI object count");
    HFONT panelFont=(HFONT)SendMessageW(GetDlgItem(batchPanel,113),WM_GETFONT,0,0); LOGFONTW panelFontInfo={0};
    NONCLIENTMETRICSW panelMetrics={0}; panelMetrics.cbSize=sizeof(panelMetrics);
    bool panelMetricsReady=SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS,sizeof(panelMetrics),&panelMetrics,0,GetDpiForWindow(Globals.hwndMain));
    _WorkspaceTestCheck(&result,panelFont && panelFont!=(HFONT)GetStockObject(DEFAULT_GUI_FONT) && GetObjectW(panelFont,sizeof(panelFontInfo),&panelFontInfo)==sizeof(panelFontInfo) && panelMetricsReady && panelFontInfo.lfHeight==panelMetrics.lfMessageFont.lfHeight,"panel controls use an owned DPI message font");
    char themeNote[160]; DWORD themeWritten=0;
    StringCchPrintfA(themeNote,COUNTOF(themeNote),"THEME dark=%i panel=%lu field=%lu text=%lu GDI_before=%lu GDI_after=%lu DPI=%u\r\n",themeColors.dark,(unsigned long)themeColors.panel,(unsigned long)themeColors.field,(unsigned long)themeColors.text,(unsigned long)themeGdiBefore,(unsigned long)themeGdiAfter,GetDpiForWindow(Globals.hwndMain));
    WriteFile(result.log,themeNote,(DWORD)strlen(themeNote),&themeWritten,NULL);
    _WorkspaceTestCheck(&result,workspace->documents[0].version==batchThemeVersion && _WorkspaceTestText("alpha\r\nA") && !IsSaveNeeded(),"repeated theme refresh preserves editor version text and saved state");
    _LogFilterToggle(); HWND themedLog=FindWindowExW(Globals.hwndMain,NULL,L"NP3LogFilterPanel",NULL);
    _WorkspaceTestCheck(&result,ListView_GetBkColor(GetDlgItem(themedLog,107))==themeColors.field && ListView_GetTextColor(GetDlgItem(themedLog,107))==themeColors.text,"log filter list receives shared panel theme");
    _LogFilterToggle();
    _FileTreeToggle(); HWND themedTree=FindWindowExW(Globals.hwndMain,NULL,L"NP3WorkspaceFileTree",NULL); HWND themedTreeView=FindWindowExW(themedTree,NULL,WC_TREEVIEWW,NULL);
    _WorkspaceTestCheck(&result,_WorkspaceTestTreeReady() && TreeView_GetBkColor(themedTreeView)==themeColors.field && TreeView_GetTextColor(themedTreeView)==themeColors.text,"file tree receives shared panel theme without losing directory loading");
#ifdef D_NP3_WIN10_DARK_MODE
    if(IsDarkModeSupported() && !IsHighContrast()) {
        WIN_DISPL_MODE savedTheme=Settings.WinThemeDarkMode;
        SetDarkMode(true);
        bool systemDark=ShouldAppsUseDarkModeEx();
        SetDarkModePreference(IsSettingDarkMode(),savedTheme==WINDSPMOD_DARK);
        _LogFilterToggle(); themedLog=FindWindowExW(Globals.hwndMain,NULL,L"NP3LogFilterPanel",NULL);
        bool openedThemeAi=!AiPanel_IsOpen(_WorkspaceAi()); if(openedThemeAi) _AiToggle();
        HWND themedAi=FindWindowExW(Globals.hwndMain,NULL,L"NP3AiPanel",NULL);
        bool testPreviewTheme=GetEnvironmentVariableW(L"NP3_MARKDOWN_CHECK_PREVIEW",NULL,0)!=0;
        if(testPreviewTheme && !MarkdownPreview_IsOpen(Globals.hwndMain)) MarkdownPreview_Toggle(Globals.hwndMain);
        for(int mode=0;mode<3;++mode) {
            Settings.WinThemeDarkMode=mode==0?WINDSPMOD_DARK:mode==1?WINDSPMOD_LIGHT:WINDSPMOD_AUTO;
            // Reset app preference before asking for the OS preference in automatic mode.
            SetDarkMode(true);
            SetDarkModePreference(IsSettingDarkMode(),Settings.WinThemeDarkMode==WINDSPMOD_DARK);
            MsgThemeChanged(Globals.hwndMain,0,0);
            PANEL_COLORS current={0}; _PanelColors(&current);
            bool expectedDark=mode==0 || (mode==2 && systemDark);
            _WorkspaceTestCheck(&result,_WorkspaceTestToolbarGlyph(TB_GETIMAGELIST,GetModeTextColor(UseDarkMode())) && _WorkspaceTestToolbarGlyph(TB_GETHOTIMAGELIST,GetModeTextColor(UseDarkMode())) && _WorkspaceTestToolbarGlyph(TB_GETDISABLEDIMAGELIST,current.disabled),"theme transition recreates M image pixels in current toolbar foreground colors");
            _WorkspaceTestCheck(&result,IsDarkModeSupported() && !!UseDarkMode()==expectedDark && !!current.dark==expectedDark,mode==0?"explicit dark theme works independently of OS light preference":mode==1?"explicit light theme keeps dark capability available":"automatic theme follows OS preference after explicit modes");
            _WorkspaceTestCheck(&result,ListView_GetBkColor(batchList)==current.field && ListView_GetTextColor(batchList)==current.text && ListView_GetBkColor(GetDlgItem(themedLog,107))==current.field && TreeView_GetBkColor(themedTreeView)==current.field && TreeView_GetTextColor(themedTreeView)==current.text && _WorkspaceTestPanelBrush(themedAi,GetDlgItem(themedAi,121),current.field,current.text),"all four extension panels refresh actual palette across theme modes");
            _WorkspaceTestCheck(&result,workspace->documents[0].version==batchThemeVersion && _WorkspaceTestText("alpha\r\nA") && !IsSaveNeeded() && ListView_GetItemCount(batchList)==1 && ListView_GetCheckState(batchList,0),"theme transitions retain saved editor and confirmed batch proposal");
            if(testPreviewTheme) {
                _WorkspaceTestCheck(&result,_WorkspaceTestPreviewReady(false) && MarkdownPreview_IsThemeApplied(Globals.hwndMain),"WebView DOM background foreground and color scheme follow application theme");
                HWND previewWindow=FindWindowExW(Globals.hwndMain,NULL,L"NP3MarkdownPreview",NULL);
                int previewGripWidth=MulDiv(6,GetDpiForWindow(previewWindow),96);
                BITMAPINFO previewPaintInfo={0}; previewPaintInfo.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
                previewPaintInfo.bmiHeader.biWidth=previewGripWidth+4; previewPaintInfo.bmiHeader.biHeight=-4;
                previewPaintInfo.bmiHeader.biPlanes=1; previewPaintInfo.bmiHeader.biBitCount=32; previewPaintInfo.bmiHeader.biCompression=BI_RGB;
                HDC previewPaintDc=CreateCompatibleDC(NULL); void* previewPaintPixels=NULL;
                HBITMAP previewPaintBitmap=previewPaintDc?CreateDIBSection(previewPaintDc,&previewPaintInfo,DIB_RGB_COLORS,&previewPaintPixels,NULL,0):NULL;
                HGDIOBJ previewPaintOld=previewPaintBitmap?SelectObject(previewPaintDc,previewPaintBitmap):NULL;
                if(previewPaintBitmap) SendMessageW(previewWindow,WM_PRINTCLIENT,(WPARAM)previewPaintDc,PRF_CLIENT);
                _WorkspaceTestCheck(&result,previewPaintBitmap && GetPixel(previewPaintDc,0,1)==current.panel && GetPixel(previewPaintDc,previewGripWidth/2,1)==current.disabled && GetPixel(previewPaintDc,previewGripWidth+1,1)==current.field,"Markdown divider background center line and host field draw actual application theme pixels");
                DWORD previewPaintGdi=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);
                if(previewPaintBitmap) for(int paintRepeat=0;paintRepeat<40;++paintRepeat) SendMessageW(previewWindow,WM_ERASEBKGND,(WPARAM)previewPaintDc,0);
                _WorkspaceTestCheck(&result,previewPaintBitmap && GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS)==previewPaintGdi && workspace->documents[0].version==batchThemeVersion && _WorkspaceTestText("alpha\r\nA") && !IsSaveNeeded(),"repeated Markdown host painting retains GDI resources and original saved document");
                if(previewPaintOld) SelectObject(previewPaintDc,previewPaintOld);
                if(previewPaintBitmap) DeleteObject(previewPaintBitmap); if(previewPaintDc) DeleteDC(previewPaintDc);
            }
            SendMessageW(Globals.hwndMain,WM_SETTINGCHANGE,0,(LPARAM)L"ImmersiveColorSet");
            PANEL_COLORS afterSetting={0}; _PanelColors(&afterSetting);
            _WorkspaceTestCheck(&result,!!afterSetting.dark==expectedDark && ListView_GetBkColor(batchList)==afterSetting.field && TreeView_GetBkColor(themedTreeView)==afterSetting.field && workspace->documents[0].version==batchThemeVersion && _WorkspaceTestText("alpha\r\nA"),"system color notification refreshes panels while preserving chosen theme and editor");
        }
        Settings.WinThemeDarkMode=savedTheme; SetDarkMode(true); SetDarkModePreference(IsSettingDarkMode(),savedTheme==WINDSPMOD_DARK); MsgThemeChanged(Globals.hwndMain,0,0);
        if(testPreviewTheme && MarkdownPreview_IsOpen(Globals.hwndMain)) MarkdownPreview_Toggle(Globals.hwndMain);
        if(openedThemeAi) _AiToggle(); _LogFilterToggle();
    }
#endif
    _FileTreeToggle(); _BatchToggle();
    Path_Release(a); Path_Release(b);
    char summary[128]; DWORD written;
    StringCchPrintfA(summary, COUNTOF(summary), "RESULT checks=%i failures=%i\r\n", result.checks, result.failures);
    WriteFile(result.log, summary, (DWORD)strlen(summary), &written, NULL);
    CloseHandle(result.log);
    _WorkspaceDispose(); DestroyWindow(Globals.hwndMain);
}
