#include "HistoryPanel.h"
#include "HistoryCore.h"
#include "HistoryService.h"
#include "FileSearchCore.h"
#include "FileTree.h"
#include "../UI/PanelTheme.h"
#include <commctrl.h>
#include <strsafe.h>
#include <shlwapi.h>
#include <algorithm>
#include <atomic>
#include <memory>
#include <thread>
namespace {
constexpr wchar_t ClassName[]=L"NP3History";
struct Row { np3::HistoryEntry entry; bool missing=false; };
struct Job { std::atomic_bool done{false}; std::vector<Row> rows; DWORD error=0; };
struct Panel {
    HWND window=nullptr,parent=nullptr;
    std::wstring store,display;
    std::vector<Row> rows;
    std::vector<size_t> visible;
    std::shared_ptr<Job> job;
    np3::PanelTheme theme;
    int sortColumn=2; bool ascending=false;
    ULONGLONG clearUntil=0;
};
Panel* State(HWND window){return reinterpret_cast<Panel*>(GetWindowLongPtrW(window,GWLP_USERDATA));}
int Scale(HWND window,int value){return MulDiv(value,GetDpiForWindow(window),96);}
HWND Child(HWND window,const wchar_t* cls,const wchar_t* text,DWORD style,int id,int x,int y,int width,int height){return CreateWindowExW(0,cls,text,WS_CHILD|WS_VISIBLE|WS_TABSTOP|style,Scale(window,x),Scale(window,y),Scale(window,width),Scale(window,height),window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),GetModuleHandleW(nullptr),nullptr);}
std::wstring Text(HWND window,int id){wchar_t value[4097]{};GetDlgItemTextW(window,id,value,4097);return value;}
bool Filter(Panel* s){
    std::vector<std::wstring> extensions;
    if(!np3::ParseSearchExtensions(Text(s->window,102),extensions)){SetDlgItemTextW(s->window,107,L"类型格式无效，请使用 *.md;*.txt 或 *。已有列表保留。");return false;}
    std::wstring query=Text(s->window,101);s->visible.clear();
    for(size_t i=0;i<s->rows.size();++i){
        const auto& path=s->rows[i].entry.path;std::wstring extension=PathFindExtensionW(path.c_str());
        bool type=extensions.empty();for(const auto& allowed:extensions)if(CompareStringOrdinal(extension.c_str(),-1,allowed.c_str(),-1,TRUE)==CSTR_EQUAL)type=true;
        if(type && (query.empty() || StrStrIW(path.c_str(),query.c_str())))s->visible.push_back(i);
    }
    std::stable_sort(s->visible.begin(),s->visible.end(),[&](size_t a,size_t b){
        const auto& x=s->rows[a].entry;const auto& y=s->rows[b].entry;int order=0;
        if(x.pinned!=y.pinned)return x.pinned;
        if(s->sortColumn==2)order=CompareFileTime(&x.opened,&y.opened);
        else if(s->sortColumn==3)order=x.count<y.count?-1:x.count>y.count?1:0;
        else order=CompareStringOrdinal(x.path.c_str(),-1,y.path.c_str(),-1,TRUE)-CSTR_EQUAL;
        return s->ascending?order<0:order>0;
    });
    // A virtual row number can refer to a different file after sorting/filtering.
    ListView_SetItemState(GetDlgItem(s->window,106),-1,0,LVIS_SELECTED|LVIS_FOCUSED);
    ListView_SetItemCountEx(GetDlgItem(s->window,106),static_cast<int>(s->visible.size()),LVSICF_NOSCROLL);InvalidateRect(GetDlgItem(s->window,106),nullptr,TRUE);
    return true;
}
void Refresh(Panel* s,const np3::HistoryChange* change=nullptr){
    if(s->job)return;
    auto job=std::make_shared<Job>();std::wstring store=s->store;
    HISTORY_STATE settings{};SendMessageW(s->parent,NP3_WM_HISTORY_STATE,0,reinterpret_cast<LPARAM>(&settings));
    bool mutate=change!=nullptr;np3::HistoryChange operation;if(change)operation=*change;
    SetDlgItemTextW(s->window,107,mutate?L"正在更新历史…":L"正在读取历史…");
    std::thread([job,store,mutate,operation,maximum=settings.maximum]{
        try{
            if(!HistoryService_Flush(5000))job->error=ERROR_TIMEOUT;
            else {
                std::vector<np3::HistoryEntry> entries;
                bool loaded=mutate?np3::HistoryUpdate(store,operation,static_cast<size_t>(maximum),entries,job->error):np3::HistoryRead(store,entries,job->error);
                if(loaded)for(auto& entry:entries){DWORD attrs=GetFileAttributesW(entry.path.c_str());job->rows.push_back({std::move(entry),attrs==INVALID_FILE_ATTRIBUTES || !!(attrs&FILE_ATTRIBUTE_DIRECTORY)});}
            }
        }catch(...){job->error=ERROR_NOT_ENOUGH_MEMORY;}
        job->done=true;
    }).detach();s->job=job;
    for(int id:{104,110,111,112,115})EnableWindow(GetDlgItem(s->window,id),FALSE);
}
void Layout(Panel* s){RECT r{};GetClientRect(s->window,&r);int m=Scale(s->window,12),width=(std::max)(0,static_cast<int>(r.right)-2*m);
    MoveWindow(GetDlgItem(s->window,102),m,Scale(s->window,38),Scale(s->window,144),Scale(s->window,26),TRUE);
    MoveWindow(GetDlgItem(s->window,101),m+Scale(s->window,156),Scale(s->window,38),(std::max)(0,width-Scale(s->window,156)),Scale(s->window,26),TRUE);
    MoveWindow(GetDlgItem(s->window,106),m,Scale(s->window,142),width,(std::max)(0,static_cast<int>(r.bottom)-Scale(s->window,178)),TRUE);
    MoveWindow(GetDlgItem(s->window,107),m,r.bottom-Scale(s->window,28),width,Scale(s->window,22),TRUE);
}
void CancelClear(Panel* s){s->clearUntil=0;SetDlgItemTextW(s->window,112,L"清空历史");ShowWindow(GetDlgItem(s->window,116),SW_HIDE);}
void Clear(Panel* s){
    if(s->job)return;
    if(!s->clearUntil || GetTickCount64()>s->clearUntil){
        s->clearUntil=GetTickCount64()+15000;SetDlgItemTextW(s->window,112,L"确认清空");ShowWindow(GetDlgItem(s->window,116),SW_SHOW);
        SetDlgItemTextW(s->window,107,L"再次点击“确认清空”将移除全部历史（含置顶），不会删除原文件；15 秒后取消。");return;
    }
    CancelClear(s);np3::HistoryChange change;change.action=np3::HistoryAction::Clear;Refresh(s,&change);
}
void Capacity(Panel* s){
    if(s->job)return;
    std::wstring value=Text(s->window,114);
    if(value.empty() || value.find_first_not_of(L"0123456789")!=std::wstring::npos || value.size()>5){SetDlgItemTextW(s->window,107,L"历史上限必须是 1–10000 的整数。");return;}
    unsigned long maximum=wcstoul(value.c_str(),nullptr,10);
    if(maximum<1 || maximum>10000){SetDlgItemTextW(s->window,107,L"历史上限必须是 1–10000 的整数。");return;}
    HISTORY_STATE settings{};SendMessageW(s->parent,NP3_WM_HISTORY_STATE,0,reinterpret_cast<LPARAM>(&settings));settings.maximum=static_cast<int>(maximum);
    if(SendMessageW(s->parent,NP3_WM_HISTORY_STATE,1,reinterpret_cast<LPARAM>(&settings)))SetDlgItemTextW(s->window,107,L"历史上限已设置；下次历史写入时生效，置顶记录优先保留。");
}
void Mutate(Panel* s,np3::HistoryAction action){
    if(s->job)return;
    int selected=ListView_GetNextItem(GetDlgItem(s->window,106),-1,LVNI_SELECTED);if(selected<0 || static_cast<size_t>(selected)>=s->visible.size()){SetDlgItemTextW(s->window,107,L"请先选择历史记录。");return;}
    const auto& entry=s->rows[s->visible[selected]].entry;
    np3::HistoryChange change;change.action=action;change.path=entry.path;change.pinned=!entry.pinned;Refresh(s,&change);
}
void Open(Panel* s){int selected=ListView_GetNextItem(GetDlgItem(s->window,106),-1,LVNI_SELECTED);if(selected<0 || static_cast<size_t>(selected)>=s->visible.size())return;
    Row row=s->rows[s->visible[selected]];
    if(row.missing){SetDlgItemTextW(s->window,107,L"文件不存在或不可访问；记录仍保留。");return;}
    SendMessageW(s->parent,NP3_WM_FILETREE_OPEN,0,reinterpret_cast<LPARAM>(row.entry.path.c_str()));
}
LRESULT CALLBACK Proc(HWND window,UINT message,WPARAM wParam,LPARAM lParam){Panel* s=State(window);
    try{
        if(message==WM_NCCREATE){auto owner=reinterpret_cast<std::unique_ptr<Panel>*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);s=owner->release();s->window=window;SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(s));}
        if(message==WM_CREATE){
            Child(window,L"STATIC",L"类型 / 路径或文件名",0,0,12,12,280,22);Child(window,L"EDIT",L"*",ES_AUTOHSCROLL,102,12,38,144,26);SendDlgItemMessageW(window,102,EM_SETLIMITTEXT,4096,0);Child(window,L"EDIT",L"",ES_AUTOHSCROLL,101,168,38,540,26);SendDlgItemMessageW(window,101,EM_SETLIMITTEXT,4096,0);
            Child(window,L"BUTTON",L"刷新",0,104,12,72,80,26);Child(window,L"BUTTON",L"记录打开历史",BS_AUTOCHECKBOX,109,108,72,150,26);
            Child(window,L"BUTTON",L"置顶/取消",0,110,276,72,100,26);
            Child(window,L"BUTTON",L"移除记录",0,111,392,72,100,26);
            Child(window,L"BUTTON",L"清空历史",0,112,12,108,92,26);
            Child(window,L"STATIC",L"条数上限",0,0,120,112,72,22);
            Child(window,L"EDIT",L"500",ES_AUTOHSCROLL,114,200,108,76,26);SendDlgItemMessageW(window,114,EM_SETLIMITTEXT,5,0);
            Child(window,L"BUTTON",L"应用上限",0,115,292,108,100,26);
            Child(window,L"BUTTON",L"取消清空",0,116,408,108,100,26);ShowWindow(GetDlgItem(window,116),SW_HIDE);
            HWND list=Child(window,WC_LISTVIEWW,L"",LVS_REPORT|LVS_OWNERDATA|LVS_SINGLESEL|LVS_SHOWSELALWAYS,106,12,104,700,350);ListView_SetExtendedListViewStyle(list,LVS_EX_FULLROWSELECT|LVS_EX_DOUBLEBUFFER);
            const wchar_t* titles[]={L"文件",L"路径",L"最近打开",L"次数",L"状态"};const int widths[]={140,320,160,60,120};
            for(int i=0;i<5;++i){LVCOLUMNW c{};c.mask=LVCF_TEXT|LVCF_WIDTH;c.pszText=const_cast<wchar_t*>(titles[i]);c.cx=Scale(window,widths[i]);ListView_InsertColumn(list,i,&c);}
            Child(window,L"STATIC",L"",0,107,12,470,700,22);
            HISTORY_STATE settings{};SendMessageW(s->parent,NP3_WM_HISTORY_STATE,0,reinterpret_cast<LPARAM>(&settings));SendDlgItemMessageW(window,109,BM_SETCHECK,settings.enabled?BST_CHECKED:BST_UNCHECKED,0);EnableWindow(GetDlgItem(window,109),settings.recentEnabled);
            SetDlgItemTextW(window,114,std::to_wstring(settings.maximum).c_str());
            s->theme.Update(window,s->parent);SetTimer(window,1,100,nullptr);Refresh(s);Layout(s);return 0;
        }
        if(message==WM_SIZE && s){Layout(s);return 0;}
        if(message==WM_GETMINMAXINFO){auto info=reinterpret_cast<MINMAXINFO*>(lParam);info->ptMinTrackSize={Scale(window,560),Scale(window,320)};return 0;}
        if(message==WM_TIMER && s && s->clearUntil && GetTickCount64()>s->clearUntil){CancelClear(s);SetDlgItemTextW(window,107,L"清空确认已超时，历史未清除。");}
        if(message==WM_TIMER && s && s->job && s->job->done){auto job=s->job;s->job.reset();
            for(int id:{104,110,111,112,115})EnableWindow(GetDlgItem(window,id),TRUE);
            if(!job->error){auto previous=std::move(s->rows);s->rows=std::move(job->rows);if(!Filter(s)){s->rows=std::move(previous);return 0;}}
            wchar_t status[200];DWORD error=job->error?job->error:HistoryService_Error();
            if(error)StringCchPrintfW(status,200,L"历史读写错误 %lu；已有记录保留。",error);
            else StringCchPrintfW(status,200,L"%zu 条历史记录。双击或 Enter 打开；失效记录保留。",s->rows.size());SetDlgItemTextW(window,107,status);return 0;
        }
        if(message==WM_COMMAND && s){int id=LOWORD(wParam);if(id!=112 && id!=116 && HIWORD(wParam)!=EN_CHANGE)CancelClear(s);if(id==112)Clear(s);else if(id==116){CancelClear(s);SetDlgItemTextW(window,107,L"已取消清空，历史未清除。");}else if(id==115)Capacity(s);else if(id==104)Refresh(s);else if(id==110)Mutate(s,np3::HistoryAction::Pin);else if(id==111)Mutate(s,np3::HistoryAction::Remove);else if((id==101 || id==102) && HIWORD(wParam)==EN_CHANGE)Filter(s);else if(id==109){HISTORY_STATE settings{};SendMessageW(s->parent,NP3_WM_HISTORY_STATE,0,reinterpret_cast<LPARAM>(&settings));settings.enabled=SendDlgItemMessageW(window,109,BM_GETCHECK,0,0)==BST_CHECKED;SendMessageW(s->parent,NP3_WM_HISTORY_STATE,1,reinterpret_cast<LPARAM>(&settings));}return 0;}
        if(message==WM_NOTIFY && s){auto hdr=reinterpret_cast<NMHDR*>(lParam);if(hdr->idFrom==106){
            if(hdr->code==LVN_GETDISPINFOW){auto info=reinterpret_cast<NMLVDISPINFOW*>(lParam);if((info->item.mask&LVIF_TEXT) && info->item.iItem>=0 && static_cast<size_t>(info->item.iItem)<s->visible.size()){
                const Row& row=s->rows[s->visible[info->item.iItem]];s->display=row.entry.path;
                if(info->item.iSubItem==0){size_t slash=s->display.find_last_of(L"\\/");if(slash!=std::wstring::npos)s->display.erase(0,slash+1);}
                if(info->item.iSubItem==2){SYSTEMTIME time{};FILETIME local{};FileTimeToLocalFileTime(&row.entry.opened,&local);FileTimeToSystemTime(&local,&time);wchar_t text[64];StringCchPrintfW(text,64,L"%04u-%02u-%02u %02u:%02u",time.wYear,time.wMonth,time.wDay,time.wHour,time.wMinute);s->display=text;}
                if(info->item.iSubItem==3)s->display=std::to_wstring(row.entry.count);
                if(info->item.iSubItem==4)s->display=row.missing?L"失效/不可访问":row.entry.pinned?L"置顶":L"可打开";
                info->item.pszText=const_cast<wchar_t*>(s->display.c_str());}return 0;}
            if(hdr->code==NM_DBLCLK || hdr->code==NM_RETURN){Open(s);return 0;}
            if(hdr->code==LVN_COLUMNCLICK){int column=reinterpret_cast<NMLISTVIEW*>(lParam)->iSubItem;s->ascending=s->sortColumn==column?!s->ascending:true;s->sortColumn=column;Filter(s);return 0;}
        }}
        if(message==NP3_WM_PANEL_REFRESH && s){s->theme.Update(window,s->parent);return 0;}
        LRESULT result=0;if(s && s->theme.Handle(window,message,wParam,lParam,result))return result;
        if(message==WM_CLOSE){DestroyWindow(window);return 0;}
        if(message==WM_NCDESTROY){KillTimer(window,1);SetWindowLongPtrW(window,GWLP_USERDATA,0);delete s;}
    }catch(...){if(s)SetDlgItemTextW(window,107,L"历史操作失败；记录未清除。");}
    return DefWindowProcW(window,message,wParam,lParam);
}
HWND Existing(HWND parent){for(HWND w=GetWindow(parent,GW_HWNDFIRST);w;w=GetWindow(w,GW_HWNDNEXT)){wchar_t cls[64]{};GetClassNameW(w,cls,64);if(GetWindow(w,GW_OWNER)==parent && !wcscmp(cls,ClassName))return w;}return nullptr;}
}
HWND HistoryPanel_Show(HWND parent,const wchar_t* store){
    if(HWND existing=Existing(parent)){ShowWindow(existing,SW_SHOWNORMAL);SetForegroundWindow(existing);return existing;}
    if(!store || !*store)return nullptr;
    try{WNDCLASSW cls{};cls.lpfnWndProc=Proc;cls.hInstance=GetModuleHandleW(nullptr);cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);cls.lpszClassName=ClassName;RegisterClassW(&cls);
        auto state=std::make_unique<Panel>();state->parent=parent;state->store=store;
        HWND window=CreateWindowExW(0,ClassName,L"打开文件历史",WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,CW_USEDEFAULT,CW_USEDEFAULT,Scale(parent,840),Scale(parent,540),parent,nullptr,cls.hInstance,&state);if(window)ShowWindow(window,SW_SHOW);return window;
    }catch(...){return nullptr;}
}
void HistoryPanel_Refresh(HWND parent){if(HWND window=Existing(parent))SendMessageW(window,NP3_WM_PANEL_REFRESH,0,0);}
