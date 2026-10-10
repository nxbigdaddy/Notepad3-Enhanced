#include "LogFilter.h"
#define PCRE2_CODE_UNIT_WIDTH 8
#define PCRE2_STATIC
#include "../../scintilla/pcre2/src/pcre2.h"
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>
namespace {
struct Budget { const LOG_FILTER_OPTIONS* options; size_t calls = 0; bool canceled = false, exhausted = false; };
int Callout(pcre2_callout_block*, void* pointer) {
    auto& budget = *static_cast<Budget*>(pointer);
    if (budget.options->cancel && budget.options->cancel(budget.options->context)) budget.canceled = true;
    if (++budget.calls > 2000000) budget.exhausted = true;
    return budget.canceled || budget.exhausted ? PCRE2_ERROR_CALLOUT : 0;
}
}
extern "C" void LogFilter_Free(LOG_FILTER_RESULT* result) {
    if (result) { std::free(result->rows); *result = {}; }
}
extern "C" int LogFilter_Run(const char* text, size_t length, const char* pattern, size_t patternLength, const LOG_FILTER_OPTIONS* options, LOG_FILTER_RESULT* result) {
    if (!result) return LOG_FILTER_INVALID_TEXT;
    *result = {};
    if ((!text && length) || (!pattern && patternLength) || !options) return LOG_FILTER_INVALID_TEXT;
    if (length > 8 * 1024 * 1024 || patternLength > 4096) return LOG_FILTER_LIMIT;
    if (options->cancel && options->cancel(options->context)) return LOG_FILTER_CANCELED;
    try {
        std::string expression;
        std::vector<size_t> sourceOffsets;
        for (size_t i = 0; i < patternLength; ++i) {
            if (!options->regex && pattern[i] && std::strchr("\\.^$|()[]{}*+?", pattern[i])) { expression += '\\'; sourceOffsets.push_back(i); }
            expression += pattern[i]; sourceOffsets.push_back(i);
        }
        sourceOffsets.push_back(patternLength);
        int error = 0; PCRE2_SIZE errorOffset = 0;
        uint32_t flags = PCRE2_UTF | PCRE2_UCP | PCRE2_AUTO_CALLOUT;
        if (!options->caseSensitive) flags |= PCRE2_CASELESS;
        std::unique_ptr<pcre2_code, decltype(&pcre2_code_free)> code(pcre2_compile(reinterpret_cast<PCRE2_SPTR>(expression.data()), expression.size(), flags, &error, &errorOffset, nullptr), pcre2_code_free);
        if (!code) { result->errorCode = error; result->errorOffset = sourceOffsets[(std::min)(static_cast<size_t>(errorOffset), sourceOffsets.size() - 1)]; return LOG_FILTER_INVALID_PATTERN; }
        std::unique_ptr<pcre2_match_data, decltype(&pcre2_match_data_free)> match(pcre2_match_data_create_from_pattern(code.get(), nullptr), pcre2_match_data_free);
        std::unique_ptr<pcre2_match_context, decltype(&pcre2_match_context_free)> context(pcre2_match_context_create(nullptr), pcre2_match_context_free);
        if (!match || !context) return LOG_FILTER_MEMORY;
        Budget budget{options};
        pcre2_set_match_limit(context.get(), 100000); pcre2_set_depth_limit(context.get(), 256); pcre2_set_heap_limit(context.get(), 8192);
        pcre2_set_callout(context.get(), Callout, &budget);
        std::vector<LOG_FILTER_ROW> rows;
        size_t start = 0, line = 0;
        while (true) {
            if (options->cancel && options->cancel(options->context)) return LOG_FILTER_CANCELED;
            size_t end = start;
            while (end < length && text[end] != '\r' && text[end] != '\n') ++end;
            if (end - start > 1024 * 1024) return LOG_FILTER_LIMIT;
            const char* subject = text ? text + start : "";
            int found = pcre2_match(code.get(), reinterpret_cast<PCRE2_SPTR>(subject), end - start, 0, 0, match.get(), context.get());
            if (budget.canceled) return LOG_FILTER_CANCELED;
            if (budget.exhausted || found == PCRE2_ERROR_MATCHLIMIT || found == PCRE2_ERROR_DEPTHLIMIT || found == PCRE2_ERROR_HEAPLIMIT) return LOG_FILTER_LIMIT;
            if (found < 0 && found != PCRE2_ERROR_NOMATCH) { result->errorCode = found; result->errorOffset = start; return LOG_FILTER_INVALID_TEXT; }
            if ((found >= 0) != (options->invert != 0)) {
                if (rows.size() == 100000) return LOG_FILTER_LIMIT;
                PCRE2_SIZE* offsets = pcre2_get_ovector_pointer(match.get());
                rows.push_back({line, start, end - start, found >= 0 ? start + offsets[0] : start, found >= 0 ? offsets[1] - offsets[0] : 0});
            }
            if (end == length) break;
            start = end + 1;
            if (text[end] == '\r' && start < length && text[start] == '\n') ++start;
            ++line;
        }
        if (!rows.empty()) {
            result->rows = static_cast<LOG_FILTER_ROW*>(std::malloc(rows.size() * sizeof(LOG_FILTER_ROW)));
            if (!result->rows) return LOG_FILTER_MEMORY;
            std::memcpy(result->rows, rows.data(), rows.size() * sizeof(LOG_FILTER_ROW));
        }
        result->count = rows.size(); return LOG_FILTER_OK;
    } catch (...) { return LOG_FILTER_MEMORY; }
}
