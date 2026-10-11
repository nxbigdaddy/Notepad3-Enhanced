#include "HistoryService.h"
#include "HistoryCore.h"
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <chrono>
namespace {
struct Event { std::wstring store; np3::HistoryChange change; size_t maximum; };
struct State {
    std::mutex mutex;
    std::condition_variable changed;
    std::deque<Event> queue;
    bool running=false;
    DWORD error=ERROR_SUCCESS;
};
std::shared_ptr<State> state=std::make_shared<State>();
void Run(std::shared_ptr<State> s) noexcept {
    for (;;) {
        Event event;
        {
            std::lock_guard<std::mutex> lock(s->mutex);
            if (s->queue.empty()) { s->running=false; s->changed.notify_all(); return; }
            event=std::move(s->queue.front()); s->queue.pop_front();
        }
        DWORD error=ERROR_SUCCESS;
        try {
            std::vector<np3::HistoryEntry> entries;
            if (!np3::HistoryUpdate(event.store,event.change,event.maximum,entries,error) && !error) error=ERROR_WRITE_FAULT;
            // A removed history record must stay removed when an old tab saves its position.
            if(event.change.action==np3::HistoryAction::Position && error==ERROR_NOT_FOUND)error=ERROR_SUCCESS;
        } catch (...) { error=ERROR_NOT_ENOUGH_MEMORY; }
        if (error) { std::lock_guard<std::mutex> lock(s->mutex); s->error=error; }
    }
}
}
static BOOL Enqueue(const wchar_t* store,const wchar_t* path,size_t maximum,np3::HistoryAction action,unsigned long long line,unsigned long long column) {
    if (!store || !*store || !path || !*path || maximum==0 || maximum>10000) return FALSE;
    try {
        Event event; event.store=store; event.change.path=path; event.maximum=maximum;
        event.change.action=action;event.change.line=line;event.change.column=column;
        GetSystemTimeAsFileTime(&event.change.time);
        std::lock_guard<std::mutex> lock(state->mutex);
        if (state->queue.size()>=1024) { state->error=ERROR_NOT_ENOUGH_QUOTA; return FALSE; }
        state->queue.push_back(std::move(event));
        if (!state->running) {
            state->running=true;
            try { std::thread(Run,state).detach(); }
            catch (...) { state->running=false; state->queue.pop_back(); state->error=ERROR_NOT_ENOUGH_MEMORY; return FALSE; }
        }
        return TRUE;
    } catch (...) { return FALSE; }
}
BOOL HistoryService_Record(const wchar_t* store,const wchar_t* path,size_t maximum){return Enqueue(store,path,maximum,np3::HistoryAction::Open,0,0);}
BOOL HistoryService_Position(const wchar_t* store,const wchar_t* path,size_t maximum,unsigned long long line,unsigned long long column){
    if(!line || !column)return FALSE;
    return Enqueue(store,path,maximum,np3::HistoryAction::Position,line,column);
}
BOOL HistoryService_GetPosition(const wchar_t* store,const wchar_t* path,unsigned long long* line,unsigned long long* column){
    if(!store || !path || !line || !column)return FALSE;
    try{
        std::vector<np3::HistoryEntry> entries;DWORD error=0;if(!np3::HistoryRead(store,entries,error))return FALSE;
        for(const auto& entry:entries)if(CompareStringOrdinal(entry.path.c_str(),-1,path,-1,TRUE)==CSTR_EQUAL && entry.line && entry.column){*line=entry.line;*column=entry.column;return TRUE;}
    }catch(...){}
    return FALSE;
}
BOOL HistoryService_Flush(DWORD timeout) {
    std::unique_lock<std::mutex> lock(state->mutex);
    return state->changed.wait_for(lock,std::chrono::milliseconds(timeout),[] { return !state->running && state->queue.empty(); }) ? TRUE : FALSE;
}
DWORD HistoryService_Error(void) {
    std::lock_guard<std::mutex> lock(state->mutex); return state->error;
}
BOOL HistoryService_GetEntry(const wchar_t* store,const wchar_t* path,unsigned long long* count) {
    if (!store || !path || !count) return FALSE;
    try {
        std::vector<np3::HistoryEntry> entries; DWORD error=0;
        if (!np3::HistoryRead(store,entries,error)) return FALSE;
        for (const auto& entry:entries) if (CompareStringOrdinal(entry.path.c_str(),-1,path,-1,TRUE)==CSTR_EQUAL) { *count=entry.count; return TRUE; }
    } catch (...) {}
    return FALSE;
}
