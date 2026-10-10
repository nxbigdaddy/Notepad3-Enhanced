#include "../Notepad3/src/TextTools/JsonFormat.h"
#include <cstdio>
#include <cstdlib>
#include <string>

int checks = 0;
void expect(bool condition, const char* name) {
    ++checks;
    if (!condition) { std::fprintf(stderr, "FAIL: %s\n", name); std::exit(1); }
}
std::string format(const std::string& text, bool compact = false, const char* eol = "\n") {
    char* output; size_t length, error;
    expect(JsonFormat(text.data(), text.size(), compact, eol, &output, &length, &error) == 0, "valid JSON");
    std::string result(output, length); JsonFormat_Free(output); return result;
}
void invalid(const std::string& text, int expected = 1) {
    char* output; size_t length, error;
    const std::string original = text;
    expect(JsonFormat(text.data(), text.size(), 0, "\n", &output, &length, &error) == expected, "reject invalid input");
    expect(output == nullptr && length == 0 && text == original, "failure leaves input unchanged and output empty");
}
int main() {
    expect(format("{\"b\":1,\"a\":[true,null,{}]}") == "{\n  \"b\": 1,\n  \"a\": [\n    true,\n    null,\n    {}\n  ]\n}", "indent and field order");
    expect(format(" { \"x\" : 9007199254740993123456789, \"x\": -0.12345678901234567890e+999 } ", true) == "{\"x\":9007199254740993123456789,\"x\":-0.12345678901234567890e+999}", "preserve duplicate keys and exact numeric tokens");
    expect(format("{\"中文\":\"a \\\"b\\n\\u1234\"}", true) == "{\"中文\":\"a \\\"b\\n\\u1234\"}", "preserve string bytes and escapes");
    expect(format("[1,2]\r\n", false, "\r\n") == "[\r\n  1,\r\n  2\r\n]\r\n", "document CRLF");
    expect(format("[]", false, "\r") == "[]", "empty container");
    expect(format("\xEF\xBB\xBF{}", true) == "\xEF\xBB\xBF{}", "retain BOM");
    expect(format(" true ", true) == "true", "scalar root");
    const std::string formatted = format("{\"k\":[0,-1,1E2]}");
    expect(format(formatted) == formatted, "idempotent formatting");
    expect(format(formatted, true) == "{\"k\":[0,-1,1E2]}", "format to compact roundtrip");
    for (const char* text : {"", "{", "[1,]", "{\"x\":1,}", "{x:1}", "{\"x\" 1}", "[1 2]", "01", "1.", "1e+", "NaN", "true false", "\"\\q\"", "\"\\u12GG\"", "\"a\n\"", "/*x*/{}", "-", "[true,false,null,]"}) invalid(text);
    invalid(std::string("{}\0x", 4));
    std::string deep(128, '['); deep += '0'; deep.append(128, ']');
    expect(format(deep, true) == deep, "allowed depth boundary");
    invalid("[" + deep + "]", 2);
    invalid(std::string(8 * 1024 * 1024 + 1, ' '), 2);
    std::printf("%d JSON checks passed\n", checks);
}
