#pragma once
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
enum LOG_FILTER_STATUS { LOG_FILTER_OK, LOG_FILTER_INVALID_PATTERN, LOG_FILTER_LIMIT, LOG_FILTER_CANCELED, LOG_FILTER_MEMORY, LOG_FILTER_INVALID_TEXT };
typedef int (*LOG_FILTER_CANCEL)(void* context);
typedef struct LOG_FILTER_OPTIONS { int regex, caseSensitive, invert; LOG_FILTER_CANCEL cancel; void* context; } LOG_FILTER_OPTIONS;
typedef struct LOG_FILTER_ROW { size_t line, offset, length, matchOffset, matchLength; } LOG_FILTER_ROW;
typedef struct LOG_FILTER_RESULT { LOG_FILTER_ROW* rows; size_t count, errorOffset; int errorCode; } LOG_FILTER_RESULT;
// Immutable UTF-8 snapshot; rows use zero-based source lines and byte offsets.
// On failure no partial result is returned. Free with LogFilter_Free.
int LogFilter_Run(const char* text, size_t length, const char* pattern, size_t patternLength, const LOG_FILTER_OPTIONS* options, LOG_FILTER_RESULT* result);
void LogFilter_Free(LOG_FILTER_RESULT* result);
#ifdef __cplusplus
}
#endif
