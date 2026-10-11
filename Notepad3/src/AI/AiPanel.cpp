#include "AiDiff.h"
#include "AiPanel.h"
#include "../UI/PanelTheme.h"
#include <richedit.h>
#include <algorithm>
#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <stdexcept>
namespace {
constexpr wchar_t ClassName[] = L"NP3AiPanel";
struct Client { void* handle; explicit Client(const wchar_t* endpoint) : handle(AiHttp_ClientCreate(endpoint)) { if (!handle) throw std::runtime_error("client"); } ~Client() { AiHttp_ClientFree(handle); } };
struct Job {
    AI_PANEL_SOURCE source{};
    int action = AI_EXPLAIN, status = AI_HTTP_CONFIG;
    std::wstring endpoint;
    std::string model, original, instruction;
    char* secret = nullptr;
    DWORD timeout = 60000;
    std::shared_ptr<Client> client;
    std::atomic<bool> cancel{false}, done{false};
    AI_HTTP_RESULT result{};
    ~Job() { AiCredential_Free(secret); AiHttp_Free(&result); }
};
struct Panel {
    np3::PanelTheme theme;
    HMODULE richEdit = nullptr;
    ~Panel() { if (richEdit) FreeLibrary(richEdit); }
    HWND window = nullptr, parent = nullptr;
    std::wstring labels[AP_COUNT], endpoint;
    std::shared_ptr<Client> client;
    std::shared_ptr<Job> job;
    bool published = false;
};
using Owner = std::shared_ptr<Panel>;
Owner State(void* context) { return context ? *static_cast<Owner*>(context) : nullptr; }
Owner WindowState(HWND window) { return State(reinterpret_cast<void*>(GetWindowLongPtrW(window, GWLP_USERDATA))); }
std::wstring Wide(const char* value, size_t length) {
    if (!length) return {};
    int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value, static_cast<int>(length), nullptr, 0);
    if (!size) throw std::runtime_error("UTF8");
    std::wstring result(size, 0); MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value, static_cast<int>(length), result.data(), size); return result;
}
std::string Utf8(const wchar_t* value) {
    int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value, -1, nullptr, 0, nullptr, nullptr);
    if (!size) throw std::runtime_error("UTF16");
    std::string result(size, 0); WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value, -1, result.data(), size, nullptr, nullptr); result.pop_back(); return result;
}
void Status(const Owner& panel, int label) { SetWindowTextW(GetDlgItem(panel->window, 130), panel->labels[label].c_str()); }
void Busy(const Owner& panel, bool busy) {
    for (int id : {101,102,103,104,105,106,110,111,112,113,116,117,120}) EnableWindow(GetDlgItem(panel->window,id), !busy);
    EnableWindow(GetDlgItem(panel->window,114), busy);
    EnableWindow(GetDlgItem(panel->window,115), !busy && panel->job && panel->published && panel->job->status == AI_HTTP_OK && panel->job->action != AI_EXPLAIN && panel->job->action != AI_ASK);
}
int Canceled(void* context) { return static_cast<Job*>(context)->cancel.load() ? 1 : 0; }
void Highlight(HWND edit, size_t start, size_t end, COLORREF color, COLORREF foreground) {
    CHARRANGE selection{}; POINT scroll{};
    SendMessageW(edit,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&selection)); SendMessageW(edit,EM_GETSCROLLPOS,0,reinterpret_cast<LPARAM>(&scroll));
    SendMessageW(edit, EM_SETSEL, static_cast<WPARAM>(start), static_cast<LPARAM>(end));
    CHARFORMAT2W format{}; format.cbSize=sizeof(format); format.dwMask=CFM_BACKCOLOR|CFM_COLOR; format.crBackColor=color; format.crTextColor=foreground;
    SendMessageW(edit, EM_SETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&format));
    SendMessageW(edit,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&selection)); SendMessageW(edit,EM_SETSCROLLPOS,0,reinterpret_cast<LPARAM>(&scroll));
}
std::wstring Paragraphs(const std::wstring& text) {
    std::wstring result; result.reserve(text.size());
    for(size_t i=0;i<text.size();++i) {
        if(text[i]==L'\r') { result.push_back(L'\r'); if(i+1<text.size() && text[i+1]==L'\n') ++i; }
        else result.push_back(text[i]==L'\n'?L'\r':text[i]);
    }
    return result;
}
bool Difference(const Owner& panel, const std::wstring& source, const std::wstring& output) {
    const auto before=Paragraphs(source), after=Paragraphs(output);
    const auto difference=np3::AiDifference(before,after);
    const auto colors=panel->theme.Colors();
    Highlight(GetDlgItem(panel->window,121),0,before.size(),colors.field,colors.text);
    Highlight(GetDlgItem(panel->window,122),0,after.size(),colors.field,colors.text);
    for(const auto& range:difference.ranges) {
        Highlight(GetDlgItem(panel->window,121),range.beforeStart,range.beforeEnd,colors.removed,colors.highlightText);
        Highlight(GetDlgItem(panel->window,122),range.afterStart,range.afterEnd,colors.added,colors.highlightText);
    }
    return difference.precise;
}
void Publish(const Owner& panel) {
    auto job = panel->job;
    if (!job || !job->done.load() || panel->published) return;
    panel->published = true;
    if (job->status == AI_HTTP_OK) {
        auto output = Wide(job->result.text, job->result.length); SetWindowTextW(GetDlgItem(panel->window,122), output.c_str()); bool precise=Difference(panel,Wide(job->original.data(),job->original.size()),output); Status(panel, precise ? AP_DONE : AP_GROUPED);
    } else {
        Status(panel, job->status == AI_HTTP_CANCELED ? AP_CANCELED : AP_ERROR);
        if (job->status != AI_HTTP_CANCELED) {
            auto message = panel->labels[AP_ERROR] + L" (" + std::to_wstring(job->status) + L", HTTP " + std::to_wstring(job->result.httpStatus) + L", Windows " + std::to_wstring(job->result.winError) + L")";
            SetWindowTextW(GetDlgItem(panel->window,130), message.c_str());
        }
    }
    Busy(panel, false);
}
LRESULT CALLBACK Proc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    auto panel = WindowState(window);
    try {
    if(message==NP3_WM_PANEL_REFRESH && panel) {
        panel->theme.Update(window,panel->parent);
        if(panel->job && panel->published && panel->job->status==AI_HTTP_OK) Difference(panel,Wide(panel->job->original.data(),panel->job->original.size()),Wide(panel->job->result.text,panel->job->result.length));
        return 0;
    }
    LRESULT themed=0;
    if(panel && panel->theme.Handle(window,message,wParam,lParam,themed)) return themed;
    if (message == WM_TIMER && panel) { Publish(panel); return 0; }
    if (message == WM_SIZE && panel) {
        int width = LOWORD(lParam), height = HIWORD(lParam), row = MulDiv(28, GetDpiForWindow(window), 96);
        auto move = [&](int id, int x, int y, int w, int h) { MoveWindow(GetDlgItem(window,id), x,y,(std::max)(0,w),(std::max)(0,h),TRUE); };
        for (int i=0;i<4;++i) { int x=i*width/4,w=width/4; move(201+i,x,0,w,row); move(101+i,x,row,w-2,row-2); }
        const int buttons[] = {105,106,110,111,112,113,114,115};
        for (int i=0;i<8;++i) move(buttons[i],i*width/8,2*row,width/8-2,row-2);
        move(205,0,3*row,width/4,row); move(120,width/4,3*row,width-width/4,row-2);
        move(116,0,4*row,width/3-2,6*row); move(117,width/3,4*row,width/6-2,row-2);
        move(206,0,5*row,width/2,row); move(207,width/2,5*row,width-width/2,row);
        int panes = height-7*row;
        move(121,0,6*row,width/2-2,panes); move(122,width/2,6*row,width-width/2,panes);
        move(130,0,(std::max)(6*row,height-row),width,row); return 0;
    }
    if (message == WM_COMMAND && panel) {
        int id = LOWORD(wParam);
        if (HIWORD(wParam) != BN_CLICKED) return 0;
        if ((id >= 110 && id <=113) || id ==117) {
            const int selected=static_cast<int>(SendDlgItemMessageW(window,116,CB_GETCURSEL,0,0));
            if(id==117 && (selected<0 || selected>AI_ASK-AI_SUMMARIZE)) { Status(panel,AP_ERROR); return 0; }
            wchar_t instruction[4097]{}; GetWindowTextW(GetDlgItem(window,120),instruction,4097); auto text=Utf8(instruction);
            if (text.size()>4096) { Status(panel,AP_ERROR); return 0; }
            AI_PANEL_CAPTURE request{id==117?AI_SUMMARIZE+selected:id-110,text.c_str()};
            if (!SendMessageW(panel->parent,NP3_WM_AI_CAPTURE,0,reinterpret_cast<LPARAM>(&request))) Status(panel,AP_ERROR);
        } else if (id == 114 && panel->job) panel->job->cancel=true;
        else if (id == 115 && panel->job && panel->published && panel->job->status == AI_HTTP_OK && panel->job->action != AI_EXPLAIN && panel->job->action != AI_ASK) {
            auto job=panel->job;
            AI_PANEL_APPLY apply{job->source,job->original.data(),job->original.size(),job->result.text,job->result.length};
            if (SendMessageW(panel->parent,NP3_WM_AI_APPLY,0,reinterpret_cast<LPARAM>(&apply))) { EnableWindow(GetDlgItem(window,115),FALSE); Status(panel,AP_READY); }
            else Status(panel,AP_STALE);
        } else if (id ==105 || id==106) {
            AI_PANEL_SAVE request{}; wchar_t key[2049]{};
            GetWindowTextW(GetDlgItem(window,101),request.settings.endpoint,2049); GetWindowTextW(GetDlgItem(window,102),request.settings.model,257);
            BOOL valid=FALSE; request.settings.timeoutMs=GetDlgItemInt(window,104,&valid,FALSE);
            GetWindowTextW(GetDlgItem(window,103),key,2049); auto secret=Utf8(key); SecureZeroMemory(key,sizeof(key)); request.secret=secret.c_str();
            bool saved=valid && SendMessageW(panel->parent,NP3_WM_AI_SAVE,id==106,reinterpret_cast<LPARAM>(&request));
            SecureZeroMemory(secret.data(),secret.size());
            if (saved) SetWindowTextW(GetDlgItem(window,103),L""); Status(panel,saved?AP_SAVED:AP_ERROR);
        }
        return 0;
    }
    if (message == WM_NCDESTROY && panel) {
        KillTimer(window,1); if(panel->job) panel->job->cancel=true;
        panel->job.reset(); panel->window=nullptr; panel->published=false;
        delete reinterpret_cast<Owner*>(GetWindowLongPtrW(window,GWLP_USERDATA)); SetWindowLongPtrW(window,GWLP_USERDATA,0);
    }
    } catch (...) { if(panel && panel->window) { Status(panel,AP_ERROR); Busy(panel,false); } return 0; }
    return DefWindowProcW(window,message,wParam,lParam);
}
}
extern "C" void* AiPanel_Create() { try { return new Owner(std::make_shared<Panel>()); } catch (...) { return nullptr; } }
extern "C" void AiPanel_Destroy(void* context) { auto panel=State(context); if(panel && panel->window) DestroyWindow(panel->window); delete static_cast<Owner*>(context); }
extern "C" BOOL AiPanel_IsOpen(void* context) { auto panel=State(context); return panel && panel->window; }
extern "C" BOOL AiPanel_IsBusy(void* context) { auto panel=State(context); return panel && panel->job && (!panel->job->done.load() || !panel->published); }
extern "C" int AiPanel_Layout(void* context,int x,int y,int width,int height) {
    auto panel=State(context); if(!panel || !panel->window) return 0;
    int reserve=(std::min)((std::max)(0,height*2/3),MulDiv(360,GetDpiForWindow(panel->parent),96));
    MoveWindow(panel->window,x,y+height-reserve,width,reserve,TRUE); return reserve;
}
extern "C" BOOL AiPanel_Run(void* context,const AI_PANEL_SETTINGS* settings,const AI_PANEL_SOURCE* source,int action,const char* original,size_t length,const char* instruction) {
    auto panel=State(context); if(!panel || !panel->window || AiPanel_IsBusy(context) || !settings || !source || (!original && length) || length>256*1024) return FALSE;
    try {
        if(action<AI_EXPLAIN || action>AI_ASK || AiEndpoint_Validate(settings->endpoint)!=AI_OK || !settings->model[0] || settings->timeoutMs<100 || settings->timeoutMs>120000) return FALSE;
        if((action==AI_GENERATE || action==AI_ASK) && (!instruction || !*instruction)) return FALSE;
        auto job=std::make_shared<Job>(); job->source=*source; job->action=action; job->endpoint=settings->endpoint; job->model=Utf8(settings->model); job->timeout=settings->timeoutMs;
        job->original.assign(original?original:"",length); job->instruction=instruction?instruction:"";
        if(job->model.size()>256 || job->instruction.size()>4096) return FALSE;
        wchar_t profile[73]{}; if(AiCredential_ServiceProfile(settings->endpoint,profile,73)!=ERROR_SUCCESS) return FALSE;
        DWORD loaded=AiCredential_Load(profile,&job->secret); if(loaded!=ERROR_SUCCESS && loaded!=ERROR_NOT_FOUND) return FALSE;
        if(!panel->client || panel->endpoint!=settings->endpoint) { panel->client=std::make_shared<Client>(settings->endpoint); panel->endpoint=settings->endpoint; }
        job->client=panel->client;
        const auto& colors=panel->theme.Colors();
        Highlight(GetDlgItem(panel->window,121),0,static_cast<size_t>(GetWindowTextLengthW(GetDlgItem(panel->window,121))),colors.field,colors.text);
        Highlight(GetDlgItem(panel->window,122),0,static_cast<size_t>(GetWindowTextLengthW(GetDlgItem(panel->window,122))),colors.field,colors.text);
        auto before=Wide(original?original:"",length); SetWindowTextW(GetDlgItem(panel->window,121),before.c_str()); SetWindowTextW(GetDlgItem(panel->window,122),L"");
        panel->job=job; panel->published=false;
        std::thread([job] {
            AI_HTTP_CONFIG_DATA config{job->endpoint.c_str(),job->model.c_str(),job->secret,job->timeout,Canceled,job.get(),job->client->handle};
            const auto& input=job->action==AI_GENERATE?job->instruction:job->original;
            job->status=AiHttp_Run(&config,job->action,input.data(),input.size(),job->action==AI_GENERATE?nullptr:job->instruction.c_str(),&job->result);
            AiCredential_Free(job->secret); job->secret=nullptr; job->done=true;
        }).detach(); Busy(panel,true); Status(panel,AP_RUNNING); return TRUE;
    } catch(...) { panel->job.reset(); Busy(panel,false); Status(panel,AP_ERROR); return FALSE; }
}
extern "C" void AiPanel_Toggle(void* context,HWND parent,const AI_PANEL_SETTINGS* settings,const wchar_t* const* labels) {
    auto panel=State(context); if(!panel || !settings || !labels) return;
    try {
        if(panel->window) DestroyWindow(panel->window);
        else {
            if(!panel->richEdit) panel->richEdit=LoadLibraryExW(L"Msftedit.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32); if(!panel->richEdit) throw std::runtime_error("RichEdit");
            panel->parent=parent; for(int i=0;i<AP_COUNT;++i) panel->labels[i]=labels[i];
            WNDCLASSW type{}; type.lpfnWndProc=Proc; type.hInstance=GetModuleHandleW(nullptr); type.lpszClassName=ClassName; type.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_BTNFACE+1); RegisterClassW(&type);
            auto holder=std::make_unique<Owner>(panel);
            HWND window=CreateWindowExW(WS_EX_CLIENTEDGE,ClassName,nullptr,WS_CHILD|WS_VISIBLE,0,0,0,0,parent,nullptr,type.hInstance,nullptr); if(!window) return;
            panel->window=window; SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(holder.release()));
            auto control=[&](const wchar_t* name,const wchar_t* text,DWORD style,int id) {
                HWND child=CreateWindowExW(0,name,text,WS_CHILD|WS_VISIBLE|style,0,0,0,0,window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),type.hInstance,nullptr);
                if(!child) throw std::bad_alloc(); SendMessageW(child,WM_SETFONT,reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)),FALSE); return child;
            };
            for(int i=0;i<4;++i) { control(L"STATIC",labels[i],0,201+i); control(L"EDIT",L"",WS_BORDER|ES_AUTOHSCROLL|WS_TABSTOP|(i==2?ES_PASSWORD:0)|(i==3?ES_NUMBER:0),101+i); }
            SendMessageW(GetDlgItem(window,101),EM_SETLIMITTEXT,2048,0); SendMessageW(GetDlgItem(window,102),EM_SETLIMITTEXT,256,0); SendMessageW(GetDlgItem(window,103),EM_SETLIMITTEXT,2048,0);
            SetWindowTextW(GetDlgItem(window,101),settings->endpoint); SetWindowTextW(GetDlgItem(window,102),settings->model); SetDlgItemInt(window,104,settings->timeoutMs,FALSE);
            const int ids[]={105,106,110,111,112,113,114,115}; const int texts[]={AP_SAVE,AP_DELETE,AP_EXPLAIN,AP_TRANSLATE,AP_REWRITE,AP_GENERATE,AP_CANCEL,AP_APPLY};
            for(int i=0;i<8;++i) control(L"BUTTON",labels[texts[i]],BS_PUSHBUTTON|WS_TABSTOP,ids[i]);
            control(L"STATIC",labels[AP_INSTRUCTION],0,205); control(L"EDIT",L"",WS_BORDER|ES_AUTOHSCROLL|WS_TABSTOP,120); SendMessageW(GetDlgItem(window,120),EM_SETLIMITTEXT,4096,0);
            HWND actions=control(L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_VSCROLL|WS_TABSTOP,116);
            for(const wchar_t* name : {L"摘要",L"润色",L"扩写",L"缩写",L"纠错",L"当前文件问答"}) SendMessageW(actions,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(name));
            SendMessageW(actions,CB_SETCURSEL,0,0); control(L"BUTTON",L"执行",BS_PUSHBUTTON|WS_TABSTOP,117);
            control(L"STATIC",labels[AP_ORIGINAL],0,206); control(L"STATIC",labels[AP_RESULT],0,207);
            for(int id:{121,122}) { HWND edit=control(L"RICHEDIT50W",L"",WS_BORDER|WS_VSCROLL|WS_HSCROLL|ES_MULTILINE|ES_READONLY|ES_AUTOVSCROLL|ES_AUTOHSCROLL|WS_TABSTOP,id); SendMessageW(edit,EM_EXLIMITTEXT,0,2*1024*1024); }
            control(L"STATIC",labels[AP_READY],0,130); Busy(panel,false); if(!SetTimer(window,1,50,nullptr)) throw std::bad_alloc();
            panel->theme.Update(window,parent);
        }
        RECT rect{}; GetClientRect(parent,&rect); SendMessageW(parent,WM_SIZE,0,MAKELPARAM(rect.right,rect.bottom));
    } catch(...) { if(panel->window) DestroyWindow(panel->window); }
}
