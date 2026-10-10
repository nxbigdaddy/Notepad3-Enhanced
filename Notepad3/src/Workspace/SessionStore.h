#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace np3 {
struct SessionDocument {
    std::wstring path;
    std::string text, selection;
    std::vector<std::uint64_t> folds;
    std::uint32_t encoding = 0, eol = 0, flags = 0;
    std::uint64_t anchor = 0, caret = 0, firstLine = 0;
    std::uint32_t xOffset = 0;
    std::int32_t zoom = 0;
};
struct SessionSnapshot {
    std::vector<SessionDocument> documents;
    std::uint32_t active = 0;
};
// Exceptions describe metadata/size errors without containing document text.
std::string EncodeSession(const SessionSnapshot& session);
SessionSnapshot DecodeSession(std::string_view bytes);
// Per-user DPAPI protected storage, flushed and atomically replaced. A failure
// leaves the previous file intact; no file encryption keys are serialized.
bool SaveSession(const std::wstring& path, const SessionSnapshot& session, std::uint32_t& error) noexcept;
bool LoadSession(const std::wstring& path, SessionSnapshot& session, std::uint32_t& error) noexcept;
}
