#include "ConfigValidate.h"
#include "JsonFormat.h"
#include <string_view>
#include <algorithm>
#include <new>
namespace {
bool Utf8(std::string_view text, size_t& error) {
    for (size_t i = 0; i < text.size();) {
        size_t start = i; unsigned char lead = static_cast<unsigned char>(text[i++]);
        if (!lead) { error = start; return false; }
        if (lead < 128) continue;
        unsigned count; unsigned value;
        if (lead >= 0xC2 && lead <= 0xDF) { count = 1; value = lead & 31; }
        else if (lead >= 0xE0 && lead <= 0xEF) { count = 2; value = lead & 15; }
        else if (lead >= 0xF0 && lead <= 0xF4) { count = 3; value = lead & 7; }
        else { error = start; return false; }
        if (text.size() - i < count) { error = start; return false; }
        for (unsigned j = 0; j < count; ++j) {
            unsigned char next = static_cast<unsigned char>(text[i++]);
            if ((next & 0xC0) != 0x80) { error = start; return false; }
            value = (value << 6) | (next & 63);
        }
        if ((count == 2 && value < 0x800) || (count == 3 && value < 0x10000) || value > 0x10FFFF || (value >= 0xD800 && value <= 0xDFFF)) { error = start; return false; }
    }
    return true;
}
bool White(char c) { return c == ' ' || c == '\t'; }
int Ini(std::string_view text, size_t base, CONFIG_DIAGNOSTIC* diagnostic) {
    for (size_t line = 0; line < text.size();) {
        size_t end = text.find_first_of("\r\n", line);
        if (end == std::string_view::npos) end = text.size();
        size_t begin = line, finish = end;
        while (begin < finish && White(text[begin])) ++begin;
        while (finish > begin && White(text[finish - 1])) --finish;
        for (size_t i = begin; i < finish; ++i) {
            unsigned char c = static_cast<unsigned char>(text[i]);
            if (c < 32 && c != '\t') { *diagnostic = { base + i, CONFIG_INVALID_TEXT }; return 1; }
        }
        if (begin < finish && text[begin] != ';' && text[begin] != '#') {
            if (text[begin] == '[') {
                size_t close = text.find(']', begin + 1);
                if (close == std::string_view::npos || close >= finish) { *diagnostic = { base + begin, CONFIG_INI_SECTION }; return 1; }
                size_t name = begin + 1; while (name < close && White(text[name])) ++name;
                if (name == close || text.find('[', begin + 1) < close) { *diagnostic = { base + begin, CONFIG_INI_SECTION }; return 1; }
                size_t tail = close + 1; while (tail < finish && White(text[tail])) ++tail;
                if (tail < finish && text[tail] != ';' && text[tail] != '#') { *diagnostic = { base + tail, CONFIG_INI_SECTION }; return 1; }
            } else {
                size_t equals = text.find('=', begin);
                if (equals == std::string_view::npos || equals >= finish) { *diagnostic = { base + begin, CONFIG_INI_KEY }; return 1; }
                size_t keyEnd = equals; while (keyEnd > begin && White(text[keyEnd - 1])) --keyEnd;
                if (keyEnd == begin) { *diagnostic = { base + begin, CONFIG_INI_KEY }; return 1; }
            }
        }
        line = end;
        if (line < text.size() && text[line] == '\r') ++line;
        if (line < text.size() && text[line] == '\n') ++line;
    }
    return 0;
}
}
extern "C" int ConfigValidate(const char* text, size_t length, int kind, CONFIG_DIAGNOSTIC* diagnostic) {
    if (!diagnostic) return 1;
    *diagnostic = { 0, CONFIG_VALID };
    if ((!text && length) || (kind != CONFIG_JSON && kind != CONFIG_INI)) { diagnostic->reason = CONFIG_INVALID_TEXT; return 1; }
    if (length > 8 * 1024 * 1024) return 2;
    try {
        std::string_view input(text ? text : "", length); size_t bad = 0;
        if (!Utf8(input, bad)) { *diagnostic = { bad, CONFIG_INVALID_TEXT }; return 1; }
        size_t base = input.substr(0, 3) == "\xEF\xBB\xBF" ? 3 : 0; input.remove_prefix(base);
        if (kind == CONFIG_INI) return Ini(input, base, diagnostic);
        char* output = nullptr; size_t outputLength = 0, offset = 0;
        int result = JsonFormat(input.data(), input.size(), 1, "\n", &output, &outputLength, &offset);
        JsonFormat_Free(output);
        if (result) *diagnostic = { base + (std::min)(offset, input.size()), CONFIG_JSON_SYNTAX };
        return result;
    } catch (const std::bad_alloc&) { return 3; }
    catch (...) { diagnostic->reason = CONFIG_INVALID_TEXT; return 1; }
}
