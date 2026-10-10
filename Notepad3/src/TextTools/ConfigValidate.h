#pragma once
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
enum CONFIG_KIND { CONFIG_JSON = 1, CONFIG_INI = 2 };
enum CONFIG_REASON { CONFIG_VALID = 0, CONFIG_JSON_SYNTAX = 1, CONFIG_INVALID_TEXT = 2, CONFIG_INI_SECTION = 3, CONFIG_INI_KEY = 4 };
typedef struct CONFIG_DIAGNOSTIC { size_t offset; int reason; } CONFIG_DIAGNOSTIC;
// Returns 0 for valid structure, 1 for syntax, 2 for limits, 3 for allocation.
int ConfigValidate(const char* text, size_t length, int kind, CONFIG_DIAGNOSTIC* diagnostic);
#ifdef __cplusplus
}
#endif
