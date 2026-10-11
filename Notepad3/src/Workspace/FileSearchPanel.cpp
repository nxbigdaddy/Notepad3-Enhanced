#include "FileSearchPanel.h"
#include "FileSearchCore.h"
#include "FileTree.h"
#include "../UI/PanelTheme.h"
#include <commctrl.h>
#include <strsafe.h>
#include <algorithm>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <iterator>

namespace {
constexpr wchar_t ClassName[]=L"NP3FileSearch";
struct Job {
    std::atomic_bool cancel{false},done{false};
    std::mutex mutex;
    std::deque<std::vector<np3::SearchFile>> batches;
    np3::SearchSummary summary;
    bool failed=false;
    size_t skipped=0;
};
struct Panel {
    HWND window=nullptr,parent=nullptr,roots=nullptr,types=nullptr,query=nullptr,list=nullptr,status=nullptr;
    np3::PanelTheme theme;
    std::shared_ptr<Job> job;
    std::vector<np3::SearchFile> files;
    std::wstring display;
    int sortColumn=0; bool ascending=true;
    bool contentMode=false;
};
Panel* State(HWND window){return reinterpret_cast<Panel*>(GetWindowLongPtrW(window,GWLP_USERDATA));}
std::wstring Text(HWND control){int length=GetWindowTextLengthW(control);std::vector<wchar_t> text(static_cast<size_t>(length)+1);GetWindowTextW(control,text.data(),length+1);return text.data();}
int Scale(HWND window,int value){return MulDiv(value,GetDpiForWindow(window),96);}
void Layout(Panel* state) {
    RECT client{};GetClientRect(state->window,&client);int margin=Scale(state->window,12),width=(std::max)(0,static_cast<int>(client.right)-2*margin);
    MoveWindow(state->roots,margin,Scale(state->window,36),width,Scale(state->window,52),TRUE);
    MoveWindow(state->types,margin,Scale(state->window,116),Scale(state->window,180),Scale(state->window,26),TRUE);
    MoveWindow(state->query,margin+Scale(state->window,192),Scale(state->window,116),(std::max)(0,width-Scale(state->window,192)),Scale(state->window,26),TRUE);
    MoveWindow(GetDlgItem(state->window,108),margin+Scale(state->window,100),Scale(state->window,154),(std::max)(0,width-Scale(state->window,100)),Scale(state->window,26),TRUE);
    MoveWindow(GetDlgItem(state->window,110),margin+Scale(state->window,100),Scale(state->window,190),(std::max)(0,width-Scale(state->window,100)),Scale(state->window,26),TRUE);
    MoveWindow(state->list,margin,Scale(state->window,268),width,(std::max)(0,static_cast<int>(client.bottom)-Scale(state->window,304)),TRUE);
    MoveWindow(state->status,margin,client.bottom-Scale(state->window,28),width,Scale(state->window,22),TRUE);
}
std::wstring Name(const std::wstring& path){size_t separator=path.find_last_of(L"\\/");return separator==std::wstring::npos?path:path.substr(separator+1);}
void Sort(Panel* state) {
    std::wstring selected;size_t selectedLine=0,selectedColumn=0;int selection=ListView_GetNextItem(state->list,-1,LVNI_SELECTED);
    if(selection>=0 && static_cast<size_t>(selection)<state->files.size()){selected=state->files[selection].path;selectedLine=state->files[selection].line;selectedColumn=state->files[selection].column;}
    std::stable_sort(state->files.begin(),state->files.end(),[&](const np3::SearchFile& a,const np3::SearchFile& b){
        int order=0;
        if(state->sortColumn==2)order=state->contentMode?(a.line<b.line?-1:a.line>b.line?1:a.column<b.column?-1:a.column>b.column?1:0):(a.size<b.size?-1:a.size>b.size?1:0);
        else if(state->sortColumn==3)order=state->contentMode?CompareStringOrdinal(a.context.c_str(),-1,b.context.c_str(),-1,TRUE)-CSTR_EQUAL:CompareFileTime(&a.modified,&b.modified);
        else {std::wstring first=state->sortColumn==0?Name(a.path):a.path,second=state->sortColumn==0?Name(b.path):b.path;order=CompareStringOrdinal(first.c_str(),-1,second.c_str(),-1,TRUE)-CSTR_EQUAL;}
        if(!order)order=CompareStringOrdinal(a.path.c_str(),-1,b.path.c_str(),-1,TRUE)-CSTR_EQUAL;
        return state->ascending?order<0:order>0;
    });
    ListView_SetItemState(state->list,-1,0,LVIS_SELECTED|LVIS_FOCUSED);
    if(!selected.empty())for(size_t i=0;i<state->files.size();++i)if(state->files[i].path==selected && state->files[i].line==selectedLine && state->files[i].column==selectedColumn){ListView_SetItemState(state->list,static_cast<int>(i),LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);break;}
    InvalidateRect(state->list,nullptr,TRUE);
}
void Start(Panel* state) {
    np3::SearchOptions options;
    if(!np3::ParseSearchExtensions(Text(state->types),options.extensions)){SetWindowTextW(state->status,L"格式无效，请填写 *.md;*.txt 或 *");return;}
    std::wstring roots=Text(state->roots),root;
    for(wchar_t c:roots){if(c==L'\r'||c==L'\n'){if(!root.empty()){options.roots.push_back(root);root.clear();}}else root+=c;}
    if(!root.empty())options.roots.push_back(root);
    if(options.roots.empty()){SetWindowTextW(state->status,L"请填写搜索目录，每行一个目录。");return;}
    options.query=Text(state->query);
    np3::ContentOptions content;content.query=Text(GetDlgItem(state->window,110));content.caseSensitive=SendDlgItemMessageW(state->window,111,BM_GETCHECK,0,0)==BST_CHECKED;content.wholeWord=SendDlgItemMessageW(state->window,113,BM_GETCHECK,0,0)==BST_CHECKED;
    options.recursive=SendDlgItemMessageW(state->window,109,BM_GETCHECK,0,0)==BST_CHECKED;
    options.excludedDirectories.clear();std::wstring excluded=Text(GetDlgItem(state->window,108)),entry;
    auto accept=[&](){size_t first=entry.find_first_not_of(L" \t"),last=entry.find_last_not_of(L" \t");if(first!=std::wstring::npos){auto name=entry.substr(first,last-first+1);if(name==L"." || name==L".." || name.find_first_of(L"\\/:*?\"<>|")!=std::wstring::npos)return false;options.excludedDirectories.push_back(name);}entry.clear();return true;};
    for(wchar_t c:excluded){if(c==L';'){if(!accept()){SetWindowTextW(state->status,L"排除目录请填写名称，以分号分隔，不填写路径或通配符。");return;}}else entry+=c;}
    if(!accept()){SetWindowTextW(state->status,L"排除目录请填写名称，以分号分隔，不填写路径或通配符。");return;}
    if(state->job)state->job->cancel=true;
    auto job=std::make_shared<Job>();
    if(!SetTimer(state->window,1,100,nullptr)){SetWindowTextW(state->status,L"无法启动搜索计时器。");return;}
    // Keep previous results if allocating the worker fails.
    std::thread([job,options,content]{
        try {
            auto publish=[&](const std::vector<np3::SearchFile>& batch){
                while(!job->cancel.load()) {
                    {std::lock_guard<std::mutex> guard(job->mutex);if(job->batches.size()<16){job->batches.push_back(batch);return true;}}
                    Sleep(10);
                }
                return false;
            };
            size_t matched=0,skipped=0;bool limited=false;std::vector<np3::SearchError> errors;
            auto summary=np3::SearchFiles(options,job->cancel,[&](const std::vector<np3::SearchFile>& batch){
                if(content.query.empty())return publish(batch);
                auto settings=content;settings.maxMatches=content.maxMatches-matched;
                if(!settings.maxMatches){limited=true;return false;}
                std::vector<np3::SearchFile> rows;
                auto found=np3::SearchContents(batch,settings,job->cancel,[&](const np3::ContentMatch& match){rows.push_back({match.path,0,{},match.context,match.line,match.column,match.length});if(rows.size()==128){if(!publish(rows))return false;rows.clear();}return true;});
                if(!rows.empty() && !publish(rows))return false;
                matched+=found.matches;skipped+=found.skipped;errors.insert(errors.end(),found.errors.begin(),found.errors.end());limited=found.limited;
                return !found.cancelled && !found.limited;
            });
            summary.errors.insert(summary.errors.end(),errors.begin(),errors.end());summary.limited=summary.limited||limited;if(limited)summary.cancelled=false;
            job->skipped=skipped;
            {std::lock_guard<std::mutex> guard(job->mutex);job->summary=std::move(summary);}
        }catch(...){std::lock_guard<std::mutex> guard(job->mutex);job->failed=true;}
        job->done=true;
    }).detach();
    state->job=job;state->contentMode=!content.query.empty();state->files.clear();ListView_SetItemCountEx(state->list,0,LVSICF_NOSCROLL);
    const wchar_t* titles[]={state->contentMode?L"行:列":L"大小（字节）",state->contentMode?L"匹配上下文":L"修改时间"};for(int i=0;i<2;++i){LVCOLUMNW column{};column.mask=LVCF_TEXT;column.pszText=const_cast<wchar_t*>(titles[i]);ListView_SetColumn(state->list,i+2,&column);}
    SetWindowTextW(state->status,L"搜索中… 可停止或重新搜索");
}
void Drain(Panel* state) {
    auto job=state->job;if(!job)return;
    std::deque<std::vector<np3::SearchFile>> batches;np3::SearchSummary summary;bool failed=false,done=false;
    {std::lock_guard<std::mutex> guard(job->mutex);batches.swap(job->batches);done=job->done.load();if(done){summary=job->summary;failed=job->failed;}}
    for(auto& batch:batches)state->files.insert(state->files.end(),std::make_move_iterator(batch.begin()),std::make_move_iterator(batch.end()));
    ListView_SetItemCountEx(state->list,static_cast<int>(state->files.size()),LVSICF_NOSCROLL|LVSICF_NOINVALIDATEALL);
    wchar_t message[256];
    StringCchPrintfW(message,256,L"%s · %llu %s%s · %llu 个错误 · %llu 个跳过",failed?L"搜索失败":done?(summary.cancelled?L"已停止":L"搜索完成"):L"搜索中",static_cast<unsigned long long>(state->files.size()),state->contentMode?L"个匹配":L"个文件",summary.limited?L"（达到结果上限）":L"",static_cast<unsigned long long>(summary.errors.size()),static_cast<unsigned long long>(done?job->skipped:0));
    SetWindowTextW(state->status,message);
    if(done){KillTimer(state->window,1);Sort(state);}
}
HWND Child(HWND parent,const wchar_t* type,const wchar_t* text,DWORD style,int id,int x,int y,int w,int h){return CreateWindowExW(type==std::wstring(L"EDIT")?WS_EX_CLIENTEDGE:0,type,text,WS_CHILD|WS_VISIBLE|style,Scale(parent,x),Scale(parent,y),Scale(parent,w),Scale(parent,h),parent,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),GetModuleHandleW(nullptr),nullptr);}
LRESULT CALLBACK Proc(HWND window,UINT message,WPARAM wParam,LPARAM lParam) {
    Panel* state=State(window);
    try {
        if(message==WM_NCCREATE){auto* owner=static_cast<std::unique_ptr<Panel>*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);state=owner->release();state->window=window;SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(state));return TRUE;}
        if(message==WM_GETMINMAXINFO){auto* size=reinterpret_cast<MINMAXINFO*>(lParam);size->ptMinTrackSize={Scale(window,600),Scale(window,440)};return 0;}
        if(!state)return DefWindowProcW(window,message,wParam,lParam);
        if(message==NP3_WM_PANEL_REFRESH){state->theme.Update(window,state->parent);Layout(state);return 0;}
        LRESULT themed=0;if(state->theme.Handle(window,message,wParam,lParam,themed))return themed;
        if(message==WM_CREATE){
            Child(window,L"STATIC",L"搜索目录（每行一个）",0,0,12,12,620,22);
            state->roots=Child(window,L"EDIT",L"",WS_TABSTOP|ES_MULTILINE|ES_AUTOVSCROLL|WS_VSCROLL,101,12,36,700,52);SendMessageW(state->roots,EM_SETLIMITTEXT,65536,0);
            Child(window,L"STATIC",L"文件格式",0,0,12,94,180,22);Child(window,L"STATIC",L"文件名或路径关键词",0,0,204,94,400,22);
            state->types=Child(window,L"EDIT",L"*.md",WS_TABSTOP|ES_AUTOHSCROLL,102,12,116,180,26);SendMessageW(state->types,EM_SETLIMITTEXT,4096,0);
            state->query=Child(window,L"EDIT",L"",WS_TABSTOP|ES_AUTOHSCROLL,103,204,116,500,26);SendMessageW(state->query,EM_SETLIMITTEXT,4096,0);
            Child(window,L"STATIC",L"排除目录",0,0,12,156,96,22);
            HWND exclusions=Child(window,L"EDIT",L".git;node_modules;.np3-extension-backups;render-assets",WS_TABSTOP|ES_AUTOHSCROLL,108,112,154,600,26);SendMessageW(exclusions,EM_SETLIMITTEXT,4096,0);
            Child(window,L"STATIC",L"正文关键词",0,0,12,192,96,22);HWND contentEdit=Child(window,L"EDIT",L"",WS_TABSTOP|ES_AUTOHSCROLL,110,112,190,600,26);SendMessageW(contentEdit,EM_SETLIMITTEXT,4096,0);
            Child(window,L"BUTTON",L"搜索",WS_TABSTOP,104,12,226,90,30);Child(window,L"BUTTON",L"停止",WS_TABSTOP,105,114,226,90,30);
            HWND recursive=Child(window,L"BUTTON",L"包含子目录",WS_TABSTOP|BS_AUTOCHECKBOX,109,218,230,120,22);SendMessageW(recursive,BM_SETCHECK,BST_CHECKED,0);
            Child(window,L"BUTTON",L"区分大小写",WS_TABSTOP|BS_AUTOCHECKBOX,111,350,230,120,22);Child(window,L"BUTTON",L"整词",WS_TABSTOP|BS_AUTOCHECKBOX,113,482,230,80,22);
            state->list=Child(window,WC_LISTVIEWW,L"",WS_TABSTOP|LVS_REPORT|LVS_OWNERDATA|LVS_SINGLESEL|LVS_SHOWSELALWAYS,106,12,268,700,204);
            ListView_SetExtendedListViewStyle(state->list,LVS_EX_FULLROWSELECT|LVS_EX_DOUBLEBUFFER);
            const wchar_t* names[]={L"名称",L"完整路径",L"大小（字节）",L"修改时间"};int widths[]={180,370,100,160};
            for(int i=0;i<4;++i){LVCOLUMNW column{};column.mask=LVCF_TEXT|LVCF_WIDTH;column.pszText=const_cast<wchar_t*>(names[i]);column.cx=Scale(window,widths[i]);ListView_InsertColumn(state->list,i,&column);}
            state->status=Child(window,L"STATIC",L"填写目录后点击搜索；双击或 Enter 打开文件。",0,107,12,486,700,22);
            state->theme.Update(window,state->parent);return 0;
        }
        if(message==WM_SIZE){Layout(state);return 0;}
        if(message==WM_TIMER){Drain(state);return 0;}
        if(message==WM_COMMAND){if(LOWORD(wParam)==104)Start(state);else if(LOWORD(wParam)==105 && state->job)state->job->cancel=true;return 0;}
        if(message==WM_NOTIFY){auto* header=reinterpret_cast<NMHDR*>(lParam);if(header->hwndFrom!=state->list)return 0;
            if(header->code==LVN_GETDISPINFOW){auto* info=reinterpret_cast<NMLVDISPINFOW*>(lParam);if((info->item.mask&LVIF_TEXT) && info->item.iItem>=0 && static_cast<size_t>(info->item.iItem)<state->files.size()){
                const auto& file=state->files[info->item.iItem];if(state->contentMode && info->item.iSubItem==2)state->display=std::to_wstring(file.line)+L":"+std::to_wstring(file.column);else if(state->contentMode && info->item.iSubItem==3)state->display=file.context;else if(info->item.iSubItem==0)state->display=Name(file.path);else if(info->item.iSubItem==1)state->display=file.path;else if(info->item.iSubItem==2)state->display=std::to_wstring(file.size);else {FILETIME local{};SYSTEMTIME time{};wchar_t value[64]{};if(FileTimeToLocalFileTime(&file.modified,&local)&&FileTimeToSystemTime(&local,&time))StringCchPrintfW(value,64,L"%04u-%02u-%02u %02u:%02u",time.wYear,time.wMonth,time.wDay,time.wHour,time.wMinute);state->display=value;}info->item.pszText=const_cast<wchar_t*>(state->display.c_str());}return 0;}
            if(header->code==LVN_COLUMNCLICK){int column=reinterpret_cast<NMLISTVIEW*>(lParam)->iSubItem;state->ascending=column==state->sortColumn?!state->ascending:true;state->sortColumn=column;Sort(state);return 0;}
            if(header->code==NM_DBLCLK || header->code==NM_RETURN){int item=ListView_GetNextItem(state->list,-1,LVNI_SELECTED);if(item>=0 && static_cast<size_t>(item)<state->files.size()){auto file=state->files[item];if(state->contentMode){SEARCH_LOCATION location{file.path.c_str(),file.context.c_str(),file.line,file.column,file.length};if(!SendMessageW(state->parent,NP3_WM_SEARCH_LOCATE,0,reinterpret_cast<LPARAM>(&location)))SetWindowTextW(state->status,L"文件内容已变化或无法定位，请重新搜索。");}else SendMessageW(state->parent,NP3_WM_FILETREE_OPEN,0,reinterpret_cast<LPARAM>(file.path.c_str()));}return 0;}
        }
        if(message==WM_NCDESTROY){if(state->job)state->job->cancel=true;KillTimer(window,1);SetWindowLongPtrW(window,GWLP_USERDATA,0);delete state;return DefWindowProcW(window,message,wParam,lParam);}
        return DefWindowProcW(window,message,wParam,lParam);
    }catch(...){if(state && state->status)SetWindowTextW(state->status,L"操作失败，请停止并重试。");return 0;}
}
}
extern "C" HWND FileSearch_Show(HWND parent,const wchar_t* directory) {
    if(!IsWindow(parent))return nullptr;
    for(HWND existing=GetWindow(parent,GW_HWNDFIRST);existing;existing=GetWindow(existing,GW_HWNDNEXT)){wchar_t cls[64]{};GetClassNameW(existing,cls,64);if(GetWindow(existing,GW_OWNER)==parent && wcscmp(cls,ClassName)==0){ShowWindow(existing,SW_SHOWNORMAL);SetForegroundWindow(existing);return existing;}}
    try {
        WNDCLASSW cls{};cls.lpfnWndProc=Proc;cls.hInstance=GetModuleHandleW(nullptr);cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);cls.lpszClassName=ClassName;RegisterClassW(&cls);
        auto state=std::make_unique<Panel>();state->parent=parent;
        HWND window=CreateWindowExW(0,ClassName,L"文件搜索",WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,CW_USEDEFAULT,CW_USEDEFAULT,Scale(parent,840),Scale(parent,580),parent,nullptr,cls.hInstance,&state);
        if(!window)return nullptr;SetWindowTextW(State(window)->roots,directory?directory:L"");Layout(State(window));ShowWindow(window,SW_SHOW);return window;
    }catch(...){return nullptr;}
}
extern "C" void FileSearch_Refresh(HWND parent) {
    for(HWND window=GetWindow(parent,GW_HWNDFIRST);window;window=GetWindow(window,GW_HWNDNEXT)){
        wchar_t cls[64]{};GetClassNameW(window,cls,64);
        if(GetWindow(window,GW_OWNER)==parent && wcscmp(cls,ClassName)==0)SendMessageW(window,NP3_WM_PANEL_REFRESH,0,0);
    }
}
