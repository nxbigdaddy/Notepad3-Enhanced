#pragma once
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct MD_OUTLINE_ITEM { unsigned level; size_t line, offset; char* title; } MD_OUTLINE_ITEM;
typedef struct MD_OUTLINE_RESULT { MD_OUTLINE_ITEM* items; size_t count; } MD_OUTLINE_RESULT;
// 0 success, 1 invalid UTF-8/input, 2 limit, 3 memory. Result must be freed before reuse.
int MarkdownOutline_Parse(const char* text, size_t length, MD_OUTLINE_RESULT* result);
void MarkdownOutline_Free(MD_OUTLINE_RESULT* result);
#ifdef __cplusplus
}
#endif
