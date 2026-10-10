#include "../Notepad3/src/Workspace/SessionStoreC.h"
#include "../Notepad3/src/Workspace/SessionStore.h"
#include <windows.h>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <stdexcept>

int checks = 0;
void expect(bool condition, const char* label) {
    ++checks; if (!condition) { std::fprintf(stderr, "FAIL %s\n", label); std::exit(1); }
}
template<class Function> void rejected(Function function) {
    bool failed = false;
    try { function(); } catch (const std::exception&) { failed = true; }
    expect(failed, "invalid snapshot rejected");
}
std::string disk(const std::wstring& path) {
    std::ifstream file(path, std::ios::binary);
    return { std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>() };
}
int main() {
    np3::SessionSnapshot session;
    np3::SessionDocument first;
    first.path = L"E:\\中文\\a.txt"; first.text = std::string("unsaved\0buffer\r\n", 16);
    first.encoding = 7; first.flags = 1; first.anchor = 2; first.caret = 7;
    first.firstLine = 1; first.selection = "selection-state"; first.folds = { 0, 2 }; first.zoom = -2;
    session.documents.push_back(first);
    auto second = first; second.path.clear(); second.encoding = 2; second.flags = 3;
    session.documents.push_back(second); session.active = 1;
    const auto bytes = np3::EncodeSession(session);
    auto decoded = np3::DecodeSession(bytes);
    expect(decoded.documents.size() == 2 && decoded.active == 1, "document order and active tab");
    expect(decoded.documents[0].path == first.path && decoded.documents[0].text == first.text, "Unicode path and binary unsaved text");
    expect(decoded.documents[1].path.empty() && decoded.documents[1].encoding == 2 && decoded.documents[1].flags == 3, "untitled and encoding flags");
    expect(decoded.documents[0].anchor == 2 && decoded.documents[0].caret == 7 && decoded.documents[0].selection == first.selection && decoded.documents[0].folds == first.folds && decoded.documents[0].zoom == -2, "selection fold and zoom metadata");
    expect(np3::EncodeSession(decoded) == bytes, "deterministic roundtrip");
    auto legacy = bytes; legacy[7] = '2';
    std::uint32_t legacyCrc = 0xFFFFFFFF;
    for (size_t i = 0; i < legacy.size() - 4; ++i) {
        legacyCrc ^= static_cast<unsigned char>(legacy[i]);
        for (int bit = 0; bit < 8; ++bit) legacyCrc = (legacyCrc >> 1) ^ (0xEDB88320u & (0u - (legacyCrc & 1u)));
    }
    legacyCrc = ~legacyCrc;
    for (unsigned i = 0; i < 4; ++i) legacy[legacy.size() - 4 + i] = static_cast<char>((legacyCrc >> (i * 8)) & 255);
    expect(np3::DecodeSession(legacy).documents[0].text == first.text, "NP3SESS2 remains readable after metadata format extension");
    auto metadata = session; metadata.documents.resize(1); metadata.active = 0;
    auto& diskDocument = metadata.documents[0]; diskDocument.text.clear(); diskDocument.flags = NP3_SESSION_DISK_BACKED;
    diskDocument.anchor = 100; diskDocument.caret = 200; diskDocument.firstLine = 20; diskDocument.folds = { 1, 30 };
    auto diskDecoded = np3::DecodeSession(np3::EncodeSession(metadata));
    expect(diskDecoded.documents[0].text.empty() && diskDecoded.documents[0].caret == 200 && diskDecoded.documents[0].folds == diskDocument.folds, "disk-backed metadata without discarded text");
    auto badMetadata = metadata; badMetadata.documents[0].path.clear(); rejected([&] { np3::EncodeSession(badMetadata); });
    badMetadata = metadata; badMetadata.documents[0].text = "discarded"; rejected([&] { np3::EncodeSession(badMetadata); });
    badMetadata = metadata; badMetadata.documents[0].flags |= NP3_SESSION_MODIFIED; rejected([&] { np3::EncodeSession(badMetadata); });
    badMetadata = metadata; badMetadata.documents[0].caret = static_cast<std::uint64_t>(UINT32_MAX) + 1; rejected([&] { np3::EncodeSession(badMetadata); });
    for (size_t length = 0; length < bytes.size(); ++length) rejected([&] { np3::DecodeSession(std::string_view(bytes.data(), length)); });
    auto corrupt = bytes; corrupt[24] ^= 1; rejected([&] { np3::DecodeSession(corrupt); });
    rejected([&] { np3::DecodeSession(bytes + "suffix"); });
    auto invalid = session; invalid.active = 2; rejected([&] { np3::EncodeSession(invalid); });
    invalid = session; invalid.documents.clear(); rejected([&] { np3::EncodeSession(invalid); });
    invalid = session; invalid.documents.resize(65); rejected([&] { np3::EncodeSession(invalid); });
    invalid = session; invalid.documents[0].path.push_back(L'\0'); rejected([&] { np3::EncodeSession(invalid); });
    invalid = session; invalid.documents[0].caret = 100; rejected([&] { np3::EncodeSession(invalid); });
    invalid = session; invalid.documents[0].eol = 3; rejected([&] { np3::EncodeSession(invalid); });
    invalid = session; invalid.documents[0].flags = 8; rejected([&] { np3::EncodeSession(invalid); });
    invalid = session; invalid.documents[0].folds = { 2, 0 }; rejected([&] { np3::EncodeSession(invalid); });
    invalid = session; invalid.documents[0].zoom = 21; rejected([&] { np3::EncodeSession(invalid); });
    invalid = session; invalid.documents[0].text.resize(32 * 1024 * 1024 + 1); rejected([&] { np3::EncodeSession(invalid); });
    np3::SessionSnapshot maximum; maximum.documents.resize(64);
    expect(np3::DecodeSession(np3::EncodeSession(maximum)).documents.size() == 64, "64 document boundary");

    wchar_t temp[MAX_PATH]; GetTempPathW(MAX_PATH, temp);
    const std::wstring folder = std::wstring(temp) + L"np3-session-test-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64());
    expect(CreateDirectoryW(folder.c_str(), nullptr) != FALSE, "isolated test directory");
    const auto path = folder + L"\\session.dat";
    std::uint32_t error;
    expect(np3::SaveSession(path, session, error) && error == 0, "DPAPI atomic save");
    const auto original = disk(path);
    expect(!original.empty() && original.find("unsaved") == std::string::npos && original.find("NP3SESS2") == std::string::npos && original.find("NP3SESS3") == std::string::npos, "no plaintext buffer on disk");
    np3::SessionSnapshot restored;
    expect(np3::LoadSession(path, restored, error) && np3::EncodeSession(restored) == bytes, "DPAPI load roundtrip");
    HANDLE locked = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    expect(locked != INVALID_HANDLE_VALUE, "hold replacement lock");
    auto updated = session; updated.active = 0;
    expect(!np3::SaveSession(path, updated, error) && error != 0 && disk(path) == original, "replacement failure preserves previous session");
    CloseHandle(locked);
    expect(np3::SaveSession(path, updated, error) && np3::LoadSession(path, restored, error) && restored.active == 0, "replace existing session");
    NP3_SESSION loaded = nullptr;
    expect(Session_Load(path.c_str(), &loaded, &error) && Session_Count(loaded) == 2 && Session_Active(loaded) == 0, "C interface protected load");
    NP3_SESSION_DOCUMENT document = {};
    expect(Session_Get(loaded, 0, &document) && document.textLength == first.text.size() && std::string(document.text, document.textLength) == first.text, "C interface borrowed document");
    NP3_SESSION builder = Session_Create();
    expect(builder && Session_Add(builder, &document) && Session_Count(builder) == 1, "C interface capture builder");
    document.caret = 1000;
    expect(!Session_Add(builder, &document) && Session_Count(builder) == 1, "invalid C entry leaves builder unchanged");
    expect(!Session_SetActive(builder, 1) && Session_SetActive(builder, 0) && !Session_Get(builder, 1, &document), "C interface bounds");
    expect(Session_Save(builder, path.c_str(), &error), "C interface protected save");
    Session_Free(builder); Session_Free(loaded);
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    DWORD written; const char bad[] = "damaged"; WriteFile(file, bad, sizeof(bad), &written, nullptr); CloseHandle(file);
    const auto prior = np3::EncodeSession(restored);
    expect(!np3::LoadSession(path, restored, error) && np3::EncodeSession(restored) == prior, "corrupt disk leaves caller snapshot unchanged");
    expect(!np3::SaveSession(folder + L"\\missing\\session.dat", session, error), "missing folder save fails");
    expect(!np3::LoadSession(folder + L"\\absent.dat", restored, error) && error == ERROR_FILE_NOT_FOUND, "missing snapshot read");
    std::printf("%d session checks passed; evidence: %ls\n", checks, folder.c_str());
}
