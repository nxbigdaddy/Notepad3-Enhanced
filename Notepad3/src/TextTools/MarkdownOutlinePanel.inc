typedef struct OUTLINE_STATE { MD_OUTLINE_RESULT result; unsigned long long id,version; } OUTLINE_STATE;
static HWND _outlineWindow=NULL;
static void _OutlineRefresh(HWND window,OUTLINE_STATE* state)
{
    MarkdownOutline_Free(&state->result);SendDlgItemMessageW(window,101,LB_RESETCONTENT,0,0);
    DOCUMENT_WORKSPACE* workspace=_Workspace();DocPos length=SciCall_GetLength();
    if(!workspace || length<0 || length>1024*1024){SetDlgItemTextW(window,104,L"大纲支持当前文档最多1MiB。");return;}
    state->id=workspace->documents[workspace->active].id;state->version=workspace->documents[workspace->active].version;
    int status=MarkdownOutline_Parse(SciCall_GetRangePointer(0,length),(size_t)length,&state->result);
    if(status){SetDlgItemTextW(window,104,L"大纲解析失败：编码或大小限制。");return;}
    for(size_t index=0;index<state->result.count;++index){WCHAR label[4120],title[4097];MD_OUTLINE_ITEM* item=&state->result.items[index];MultiByteToWideChar(CP_UTF8,0,item->title,-1,title,COUNTOF(title));StringCchPrintfW(label,COUNTOF(label),L"%*s%u  %s",(int)((item->level-1)*2),L"",(unsigned)(item->line+1),title);SendDlgItemMessageW(window,101,LB_ADDSTRING,0,(LPARAM)label);}
    SetDlgItemTextW(window,104,L"双击或点击定位；编辑/切换文档后请刷新。");
}
static LRESULT CALLBACK _OutlineProc(HWND window,UINT message,WPARAM wParam,LPARAM lParam)
{
    OUTLINE_STATE* state=(OUTLINE_STATE*)GetWindowLongPtrW(window,GWLP_USERDATA);
    if(message==WM_SIZE){int width=LOWORD(lParam),height=HIWORD(lParam);MoveWindow(GetDlgItem(window,101),0,30,width,max_i(0,height-60),TRUE);MoveWindow(GetDlgItem(window,102),0,0,width/2,28,TRUE);MoveWindow(GetDlgItem(window,103),width/2,0,width/2,28,TRUE);MoveWindow(GetDlgItem(window,104),0,max_i(30,height-28),width,28,TRUE);return 0;}
    if(message==WM_COMMAND && state){int id=LOWORD(wParam);if(id==102){_OutlineRefresh(window,state);return 0;}
        if(id==103 || (id==101 && HIWORD(wParam)==LBN_DBLCLK)){
            DOCUMENT_WORKSPACE* workspace=_Workspace();if(!workspace || workspace->documents[workspace->active].id!=state->id || workspace->documents[workspace->active].version!=state->version){SetDlgItemTextW(window,104,L"文档已变化，请刷新大纲后定位。");return 0;}
            LRESULT selected=SendDlgItemMessageW(window,101,LB_GETCURSEL,0,0);if(selected>=0 && (size_t)selected<state->result.count){SciCall_GotoPos((DocPos)state->result.items[selected].offset);SetFocus(Globals.hwndEdit);}return 0;}}
    if(message==WM_NCDESTROY){if(state){MarkdownOutline_Free(&state->result);free(state);}SetWindowLongPtrW(window,GWLP_USERDATA,0);_outlineWindow=NULL;}
    return DefWindowProcW(window,message,wParam,lParam);
}
static HWND _OutlineShow(HWND parent)
{
    if(IsWindow(_outlineWindow)){ShowWindow(_outlineWindow,SW_SHOW);SetForegroundWindow(_outlineWindow);return _outlineWindow;}
    WNDCLASSW type={0};type.lpfnWndProc=_OutlineProc;type.hInstance=GetModuleHandleW(NULL);type.lpszClassName=L"NP3MarkdownOutline";type.hbrBackground=(HBRUSH)(COLOR_WINDOW+1);RegisterClassW(&type);
    HWND window=CreateWindowExW(WS_EX_TOOLWINDOW,type.lpszClassName,L"Markdown 大纲",WS_OVERLAPPEDWINDOW, CW_USEDEFAULT,CW_USEDEFAULT,420,520,parent,NULL,type.hInstance,NULL);if(!window)return NULL;
    OUTLINE_STATE* state=(OUTLINE_STATE*)calloc(1,sizeof(OUTLINE_STATE));if(!state){DestroyWindow(window);return NULL;}SetWindowLongPtrW(window,GWLP_USERDATA,(LONG_PTR)state);_outlineWindow=window;
    CreateWindowExW(WS_EX_CLIENTEDGE,L"LISTBOX",L"",WS_CHILD|WS_VISIBLE|WS_VSCROLL|WS_TABSTOP|LBS_NOTIFY,0,30,400,400,window,(HMENU)101,type.hInstance,NULL);
    CreateWindowExW(0,L"BUTTON",L"刷新",WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,0,0,200,28,window,(HMENU)102,type.hInstance,NULL);CreateWindowExW(0,L"BUTTON",L"定位",WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,200,0,200,28,window,(HMENU)103,type.hInstance,NULL);CreateWindowExW(0,L"STATIC",L"",WS_CHILD|WS_VISIBLE,0,430,400,28,window,(HMENU)104,type.hInstance,NULL);
    for(int id=101;id<=104;++id)SendDlgItemMessageW(window,id,WM_SETFONT,(WPARAM)GetStockObject(DEFAULT_GUI_FONT),TRUE);
    _OutlineRefresh(window,state);RECT rect;GetClientRect(window,&rect);SendMessageW(window,WM_SIZE,0,MAKELPARAM(rect.right,rect.bottom));ShowWindow(window,SW_SHOW);return window;
}
