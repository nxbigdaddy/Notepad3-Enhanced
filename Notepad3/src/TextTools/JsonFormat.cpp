#include "JsonFormat.h"
#include <cstdlib>
#include <cstring>
#include <new>
#include <string>
#include <string_view>

namespace {
constexpr size_t InputLimit = 8 * 1024 * 1024;
constexpr size_t OutputLimit = 32 * 1024 * 1024;
constexpr unsigned DepthLimit = 128;
struct Invalid {};
struct Limit {};
class Formatter {
    std::string_view source;
    std::string result;
    std::string_view eol;
    size_t pos = 0;
    bool compact;

    void append(std::string_view text) {
        if (text.size() > OutputLimit - result.size()) throw Limit{};
        result.append(text);
    }
    void space() {
        while (pos < source.size() && (source[pos] == ' ' || source[pos] == '\t' || source[pos] == '\r' || source[pos] == '\n')) ++pos;
    }
    bool take(char c) {
        if (pos < source.size() && source[pos] == c) { ++pos; return true; }
        return false;
    }
    void require(char c) { if (!take(c)) throw Invalid{}; }
    void line(unsigned depth) {
        if (compact) return;
        append(eol);
        for (unsigned i = 0; i < depth; ++i) append("  ");
    }
    void string() {
        const size_t start = pos;
        require('"');
        while (pos < source.size()) {
            const unsigned char c = static_cast<unsigned char>(source[pos++]);
            if (c == '"') { append(source.substr(start, pos - start)); return; }
            if (c < 0x20) throw Invalid{};
            if (c != '\\') continue;
            if (pos == source.size()) throw Invalid{};
            const char escape = source[pos++];
            if (escape == 'u') {
                for (int i = 0; i < 4; ++i) {
                    if (pos == source.size()) throw Invalid{};
                    const char hex = source[pos++];
                    if (!((hex >= '0' && hex <= '9') || (hex >= 'a' && hex <= 'f') || (hex >= 'A' && hex <= 'F'))) throw Invalid{};
                }
            } else if (escape != '"' && escape != '\\' && escape != '/' && escape != 'b' && escape != 'f' && escape != 'n' && escape != 'r' && escape != 't') throw Invalid{};
        }
        throw Invalid{};
    }
    bool digit() const { return pos < source.size() && source[pos] >= '0' && source[pos] <= '9'; }
    void digits() { if (!digit()) throw Invalid{}; while (digit()) ++pos; }
    void number() {
        const size_t start = pos;
        take('-');
        if (!take('0')) {
            if (pos == source.size() || source[pos] < '1' || source[pos] > '9') throw Invalid{};
            digits();
        }
        if (take('.')) digits();
        if (take('e') || take('E')) { if (!take('+')) take('-'); digits(); }
        append(source.substr(start, pos - start));
    }
    void value(unsigned depth) {
        space();
        if (pos == source.size()) throw Invalid{};
        const char c = source[pos];
        if (c == '{' || c == '[') {
            if (depth >= DepthLimit) throw Limit{};
            const bool object = c == '{';
            const char close = object ? '}' : ']';
            ++pos; append(object ? "{" : "["); space();
            if (take(close)) { append(object ? "}" : "]"); return; }
            line(depth + 1);
            for (;;) {
                if (object) { string(); space(); require(':'); append(compact ? ":" : ": "); }
                value(depth + 1); space();
                if (take(close)) break;
                require(','); append(","); space(); line(depth + 1);
            }
            line(depth); append(object ? "}" : "]");
        } else if (c == '"') string();
        else if (c == '-' || digit()) number();
        else {
            const std::string_view token = c == 't' ? "true" : c == 'f' ? "false" : c == 'n' ? "null" : "";
            if (token.empty() || source.substr(pos, token.size()) != token) throw Invalid{};
            pos += token.size(); append(token);
        }
    }
public:
    Formatter(std::string_view input, bool minimize, std::string_view newline) : source(input), eol(newline), compact(minimize) {}
    size_t offset() const { return pos; }
    std::string run() {
        if (source.size() >= 3 && source.substr(0, 3) == "\xEF\xBB\xBF") { pos = 3; append(source.substr(0, 3)); }
        value(0); space();
        if (pos != source.size()) throw Invalid{};
        if (!compact && !source.empty() && (source.back() == '\r' || source.back() == '\n')) append(eol);
        return result;
    }
};
}
extern "C" int JsonFormat(const char* input, size_t length, int compact, const char* eol,
                           char** output, size_t* outputLength, size_t* errorOffset)
{
    *output = nullptr; *outputLength = 0; *errorOffset = 0;
    if (length > InputLimit) return 2;
    Formatter formatter(std::string_view(input, length), compact != 0, eol);
    try {
        std::string text = formatter.run();
        char* buffer = static_cast<char*>(std::malloc(text.size() + 1));
        if (!buffer) return 3;
        std::memcpy(buffer, text.data(), text.size()); buffer[text.size()] = 0;
        *output = buffer; *outputLength = text.size(); return 0;
    } catch (const Invalid&) { *errorOffset = formatter.offset(); return 1; }
      catch (const Limit&) { *errorOffset = formatter.offset(); return 2; }
      catch (const std::bad_alloc&) { return 3; }
}
extern "C" void JsonFormat_Free(char* output) { std::free(output); }
