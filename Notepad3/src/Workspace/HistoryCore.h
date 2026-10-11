#pragma once
#include <windows.h>
#include <string>
#include <vector>
namespace np3 {
struct HistoryEntry { std::wstring path; FILETIME opened{}; unsigned long long count=0,line=0,column=0; bool pinned=false; };
enum class HistoryAction { Open, Position, Pin, Remove, Clear, Relocate };
struct HistoryChange { HistoryAction action=HistoryAction::Open; std::wstring path; FILETIME time{}; unsigned long long line=0,column=0; bool pinned=false; std::wstring destination; };
// Store paths and metadata only. Each mutation reloads under a cross-process lock.
bool HistoryRead(const std::wstring& store,std::vector<HistoryEntry>& entries,DWORD& error);
bool HistoryUpdate(const std::wstring& store,const HistoryChange&,size_t maximum,std::vector<HistoryEntry>& entries,DWORD& error);
}
