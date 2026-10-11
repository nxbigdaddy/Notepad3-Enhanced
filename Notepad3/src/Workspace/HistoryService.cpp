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
        } catch (...) { error=ERROR_NOT_ENOUGH_MEMORY; }
        if (error) { std::lock_guard<std::mutex> lock(s->mutex); s->error=error; }
    }
}
}
BOOL HistoryService_Record(const wchar_t* store,const wchar_t* path,size_t maximum) {
    if (!store || !*store || !path || !*path || maximum==0 || maximum>10000) return FALSE;
    try {
        Event event; event.store=store; event.change.path=path; event.maximum=maximum;
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
BOOL HistoryService_Flush(DWORD timeout) {
    std::unique_lock<std::mutex> lock(state->mutex);
    return state->changed.wait_for(lock,std::chrono::milliseconds(timeout),[] { return !state->running && state->queue.empty(); }) ? TRUE : FALSE;
}
DWORD HistoryService_Error(void) {
    std::lock_guard<std::mutex> lock(state->mutex); return state->error;
}
