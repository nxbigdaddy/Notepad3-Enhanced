#include "SessionStoreC.h"
#include "SessionStore.h"
#include <windows.h>
#include <dpapi.h>
#include <algorithm>
#include <limits>
#include <memory>
#include <stdexcept>
#pragma comment(lib, "crypt32.lib")

namespace np3 {
namespace {
constexpr size_t PayloadLimit = 64 * 1024 * 1024;
constexpr size_t ProtectedLimit = PayloadLimit + 1024 * 1024;
constexpr size_t DocumentLimit = 32 * 1024 * 1024;
constexpr std::string_view Magic = "NP3SESS4";
void Check(bool condition) { if (!condition) throw std::runtime_error("Invalid or oversized session"); }
std::uint32_t Checksum(std::string_view bytes) {
    std::uint32_t crc = 0xFFFFFFFF;
    for (unsigned char c : bytes) {
        crc ^= c;
        for (int bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}
void Number(std::string& out, std::uint64_t value, unsigned width) {
    Check(out.size() <= PayloadLimit - width);
    for (unsigned i = 0; i < width; ++i) out.push_back(static_cast<char>((value >> (i * 8)) & 255));
}
void Bytes(std::string& out, std::string_view value, size_t limit) {
    Check(value.size() <= limit);
    Number(out, value.size(), 4);
    Check(value.size() <= PayloadLimit - out.size()); out.append(value);
}
struct Reader {
    std::string_view bytes;
    size_t pos = 0;
    std::uint64_t number(unsigned width) {
        Check(width <= bytes.size() - pos);
        std::uint64_t value = 0;
        for (unsigned i = 0; i < width; ++i) value |= static_cast<std::uint64_t>(static_cast<unsigned char>(bytes[pos++])) << (i * 8);
        return value;
    }
    std::string text(size_t limit) {
        const auto length = static_cast<size_t>(number(4));
        Check(length <= limit && length <= bytes.size() - pos);
        std::string result(bytes.substr(pos, length)); pos += length; return result;
    }
};
void Validate(const SessionDocument& doc) {
    Check(doc.path.size() <= 32767 && doc.path.find(L'\0') == std::wstring::npos);
    Check(doc.text.size() <= DocumentLimit && doc.selection.size() <= 1024 * 1024);
    Check(doc.encoding <= 65535 && doc.eol <= 2 && doc.flags <= 15);
    const bool disk = (doc.flags & NP3_SESSION_DISK_BACKED) != 0;
    if (disk) Check(!doc.path.empty() && doc.text.empty() && !(doc.flags & NP3_SESSION_MODIFIED));
    const auto positionLimit = disk ? static_cast<std::uint64_t>(UINT32_MAX) : doc.text.size();
    Check(doc.anchor <= positionLimit && doc.caret <= positionLimit);
    Check(doc.firstLine <= positionLimit && doc.xOffset <= INT_MAX && doc.zoom >= -10 && doc.zoom <= 20);
    Check(doc.tabWidth > 0 && doc.tabWidth <= 1024 && doc.indent <= 1024 && doc.wrapMode <= 3);
    Check(doc.folds.size() <= (disk ? 131072 : doc.text.size() + 1));
    for (const auto& view : doc.views) {
        if (!view.valid) continue;
        Check(view.selection.size() <= 1024 * 1024 && view.anchor <= positionLimit && view.caret <= positionLimit);
        Check(view.firstLine <= positionLimit && view.xOffset <= INT_MAX && view.zoom >= -10 && view.zoom <= 20 && view.wrapMode <= 3);
        Check(view.folds.size() <= (disk ? 131072 : doc.text.size() + 1));
        std::uint64_t last = 0;
        for (size_t i = 0; i < view.folds.size(); ++i) { Check(view.folds[i] <= positionLimit && (!i || view.folds[i] > last)); last = view.folds[i]; }
    }
    std::uint64_t previous = 0;
    for (size_t i = 0; i < doc.folds.size(); ++i) {
        Check(doc.folds[i] <= positionLimit && (!i || doc.folds[i] > previous)); previous = doc.folds[i];
    }
}
std::wstring Absolute(const std::wstring& path) {
    Check(!path.empty() && path.find(L'\0') == std::wstring::npos && path.size() <= 32767);
    if (path.rfind(L"\\\\?\\", 0) == 0) return path;
    const DWORD length = GetFullPathNameW(path.c_str(), 0, nullptr, nullptr);
    Check(length > 0 && length < 32768);
    std::wstring full(length, L'\0');
    const DWORD written = GetFullPathNameW(path.c_str(), length, full.data(), nullptr);
    Check(written > 0 && written < length); full.resize(written);
    return full.rfind(L"\\\\", 0) == 0 ? L"\\\\?\\UNC\\" + full.substr(2) : L"\\\\?\\" + full;
}
struct Handle {
    HANDLE value = INVALID_HANDLE_VALUE;
    ~Handle() { if (value != INVALID_HANDLE_VALUE) CloseHandle(value); }
};
struct Blob {
    DATA_BLOB value = {};
    ~Blob() { if (value.pbData) { SecureZeroMemory(value.pbData, value.cbData); LocalFree(value.pbData); } }
};
}
std::string EncodeSession(const SessionSnapshot& session) {
    Check(!session.documents.empty() && session.documents.size() <= 64 && session.active < session.documents.size());
    std::string bytes(Magic); Number(bytes, session.documents.size(), 4); Number(bytes, session.active, 4);
    for (const auto& doc : session.documents) {
        Validate(doc);
        Number(bytes, doc.path.size(), 4);
        for (wchar_t c : doc.path) Number(bytes, static_cast<std::uint16_t>(c), 2);
        Bytes(bytes, doc.text, DocumentLimit); Bytes(bytes, doc.selection, 1024 * 1024);
        Number(bytes, doc.encoding, 4); Number(bytes, doc.eol, 4); Number(bytes, doc.flags, 4);
        Number(bytes, doc.anchor, 8); Number(bytes, doc.caret, 8); Number(bytes, doc.firstLine, 8);
        Number(bytes, doc.codePage, 4); Number(bytes, doc.tabWidth, 4); Number(bytes, doc.indent, 4); Number(bytes, doc.wrapMode, 4);
        Number(bytes, doc.xOffset, 4); Number(bytes, static_cast<std::uint32_t>(doc.zoom), 4);
        Number(bytes, doc.folds.size(), 4);
        for (auto line : doc.folds) Number(bytes, line, 8);
    }
    Check(session.activeView <= 1 && session.splitRatio > 0 && session.splitRatio < 10000 && (session.splitEnabled || !session.activeView));
    Number(bytes, session.splitEnabled, 4); Number(bytes, session.activeView, 4); Number(bytes, session.splitRatio, 4);
    for (const auto& doc : session.documents) for (const auto& view : doc.views) {
        Number(bytes, view.valid, 4);
        if (!view.valid) continue;
        Bytes(bytes, view.selection, 1024 * 1024);
        Number(bytes, view.anchor, 8); Number(bytes, view.caret, 8); Number(bytes, view.firstLine, 8);
        Number(bytes, view.xOffset, 4); Number(bytes, view.wrapMode, 4); Number(bytes, static_cast<std::uint32_t>(view.zoom), 4);
        Number(bytes, view.folds.size(), 4); for (auto line : view.folds) Number(bytes, line, 8);
    }
    Number(bytes, Checksum(bytes), 4); return bytes;
}
SessionSnapshot DecodeSession(std::string_view bytes) {
    Check(bytes.size() >= 20 && bytes.size() <= PayloadLimit && (bytes.substr(0, Magic.size()) == Magic || bytes.substr(0, Magic.size()) == "NP3SESS2" || bytes.substr(0, Magic.size()) == "NP3SESS3"));
    Reader checksum{bytes.substr(bytes.size() - 4)};
    Check(Checksum(bytes.substr(0, bytes.size() - 4)) == checksum.number(4));
    Reader reader{bytes.substr(0, bytes.size() - 4), Magic.size()};
    const auto count = reader.number(4);
    SessionSnapshot session; session.active = static_cast<std::uint32_t>(reader.number(4));
    Check(count > 0 && count <= 64 && session.active < count);
    for (std::uint64_t i = 0; i < count; ++i) {
        SessionDocument doc;
        const auto pathLength = reader.number(4); Check(pathLength <= 32767);
        for (std::uint64_t j = 0; j < pathLength; ++j) doc.path.push_back(static_cast<wchar_t>(reader.number(2)));
        doc.text = reader.text(DocumentLimit); doc.selection = reader.text(1024 * 1024);
        doc.encoding = static_cast<std::uint32_t>(reader.number(4));
        doc.eol = static_cast<std::uint32_t>(reader.number(4)); doc.flags = static_cast<std::uint32_t>(reader.number(4));
        doc.anchor = reader.number(8); doc.caret = reader.number(8); doc.firstLine = reader.number(8);
        doc.codePage = static_cast<std::uint32_t>(reader.number(4)); doc.tabWidth = static_cast<std::uint32_t>(reader.number(4));
        doc.indent = static_cast<std::uint32_t>(reader.number(4)); doc.wrapMode = static_cast<std::uint32_t>(reader.number(4));
        doc.xOffset = static_cast<std::uint32_t>(reader.number(4));
        const auto zoom = reader.number(4); doc.zoom = zoom <= INT_MAX ? static_cast<std::int32_t>(zoom) : -1 - static_cast<std::int32_t>(UINT32_MAX - zoom);
        const auto folds = reader.number(4); Check(folds <= ((doc.flags & NP3_SESSION_DISK_BACKED) ? 131072 : doc.text.size() + 1) && folds <= (reader.bytes.size() - reader.pos) / 8);
        for (std::uint64_t j = 0; j < folds; ++j) doc.folds.push_back(reader.number(8));
        Validate(doc); session.documents.push_back(std::move(doc));
    }
    if (bytes.substr(0, Magic.size()) == Magic) {
        const auto split = reader.number(4); Check(split <= 1); session.splitEnabled = split != 0;
        session.activeView = static_cast<std::uint32_t>(reader.number(4)); session.splitRatio = static_cast<std::uint32_t>(reader.number(4));
        Check(session.activeView <= 1 && session.splitRatio > 0 && session.splitRatio < 10000 && (session.splitEnabled || !session.activeView));
        for (auto& doc : session.documents) {
            for (auto& view : doc.views) {
                const auto valid = reader.number(4); Check(valid <= 1); view.valid = valid != 0;
                if (!view.valid) continue;
                view.selection = reader.text(1024 * 1024);
                view.anchor = reader.number(8); view.caret = reader.number(8); view.firstLine = reader.number(8);
                view.xOffset = static_cast<std::uint32_t>(reader.number(4)); view.wrapMode = static_cast<std::uint32_t>(reader.number(4));
                const auto zoom = reader.number(4); view.zoom = zoom <= INT_MAX ? static_cast<std::int32_t>(zoom) : -1 - static_cast<std::int32_t>(UINT32_MAX - zoom);
                const auto folds = reader.number(4); Check(folds <= ((doc.flags & NP3_SESSION_DISK_BACKED) ? 131072 : doc.text.size() + 1) && folds <= (reader.bytes.size() - reader.pos) / 8);
                for (std::uint64_t j = 0; j < folds; ++j) view.folds.push_back(reader.number(8));
            }
            Validate(doc);
        }
    }
    Check(reader.pos == reader.bytes.size()); return session;
}
bool SaveSession(const std::wstring& path, const SessionSnapshot& session, std::uint32_t& error) noexcept {
    error = ERROR_SUCCESS;
    std::wstring temp;
    try {
        const std::wstring target = Absolute(path);
        std::string bytes = EncodeSession(session);
        DATA_BLOB input = { static_cast<DWORD>(bytes.size()), reinterpret_cast<BYTE*>(bytes.data()) }; Blob protectedBytes;
        const BOOL protectedOK = CryptProtectData(&input, L"Notepad3 workspace", nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &protectedBytes.value);
        SecureZeroMemory(bytes.data(), bytes.size());
        if (!protectedOK) { error = GetLastError(); return false; }
        Check(protectedBytes.value.cbData <= ProtectedLimit);
        // CREATE_NEW plus the process/thread/tick suffix prevents sharing temp files.
        temp = target + L".tmp-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetCurrentThreadId()) + L"-" + std::to_wstring(GetTickCount64());
        Handle file; file.value = CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, nullptr);
        if (file.value == INVALID_HANDLE_VALUE) { error = GetLastError(); temp.clear(); return false; }
        DWORD written = 0;
        if (!WriteFile(file.value, protectedBytes.value.pbData, protectedBytes.value.cbData, &written, nullptr) || written != protectedBytes.value.cbData || !FlushFileBuffers(file.value)) {
            error = GetLastError(); if (!error) error = ERROR_WRITE_FAULT;
        } else {
            CloseHandle(file.value); file.value = INVALID_HANDLE_VALUE;
            if (MoveFileExW(temp.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) { temp.clear(); return true; }
            error = GetLastError();
        }
    } catch (const std::bad_alloc&) { error = ERROR_NOT_ENOUGH_MEMORY; }
      catch (...) { error = ERROR_INVALID_DATA; }
    if (!temp.empty()) DeleteFileW(temp.c_str()); return false;
}
bool LoadSession(const std::wstring& path, SessionSnapshot& session, std::uint32_t& error) noexcept {
    error = ERROR_SUCCESS;
    try {
        const auto target = Absolute(path);
        Handle file; file.value = CreateFileW(target.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file.value == INVALID_HANDLE_VALUE) { error = GetLastError(); return false; }
        LARGE_INTEGER length;
        if (!GetFileSizeEx(file.value, &length)) { error = GetLastError(); return false; }
        Check(length.QuadPart > 0 && length.QuadPart <= static_cast<LONGLONG>(ProtectedLimit));
        std::string bytes(static_cast<size_t>(length.QuadPart), '\0'); DWORD read = 0;
        if (!ReadFile(file.value, bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr) || read != bytes.size()) { error = ERROR_READ_FAULT; return false; }
        DATA_BLOB input = { static_cast<DWORD>(bytes.size()), reinterpret_cast<BYTE*>(bytes.data()) }; Blob plain;
        if (!CryptUnprotectData(&input, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &plain.value)) { error = GetLastError(); return false; }
        Check(plain.value.cbData <= PayloadLimit);
        auto decoded = DecodeSession(std::string_view(reinterpret_cast<const char*>(plain.value.pbData), plain.value.cbData));
        session = std::move(decoded); return true;
    } catch (const std::bad_alloc&) { error = ERROR_NOT_ENOUGH_MEMORY; }
      catch (...) { error = ERROR_INVALID_DATA; }
    return false;
}
}

extern "C" NP3_SESSION Session_Create(void) {
    try { return new np3::SessionSnapshot(); } catch (...) { return nullptr; }
}
extern "C" void Session_Free(NP3_SESSION session) {
    delete static_cast<np3::SessionSnapshot*>(session);
}
extern "C" int Session_Add(NP3_SESSION session, const NP3_SESSION_DOCUMENT* source) {
    if (!session || !source || (source->textLength && !source->text) || (source->selectionLength && !source->selection) || (source->foldCount && !source->folds)) return 0;
    try {
        auto& snapshot = *static_cast<np3::SessionSnapshot*>(session);
        if (snapshot.documents.size() >= 64 || source->textLength > 32 * 1024 * 1024 || source->selectionLength > 1024 * 1024 || source->foldCount > ((source->flags & NP3_SESSION_DISK_BACKED) ? 131072 : source->textLength + 1)) return 0;
        if (source->path && wcsnlen_s(source->path, 32768) >= 32768) return 0;
        np3::SessionDocument doc;
        if (source->path) doc.path = source->path;
        if (source->textLength) doc.text.assign(source->text, source->textLength);
        if (source->selectionLength) doc.selection.assign(source->selection, source->selectionLength);
        if (source->foldCount) doc.folds.assign(source->folds, source->folds + source->foldCount);
        doc.encoding = source->encoding; doc.eol = source->eol; doc.flags = source->flags;
        doc.anchor = source->anchor; doc.caret = source->caret; doc.firstLine = source->firstLine;
        doc.xOffset = source->xOffset; doc.zoom = source->zoom;
        doc.codePage = source->codePage; doc.tabWidth = source->tabWidth; doc.indent = source->indent; doc.wrapMode = source->wrapMode;
        for (size_t i = 0; i < 2; ++i) {
            const auto& input = source->views[i]; auto& view = doc.views[i];
            if (input.valid > 1) return 0;
            view.valid = input.valid != 0; if (!view.valid) continue;
            if (input.selectionLength > 1024 * 1024 || (input.selectionLength && !input.selection) || (input.foldCount && !input.folds) || input.foldCount > ((source->flags & NP3_SESSION_DISK_BACKED) ? 131072 : source->textLength + 1)) return 0;
            if (input.selectionLength) view.selection.assign(input.selection, input.selectionLength);
            if (input.foldCount) view.folds.assign(input.folds, input.folds + input.foldCount);
            view.anchor = input.anchor; view.caret = input.caret; view.firstLine = input.firstLine;
            view.xOffset = input.xOffset; view.wrapMode = input.wrapMode; view.zoom = input.zoom;
        }
        // Validate before mutating the builder so rejected entries cannot poison it.
        np3::Validate(doc);
        snapshot.documents.push_back(std::move(doc)); return 1;
    } catch (...) { return 0; }
}
extern "C" int Session_SetActive(NP3_SESSION session, uint32_t active) {
    if (!session) return 0;
    auto& snapshot = *static_cast<np3::SessionSnapshot*>(session);
    if (active >= snapshot.documents.size()) return 0;
    snapshot.active = active; return 1;
}
extern "C" int Session_SetLayout(NP3_SESSION session, uint32_t split, uint32_t active, uint32_t ratio) {
    if (!session || split > 1 || active > 1 || !ratio || ratio >= 10000 || (!split && active)) return 0;
    auto& snapshot = *static_cast<np3::SessionSnapshot*>(session); snapshot.splitEnabled = split != 0; snapshot.activeView = active; snapshot.splitRatio = ratio; return 1;
}
extern "C" int Session_GetLayout(NP3_SESSION session, uint32_t* split, uint32_t* active, uint32_t* ratio) {
    if (!session || !split || !active || !ratio) return 0;
    const auto& snapshot = *static_cast<np3::SessionSnapshot*>(session); *split = snapshot.splitEnabled; *active = snapshot.activeView; *ratio = snapshot.splitRatio; return 1;
}
extern "C" size_t Session_Count(NP3_SESSION session) { return session ? static_cast<np3::SessionSnapshot*>(session)->documents.size() : 0; }
extern "C" uint32_t Session_Active(NP3_SESSION session) { return session ? static_cast<np3::SessionSnapshot*>(session)->active : 0; }
extern "C" int Session_Get(NP3_SESSION session, size_t index, NP3_SESSION_DOCUMENT* output) {
    if (!session || !output) return 0;
    const auto& snapshot = *static_cast<np3::SessionSnapshot*>(session);
    if (index >= snapshot.documents.size()) return 0;
    const auto& doc = snapshot.documents[index];
    *output = { doc.path.c_str(), doc.text.data(), doc.text.size(), doc.selection.data(), doc.selection.size(), doc.folds.data(), doc.folds.size(), doc.encoding, doc.eol, doc.flags, doc.anchor, doc.caret, doc.firstLine, doc.xOffset, doc.codePage, doc.tabWidth, doc.indent, doc.wrapMode, doc.zoom };
    for (size_t i = 0; i < 2; ++i) { const auto& view = doc.views[i];
        output->views[i] = {view.selection.data(), view.selection.size(), view.folds.data(), view.folds.size(), view.anchor, view.caret, view.firstLine, view.xOffset, view.wrapMode, view.zoom, view.valid};
    }
    return 1;
}
extern "C" int Session_Save(NP3_SESSION session, const wchar_t* path, uint32_t* error) {
    if (!session || !path || !error) return 0;
    try { return np3::SaveSession(path, *static_cast<np3::SessionSnapshot*>(session), *error); }
    catch (...) { *error = ERROR_NOT_ENOUGH_MEMORY; return 0; }
}
extern "C" int Session_Load(const wchar_t* path, NP3_SESSION* output, uint32_t* error) {
    if (!path || !output || !error) return 0;
    try {
        auto snapshot = std::make_unique<np3::SessionSnapshot>();
        if (!np3::LoadSession(path, *snapshot, *error)) return 0;
        *output = snapshot.release(); return 1;
    } catch (...) { *error = ERROR_NOT_ENOUGH_MEMORY; return 0; }
}
