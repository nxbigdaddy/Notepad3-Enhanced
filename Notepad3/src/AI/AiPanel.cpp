#include "AiDiff.h"
#include "AiPanel.h"
#include "../UI/PanelTheme.h"
#include <richedit.h>
#include <commctrl.h>
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
    bool models = false, nativeModels = false;
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
    std::wstring labels[AP_COUNT], endpoint, activeId, activeModel;
    std::shared_ptr<Client> client;
    std::shared_ptr<Job> job;
    bool published = false, settingsExpanded = false;
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
std::wstring ChatAddress(const wchar_t* value) {
    std::wstring address=value;while(!address.empty() && address.back()==L'/')address.pop_back();
    if(address.size()>=3 && address.compare(address.size()-3,3,L"/v1")==0)address+=L"/chat/completions";
    return address.size()>2048?std::wstring{}:address;
}
void Status(const Owner& panel, int label) { SetWindowTextW(GetDlgItem(panel->window, 130), panel->labels[label].c_str()); }
void Busy(const Owner& panel, bool busy) {
    for (int id : {101,102,103,104,105,106,110,111,112,113,116,117,119,120,123,124,125,126,127,128,129,131,132,133,134}) EnableWindow(GetDlgItem(panel->window,id), !busy);
    EnableWindow(GetDlgItem(panel->window,114), busy);
    EnableWindow(GetDlgItem(panel->window,115), !busy && panel->job && panel->published && panel->job->status == AI_HTTP_OK && panel->job->action != AI_EXPLAIN && panel->job->action != AI_ASK && panel->job->action != AI_CONNECTION_TEST && !panel->job->models);
}
void Profiles(const Owner& panel) {
    HWND combo=GetDlgItem(panel->window,123); SendMessageW(combo,CB_RESETCONTENT,0,0);
    const std::wstring current=panel->activeModel.empty()?L"选择已保存模型":L"当前模型："+panel->activeModel;
    SendMessageW(combo,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(current.c_str())); SendMessageW(combo,CB_SETITEMDATA,0,static_cast<LPARAM>(-1));
    LRESULT selected=0;
    for(int index=0;index<AI_PROFILE_MAXIMUM;++index) {
        AI_PANEL_SETTINGS settings{};
        if(SendMessageW(panel->parent,NP3_WM_AI_PROFILE,index,reinterpret_cast<LPARAM>(&settings))) {
            std::wstring account=settings.configurationId;
            if(account.rfind(L"account-",0)==0){const std::wstring address=settings.endpoint;account=address.find(L"moonshot")!=std::wstring::npos?L"Kimi":address.find(L"deepseek")!=std::wstring::npos?L"DeepSeek":address.find(L"aliyuncs")!=std::wstring::npos?L"百炼":L"自定义服务";account+=L"（账号"+std::to_wstring(index+1)+L"）";}
            std::wstring label=account+L" — "+settings.model;
            const LRESULT item=SendMessageW(combo,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label.c_str()));
            if(item>=0){SendMessageW(combo,CB_SETITEMDATA,static_cast<WPARAM>(item),index);if(panel->activeId==settings.configurationId)selected=item;}
        }
    }
    SendMessageW(combo,CB_SETCURSEL,static_cast<WPARAM>(selected),0);
}
void Prompts(const Owner& panel) {
    HWND combo=GetDlgItem(panel->window,127);SendMessageW(combo,CB_RESETCONTENT,0,0);SendMessageW(combo,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"选择已保存提示词"));SendMessageW(combo,CB_SETITEMDATA,0,-1);
    for(int index=0;index<32;++index){AI_PROMPT prompt{};prompt.index=index;if(SendMessageW(panel->parent,NP3_WM_AI_PROMPT,0,reinterpret_cast<LPARAM>(&prompt))){LRESULT item=SendMessageW(combo,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(prompt.name));if(item>=0)SendMessageW(combo,CB_SETITEMDATA,static_cast<WPARAM>(item),index);}}
    SendMessageW(combo,CB_SETCURSEL,0,0);
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
    if (job->status == AI_HTTP_OK && job->models) {
        HWND combo=GetDlgItem(panel->window,102); wchar_t current[257]{};GetWindowTextW(combo,current,257);SendMessageW(combo,CB_RESETCONTENT,0,0);
        const auto ids=Wide(job->result.text,job->result.length);size_t begin=0;while(begin<ids.size()){auto end=ids.find(L'\n',begin);auto id=ids.substr(begin,end-begin);SendMessageW(combo,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(id.c_str()));begin=end+1;}
        SetWindowTextW(combo,current);SetWindowTextW(GetDlgItem(panel->window,130),ids.empty()?L"服务返回空模型列表；可手动填写模型。":L"模型列表已获取，请下拉选择模型后保存。列表不保证模型支持文本对话。");
    } else if (job->status == AI_HTTP_OK) {
        auto output = Wide(job->result.text, job->result.length); SetWindowTextW(GetDlgItem(panel->window,122), output.c_str()); bool precise=Difference(panel,Wide(job->original.data(),job->original.size()),output); Status(panel, precise ? AP_DONE : AP_GROUPED);
        if(job->action==AI_CONNECTION_TEST) SetWindowTextW(GetDlgItem(panel->window,130),L"连接测试成功：当前模型已返回完整回复；测试可能计费。");
    } else {
        Status(panel, job->status == AI_HTTP_CANCELED ? AP_CANCELED : AP_ERROR);
        if (job->status != AI_HTTP_CANCELED) {
            const wchar_t* reasons[]={L"成功",L"认证失败：检查密钥和地域",L"权限不足：检查账号与模型权限",L"付款或额度限制",L"限流或配额限制：稍后重试并检查额度",L"地址或模型不存在：检查完整接口地址和模型ID",L"请求参数不被服务接受",L"服务端错误：稍后重试",L"服务重定向已阻止：检查接口地址",L"请求超时",L"TLS 安全连接失败",L"网络连接失败",L"结果截断或未完成，不能应用",L"响应格式不兼容或内容无效",L"请求或响应超过大小限制",L"配置或输入无效",L"已取消",L"内存不足",L"服务忙",L"请求失败"};
            const int diagnostic=AiHttp_Diagnostic(job->status,&job->result);
            auto message = std::wstring(reasons[diagnostic]) + L" (" + std::to_wstring(job->status) + L", HTTP " + std::to_wstring(job->result.httpStatus) + L", Windows " + std::to_wstring(job->result.winError) + L")";
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
        if(panel->job && panel->published && panel->job->status==AI_HTTP_OK && !panel->job->models) Difference(panel,Wide(panel->job->original.data(),panel->job->original.size()),Wide(panel->job->result.text,panel->job->result.length));
        return 0;
    }
    LRESULT themed=0;
    if(panel && panel->theme.Handle(window,message,wParam,lParam,themed)) return themed;
    if (message == WM_TIMER && panel) { Publish(panel); return 0; }
    if (message == WM_SIZE && panel) {
        int width = LOWORD(lParam), height = HIWORD(lParam), row = MulDiv(28, GetDpiForWindow(window), 96);
        auto move = [&](int id, int x, int y, int w, int h) { MoveWindow(GetDlgItem(window,id), x,y,(std::max)(0,w),(std::max)(0,h),TRUE); };
        move(107,0,0,width/6-2,row-2);move(123,width/6,0,2*width/3-2,6*row);move(124,5*width/6,0,width/6-2,row-2);
        for(int id:{101,102,103,104,105,106,118,119,125,126,132,133,134,208,201,202,203,204}) ShowWindow(GetDlgItem(window,id),panel->settingsExpanded?SW_SHOW:SW_HIDE);
        int base=1;
        if(panel->settingsExpanded){
            for(int i=0;i<4;++i){move(201+i,i*width/4,row,width/4,row);move(101+i,i*width/4,2*row,width/4-2,i==1?8*row:row-2);}
            move(105,0,3*row,width/8-2,row-2);move(106,width/8,3*row,width/8-2,row-2);move(118,width/4,3*row,width/8,row);move(119,3*width/8,3*row,width/4-2,row-2);move(125,5*width/8,3*row,width/4-2,6*row);move(126,7*width/8,3*row,width/8-2,row-2);move(208,0,4*row,width/6,row);move(133,width/6,4*row,width/2-2,row-2);move(134,2*width/3,4*row,width/6-2,row-2);move(132,5*width/6,4*row,width/6-2,row-2);base=5;
        }
        for(int i=0;i<6;++i)move(110+i,i*width/9,base*row,width/9-2,row-2);
        move(116,2*width/3,base*row,2*width/9-2,6*row);move(117,8*width/9,base*row,width/9-2,row-2);
        ShowWindow(GetDlgItem(window,205),SW_HIDE);
        move(127,0,(base+1)*row,width/3-2,6*row);move(128,width/3,(base+1)*row,width/3-2,row-2);move(129,2*width/3,(base+1)*row,width/6-2,row-2);move(131,5*width/6,(base+1)*row,width/6-2,row-2);
        move(120,0,(base+2)*row,width,2*row-2);
        move(206,0,(base+4)*row,width/2,row);move(207,width/2,(base+4)*row,width/2,row);
        int top=(base+5)*row;int panes=(std::max)(0,height-row-top);
        move(121,0,top,width/2-2,panes);move(122,width/2,top,width/2,panes);move(130,0,(std::max)(top,height-row),width,row);return 0;
    }
    if (message == WM_COMMAND && panel) {
        int id = LOWORD(wParam);
        if(id==107 && HIWORD(wParam)==BN_CLICKED){panel->settingsExpanded=!panel->settingsExpanded;SetWindowTextW(GetDlgItem(window,107),panel->settingsExpanded?L"收起模型设置":L"模型设置");RECT rect{};GetClientRect(panel->parent,&rect);SendMessageW(panel->parent,WM_SIZE,0,MAKELPARAM(rect.right,rect.bottom));return 0;}
        if(id==127 && HIWORD(wParam)==CBN_SELCHANGE){
            if(panel->job && (!panel->job->done.load() || !panel->published))return 0;
            AI_PROMPT prompt{};LRESULT item=SendDlgItemMessageW(window,127,CB_GETCURSEL,0,0);prompt.index=static_cast<int>(SendDlgItemMessageW(window,127,CB_GETITEMDATA,static_cast<WPARAM>(item),0));
            if(SendMessageW(panel->parent,NP3_WM_AI_PROMPT,0,reinterpret_cast<LPARAM>(&prompt))){SetWindowTextW(GetDlgItem(window,128),prompt.name);SetWindowTextW(GetDlgItem(window,120),prompt.text);SetWindowTextW(GetDlgItem(window,130),L"提示词已载入，可编辑后保存，或选择操作执行。");}return 0;
        }
        if((id==129 || id==131) && HIWORD(wParam)==BN_CLICKED){
            AI_PROMPT prompt{};GetWindowTextW(GetDlgItem(window,128),prompt.name,65);GetWindowTextW(GetDlgItem(window,120),prompt.text,4097);
            if(SendMessageW(panel->parent,id==129?NP3_WM_AI_PROMPT_SAVE:NP3_WM_AI_PROMPT_DELETE,0,reinterpret_cast<LPARAM>(&prompt))){Prompts(panel);SetWindowTextW(GetDlgItem(window,130),id==129?L"提示词已保存，同名内容已更新。":L"提示词已删除；输入框中的草稿保留。");}else SetWindowTextW(GetDlgItem(window,130),L"提示词操作失败：填写名称和内容；删除前载入原内容；最多32项、4096 UTF-8字节。");return 0;
        }
        if(id==125 && HIWORD(wParam)==CBN_SELCHANGE) {
            if(panel->job && (!panel->job->done.load() || !panel->published)) return 0;
            const LRESULT selected=SendDlgItemMessageW(window,125,CB_GETCURSEL,0,0);
            const wchar_t* endpoints[]={L"",L"https://api.moonshot.cn/v1/chat/completions",L"https://api.deepseek.com/chat/completions",L"https://dashscope.aliyuncs.com/compatible-mode/v1/chat/completions",L"",L"https://api.openai.com/v1/chat/completions"};
            if(selected<1 || selected>5) return 0;
            SendDlgItemMessageW(window,134,BM_SETCHECK,selected==3?BST_CHECKED:BST_UNCHECKED,0);SetWindowTextW(GetDlgItem(window,133),L"");SendDlgItemMessageW(window,102,CB_RESETCONTENT,0,0);SetWindowTextW(GetDlgItem(window,101),endpoints[selected]);SetWindowTextW(GetDlgItem(window,102),L"");SetWindowTextW(GetDlgItem(window,103),L"");SetWindowTextW(GetDlgItem(window,119),L"");
            SetWindowTextW(GetDlgItem(window,130),L"填入 Key，点击获取模型，选择模型后保存；也可手动填写模型。");return 0;
        }
        if(id==123 && HIWORD(wParam)==CBN_SELCHANGE) {
            if(AiPanel_IsBusy(reinterpret_cast<void*>(GetWindowLongPtrW(window,GWLP_USERDATA)))) return 0;
            const LRESULT item=SendDlgItemMessageW(window,123,CB_GETCURSEL,0,0);
            const int index=static_cast<int>(SendDlgItemMessageW(window,123,CB_GETITEMDATA,static_cast<WPARAM>(item),0));
            AI_PANEL_SAVE request{};request.secret="";
            if(index<0) return 0;
            if(!SendMessageW(panel->parent,NP3_WM_AI_PROFILE,index,reinterpret_cast<LPARAM>(&request.settings)) || !SendMessageW(panel->parent,NP3_WM_AI_SAVE,0,reinterpret_cast<LPARAM>(&request))) {Status(panel,AP_ERROR);return 0;}
            panel->activeId=request.settings.configurationId;panel->activeModel=request.settings.model;
            SetWindowTextW(GetDlgItem(window,101),request.settings.endpoint);SetWindowTextW(GetDlgItem(window,102),request.settings.model);
            SetDlgItemInt(window,104,request.settings.timeoutMs,FALSE);SetWindowTextW(GetDlgItem(window,119),request.settings.configurationId);SetWindowTextW(GetDlgItem(window,103),L"");Status(panel,AP_SAVED);return 0;
        }
        if (HIWORD(wParam) != BN_CLICKED) return 0;
        if(id==124) {
            if(panel->job && (!panel->job->done.load() || !panel->published)) return 0;
            const LRESULT item=SendDlgItemMessageW(window,123,CB_GETCURSEL,0,0);
            const int index=static_cast<int>(SendDlgItemMessageW(window,123,CB_GETITEMDATA,static_cast<WPARAM>(item),0));
            AI_PANEL_SETTINGS expected{};
            const bool removed=index>=0 && SendMessageW(panel->parent,NP3_WM_AI_PROFILE,index,reinterpret_cast<LPARAM>(&expected)) && SendMessageW(panel->parent,NP3_WM_AI_REMOVE_PROFILE,index,reinterpret_cast<LPARAM>(&expected));
            if(removed) {Profiles(panel);SetWindowTextW(GetDlgItem(window,130),L"已移出列表；当前设置和密钥保留，可再次保存恢复。");} else Status(panel,AP_ERROR);
            return 0;
        }
        if(id==132) {
            if(panel->job && (!panel->job->done.load() || !panel->published))return 0;
            wchar_t address[2049]{}, list[2049]{}, key[2049]{}, identity[65]{};
            GetDlgItemTextW(window,101,address,2049);GetDlgItemTextW(window,133,list,2049);GetDlgItemTextW(window,103,key,2049);GetDlgItemTextW(window,119,identity,65);
            const auto chat=ChatAddress(address);wcscpy_s(address,chat.c_str());SetDlgItemTextW(window,101,address);
            auto job=std::make_shared<Job>();job->models=true;job->nativeModels=SendDlgItemMessageW(window,134,BM_GETCHECK,0,0)==BST_CHECKED;job->endpoint=list;
            if(job->endpoint.empty()){job->endpoint=address;const std::wstring suffix=L"/chat/completions";if(job->endpoint.size()>=suffix.size() && job->endpoint.compare(job->endpoint.size()-suffix.size(),suffix.size(),suffix)==0)job->endpoint.resize(job->endpoint.size()-suffix.size());while(!job->endpoint.empty() && job->endpoint.back()==L'/')job->endpoint.pop_back();if(job->nativeModels){const std::wstring compatible=L"/compatible-mode/v1";if(job->endpoint.size()>=compatible.size() && job->endpoint.compare(job->endpoint.size()-compatible.size(),compatible.size(),compatible)==0)job->endpoint.resize(job->endpoint.size()-compatible.size());job->endpoint+=L"/api/v1/models";}else job->endpoint+=L"/models";

            }
            if(AiEndpoint_Validate(address)!=AI_OK || AiEndpoint_Validate(job->endpoint.c_str())!=AI_OK){SecureZeroMemory(key,sizeof(key));Status(panel,AP_ERROR);return 0;}
            // Reuse an origin-bound client to prevent a custom list URL receiving another host's key.
            job->client=std::make_shared<Client>(address);BOOL valid=FALSE;job->timeout=GetDlgItemInt(window,104,&valid,FALSE);
            auto secret=Utf8(key);SecureZeroMemory(key,sizeof(key));
            if(!secret.empty()){job->secret=static_cast<char*>(malloc(secret.size()+1));if(!job->secret)throw std::bad_alloc();memcpy(job->secret,secret.c_str(),secret.size()+1);SecureZeroMemory(secret.data(),secret.size());}
            else {wchar_t profile[73]{};if((identity[0]?AiCredential_AccountProfile(address,identity,profile,73):AiCredential_ServiceProfile(address,profile,73))!=ERROR_SUCCESS){Status(panel,AP_ERROR);return 0;}DWORD loaded=AiCredential_Load(profile,&job->secret);if(loaded!=ERROR_SUCCESS && loaded!=ERROR_NOT_FOUND){Status(panel,AP_ERROR);return 0;}}
            if(!valid || job->timeout<100 || job->timeout>120000){Status(panel,AP_ERROR);return 0;}
            panel->job=job;panel->published=false;
            std::thread([job]{AI_HTTP_CONFIG_DATA config{job->endpoint.c_str(),nullptr,job->secret,job->timeout,Canceled,job.get(),job->client->handle};job->status=job->nativeModels?AiHttp_ModelPages(&config,&job->result):AiHttp_Models(&config,&job->result);AiCredential_Free(job->secret);job->secret=nullptr;job->done=true;}).detach();Busy(panel,true);SetWindowTextW(GetDlgItem(window,130),L"正在获取模型列表…");return 0;
        }
        if(id==126) {
            AI_PANEL_CAPTURE request{AI_CONNECTION_TEST,""};
            if(!SendMessageW(panel->parent,NP3_WM_AI_CAPTURE,0,reinterpret_cast<LPARAM>(&request))) Status(panel,AP_ERROR);
            return 0;
        }
        if ((id >= 110 && id <=113) || id ==117) {
            const int selected=static_cast<int>(SendDlgItemMessageW(window,116,CB_GETCURSEL,0,0));
            if(id==117 && (selected<0 || selected>AI_ASK-AI_SUMMARIZE)) { Status(panel,AP_ERROR); return 0; }
            wchar_t instruction[4097]{}; GetWindowTextW(GetDlgItem(window,120),instruction,4097); auto text=Utf8(instruction);
            if (text.size()>4096) { Status(panel,AP_ERROR); return 0; }
            AI_PANEL_CAPTURE request{id==117?AI_SUMMARIZE+selected:id-110,text.c_str()};
            if (!SendMessageW(panel->parent,NP3_WM_AI_CAPTURE,0,reinterpret_cast<LPARAM>(&request))) Status(panel,AP_ERROR);
        } else if (id == 114 && panel->job) panel->job->cancel=true;
        else if (id == 115 && panel->job && panel->published && panel->job->status == AI_HTTP_OK && panel->job->action != AI_EXPLAIN && panel->job->action != AI_ASK && panel->job->action != AI_CONNECTION_TEST && !panel->job->models) {
            auto job=panel->job;
            AI_PANEL_APPLY apply{job->source,job->original.data(),job->original.size(),job->result.text,job->result.length};
            if (SendMessageW(panel->parent,NP3_WM_AI_APPLY,0,reinterpret_cast<LPARAM>(&apply))) { EnableWindow(GetDlgItem(window,115),FALSE); Status(panel,AP_READY); }
            else Status(panel,AP_STALE);
        } else if (id ==105 || id==106) {
            AI_PANEL_SAVE request{}; wchar_t key[2049]{};
            GetWindowTextW(GetDlgItem(window,101),request.settings.endpoint,2049); GetWindowTextW(GetDlgItem(window,102),request.settings.model,257);
            const auto chat=ChatAddress(request.settings.endpoint);wcscpy_s(request.settings.endpoint,chat.c_str());SetDlgItemTextW(window,101,request.settings.endpoint);
            BOOL valid=FALSE; request.settings.timeoutMs=GetDlgItemInt(window,104,&valid,FALSE);
            GetWindowTextW(GetDlgItem(window,119),request.settings.configurationId,65);
            if(id==105 && !request.settings.configurationId[0] && GetWindowTextLengthW(GetDlgItem(window,103))>0 && SendDlgItemMessageW(window,125,CB_GETCURSEL,0,0)>0){swprintf_s(request.settings.configurationId,L"account-%lu-%llu",GetCurrentProcessId(),GetTickCount64());SetWindowTextW(GetDlgItem(window,119),request.settings.configurationId);}
            GetWindowTextW(GetDlgItem(window,103),key,2049); auto secret=Utf8(key); SecureZeroMemory(key,sizeof(key)); request.secret=secret.c_str();
            bool saved=valid && SendMessageW(panel->parent,NP3_WM_AI_SAVE,id==106,reinterpret_cast<LPARAM>(&request));
            SecureZeroMemory(secret.data(),secret.size());
            if (saved) { if(id==105){panel->activeId=request.settings.configurationId;panel->activeModel=request.settings.model;} SetWindowTextW(GetDlgItem(window,103),L""); Profiles(panel); } Status(panel,saved?AP_SAVED:AP_ERROR);
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
    int reserve=(std::min)((std::max)(0,height*2/3),MulDiv(panel->settingsExpanded?480:360,GetDpiForWindow(panel->parent),96));
    MoveWindow(panel->window,x,y+height-reserve,width,reserve,TRUE); return reserve;
}
extern "C" BOOL AiPanel_Run(void* context,const AI_PANEL_SETTINGS* settings,const AI_PANEL_SOURCE* source,int action,const char* original,size_t length,const char* instruction) {
    auto panel=State(context); if(!panel || !panel->window || AiPanel_IsBusy(context) || !settings || !source || (!original && length) || length>256*1024) return FALSE;
    try {
        if(action<AI_EXPLAIN || action>AI_CONNECTION_TEST || AiEndpoint_Validate(settings->endpoint)!=AI_OK || !settings->model[0] || settings->timeoutMs<100 || settings->timeoutMs>120000) return FALSE;
        if((action==AI_GENERATE || action==AI_ASK) && (!instruction || !*instruction)) return FALSE;
        auto job=std::make_shared<Job>(); job->source=*source; job->action=action; job->endpoint=settings->endpoint; job->model=Utf8(settings->model); job->timeout=settings->timeoutMs;
        job->original.assign(original?original:"",length); job->instruction=instruction?instruction:"";
        if(job->model.size()>256 || job->instruction.size()>4096) return FALSE;
        wchar_t profile[73]{}; if((settings->configurationId[0]?AiCredential_AccountProfile(settings->endpoint,settings->configurationId,profile,73):AiCredential_ServiceProfile(settings->endpoint,profile,73))!=ERROR_SUCCESS) return FALSE;
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
            panel->settingsExpanded=false;panel->activeId=settings->configurationId;panel->activeModel=settings->model;panel->parent=parent; for(int i=0;i<AP_COUNT;++i) panel->labels[i]=labels[i];
            WNDCLASSW type{}; type.lpfnWndProc=Proc; type.hInstance=GetModuleHandleW(nullptr); type.lpszClassName=ClassName; type.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_BTNFACE+1); RegisterClassW(&type);
            auto holder=std::make_unique<Owner>(panel);
            HWND window=CreateWindowExW(WS_EX_CLIENTEDGE,ClassName,nullptr,WS_CHILD|WS_VISIBLE,0,0,0,0,parent,nullptr,type.hInstance,nullptr); if(!window) return;
            panel->window=window; SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(holder.release()));
            auto control=[&](const wchar_t* name,const wchar_t* text,DWORD style,int id) {
                HWND child=CreateWindowExW(0,name,text,WS_CHILD|WS_VISIBLE|style,0,0,0,0,window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),type.hInstance,nullptr);
                if(!child) throw std::bad_alloc(); SendMessageW(child,WM_SETFONT,reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)),FALSE); return child;
            };
            control(L"BUTTON",L"模型设置",BS_PUSHBUTTON|WS_TABSTOP,107);
            for(int i=0;i<4;++i) { control(L"STATIC",labels[i],0,201+i); control(i==1?L"COMBOBOX":L"EDIT",L"",i==1?CBS_DROPDOWN|WS_VSCROLL|WS_TABSTOP:WS_BORDER|ES_AUTOHSCROLL|WS_TABSTOP|(i==2?ES_PASSWORD:0)|(i==3?ES_NUMBER:0),101+i); }
            SendMessageW(GetDlgItem(window,101),EM_SETLIMITTEXT,2048,0); SendMessageW(GetDlgItem(window,102),CB_LIMITTEXT,256,0); SendMessageW(GetDlgItem(window,103),EM_SETLIMITTEXT,2048,0);
            SetWindowTextW(GetDlgItem(window,101),settings->endpoint); SetWindowTextW(GetDlgItem(window,102),settings->model); SetDlgItemInt(window,104,settings->timeoutMs,FALSE);
            const int ids[]={105,106,110,111,112,113,114,115}; const int texts[]={AP_SAVE,AP_DELETE,AP_EXPLAIN,AP_TRANSLATE,AP_REWRITE,AP_GENERATE,AP_CANCEL,AP_APPLY};
            for(int i=0;i<8;++i) control(L"BUTTON",labels[texts[i]],BS_PUSHBUTTON|WS_TABSTOP,ids[i]);
            control(L"BUTTON",L"测试（计费）",BS_PUSHBUTTON|WS_TABSTOP,126);
            control(L"BUTTON",L"百炼分页",BS_AUTOCHECKBOX|WS_TABSTOP,134);control(L"BUTTON",L"获取模型",BS_PUSHBUTTON|WS_TABSTOP,132);control(L"STATIC",L"模型列表地址",0,208);control(L"EDIT",L"",WS_BORDER|ES_AUTOHSCROLL|WS_TABSTOP,133);SendDlgItemMessageW(window,133,EM_SETLIMITTEXT,2048,0);SendDlgItemMessageW(window,133,EM_SETCUEBANNER,TRUE,reinterpret_cast<LPARAM>(L"留空自动推导；特殊接口可填写同源完整地址"));
            control(L"STATIC",labels[AP_INSTRUCTION],0,205); control(L"EDIT",L"",WS_BORDER|ES_MULTILINE|ES_AUTOVSCROLL|WS_VSCROLL|WS_TABSTOP,120); SendMessageW(GetDlgItem(window,120),EM_SETLIMITTEXT,4096,0);
            HWND actions=control(L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_VSCROLL|WS_TABSTOP,116);
            for(const wchar_t* name : {L"摘要",L"润色",L"扩写",L"缩写",L"纠错",L"当前文件问答"}) SendMessageW(actions,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(name));
            SendMessageW(actions,CB_SETCURSEL,0,0); control(L"BUTTON",L"执行",BS_PUSHBUTTON|WS_TABSTOP,117);
            control(L"STATIC",L"配置ID（空=旧账号）",0,118);
            control(L"EDIT",settings->configurationId,WS_BORDER|ES_AUTOHSCROLL|WS_TABSTOP,119); SendMessageW(GetDlgItem(window,119),EM_SETLIMITTEXT,64,0);
            control(L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_VSCROLL|WS_TABSTOP,123); control(L"BUTTON",L"移出列表",BS_PUSHBUTTON|WS_TABSTOP,124);
            HWND providers=control(L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_VSCROLL|WS_TABSTOP,125);
            for(const wchar_t* name:{L"服务地址预设",L"Kimi（中国大陆）",L"DeepSeek",L"Qwen / 百炼（北京）",L"第三方兼容接口",L"OpenAI"}) SendMessageW(providers,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(name));
            SendMessageW(providers,CB_SETCURSEL,0,0);SendMessageW(GetDlgItem(window,102),EM_SETCUEBANNER,TRUE,reinterpret_cast<LPARAM>(L"填写平台可用模型ID")); Profiles(panel);
            control(L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_VSCROLL|WS_TABSTOP,127);control(L"EDIT",L"",WS_BORDER|ES_AUTOHSCROLL|WS_TABSTOP,128);SendDlgItemMessageW(window,128,EM_SETLIMITTEXT,64,0);SendDlgItemMessageW(window,128,EM_SETCUEBANNER,TRUE,reinterpret_cast<LPARAM>(L"提示词名称"));control(L"BUTTON",L"保存提示词",BS_PUSHBUTTON|WS_TABSTOP,129);control(L"BUTTON",L"删除提示词",BS_PUSHBUTTON|WS_TABSTOP,131);Prompts(panel);
            control(L"STATIC",labels[AP_ORIGINAL],0,206); control(L"STATIC",labels[AP_RESULT],0,207);
            for(int id:{121,122}) { HWND edit=control(L"RICHEDIT50W",L"",WS_BORDER|WS_VSCROLL|WS_HSCROLL|ES_MULTILINE|ES_READONLY|ES_AUTOVSCROLL|ES_AUTOHSCROLL|WS_TABSTOP,id); SendMessageW(edit,EM_EXLIMITTEXT,0,2*1024*1024); }
            control(L"STATIC",labels[AP_READY],0,130); Busy(panel,false); if(!SetTimer(window,1,50,nullptr)) throw std::bad_alloc();
            panel->theme.Update(window,parent);
        }
        RECT rect{}; GetClientRect(parent,&rect); SendMessageW(parent,WM_SIZE,0,MAKELPARAM(rect.right,rect.bottom));
    } catch(...) { if(panel->window) DestroyWindow(panel->window); }
}
