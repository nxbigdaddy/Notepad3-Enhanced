#pragma once
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
// Returns 0 on success, 1 for syntax, 2 for limits, 3 for allocation failure.
// Output is owned by the caller and must be released with JsonFormat_Free.
int JsonFormat(const char* input, size_t length, int compact, const char* eol,
               char** output, size_t* outputLength, size_t* errorOffset);
void JsonFormat_Free(char* output);
#ifdef __cplusplus
}
#endif
