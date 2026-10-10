#pragma once
#include <stddef.h>
#include <stdint.h>
#include <wchar.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef void* NP3_SESSION;
#define NP3_SESSION_MODIFIED 1u
#define NP3_SESSION_READONLY 2u
#define NP3_SESSION_ENCRYPTED_SOURCE 4u
typedef struct NP3_SESSION_DOCUMENT {
    const wchar_t* path;
    const char* text;
    size_t textLength;
    const char* selection;
    size_t selectionLength;
    const uint64_t* folds;
    size_t foldCount;
    uint32_t encoding, eol, flags;
    uint64_t anchor, caret, firstLine;
    uint32_t xOffset;
    int32_t zoom;
} NP3_SESSION_DOCUMENT;
NP3_SESSION Session_Create(void);
void Session_Free(NP3_SESSION session);
int Session_Add(NP3_SESSION session, const NP3_SESSION_DOCUMENT* document);
int Session_SetActive(NP3_SESSION session, uint32_t active);
size_t Session_Count(NP3_SESSION session);
uint32_t Session_Active(NP3_SESSION session);
// Returned pointers are borrowed until the session is mutated or freed.
int Session_Get(NP3_SESSION session, size_t index, NP3_SESSION_DOCUMENT* document);
int Session_Save(NP3_SESSION session, const wchar_t* path, uint32_t* error);
int Session_Load(const wchar_t* path, NP3_SESSION* session, uint32_t* error);
#ifdef __cplusplus
}
#endif
