#include "../Notepad3/src/TextTools/ConfigValidate.h"
#include <cstdio>
#include <string>
#include <stdexcept>
#include <fstream>
#include <iterator>
int checks = 0;
void expect(bool condition, const char* label) { ++checks; if (!condition) throw std::runtime_error(label); }
void check(const std::string& text, int kind, int expected, int reason = CONFIG_VALID, size_t offset = 0) {
    CONFIG_DIAGNOSTIC diagnostic = {};
    int result = ConfigValidate(text.data(), text.size(), kind, &diagnostic);
    expect(result == expected, "validation result");
    if (result == 1) expect(diagnostic.reason == reason && diagnostic.offset == offset, "byte-accurate diagnostic");
}
int main() {
    try {
        check("{\"中文\": [1,true,null]}", CONFIG_JSON, 0);
        check("\xEF\xBB\xBF{\"key\":1}", CONFIG_JSON, 0);
        check("{\n\"key\": 1,\n}", CONFIG_JSON, 1, CONFIG_JSON_SYNTAX, 12);
        check("\xEF\xBB\xBF{\n\"key\": 1,\n}", CONFIG_JSON, 1, CONFIG_JSON_SYNTAX, 15);
        check("", CONFIG_JSON, 1, CONFIG_JSON_SYNTAX, 0);
        check("{\"x\":\"\xC0\"}", CONFIG_JSON, 1, CONFIG_INVALID_TEXT, 6);
        check("{\"x\":\"\xED\xA0\x80\"}", CONFIG_JSON, 1, CONFIG_INVALID_TEXT, 6);
        check("{\"x\":\"\xF4\x90\x80\x80\"}", CONFIG_JSON, 1, CONFIG_INVALID_TEXT, 6);
        check("{\"x\":\"\xE0\x80\x80\"}", CONFIG_JSON, 1, CONFIG_INVALID_TEXT, 6);
        check("{\"x\":\"\xF0\x80\x80\x80\"}", CONFIG_JSON, 1, CONFIG_INVALID_TEXT, 6);
        check("{\"x\":\"\xE4\xB8\"}", CONFIG_JSON, 1, CONFIG_INVALID_TEXT, 6);
        check("{\"x\":\"😀\"}", CONFIG_JSON, 0);
        check("global=yes\r\n; comment\r\n[section]\r\n中文=值\r\nempty=\r\n", CONFIG_INI, 0);
        check("", CONFIG_INI, 0);
        check("\xEF\xBB\xBF[Section] # comment\npath=a=b;c#d\n", CONFIG_INI, 0);
        check("[repeat]\nx=1\n[repeat]\nx=2\n", CONFIG_INI, 0);
        check("\t[ a ] ; note\rkey = value\r", CONFIG_INI, 0);
        check("[unfinished\nkey=1", CONFIG_INI, 1, CONFIG_INI_SECTION, 0);
        check("[ ]\nkey=1", CONFIG_INI, 1, CONFIG_INI_SECTION, 0);
        check("[[nested]]\nkey=1", CONFIG_INI, 1, CONFIG_INI_SECTION, 0);
        check("[section] junk\nkey=1", CONFIG_INI, 1, CONFIG_INI_SECTION, 10);
        check("[s]\r\nmissing", CONFIG_INI, 1, CONFIG_INI_KEY, 5);
        check("[s]\n  =empty", CONFIG_INI, 1, CONFIG_INI_KEY, 6);
        check("\xEF\xBB\xBF[s]\n =empty", CONFIG_INI, 1, CONFIG_INI_KEY, 8);
        check(std::string("x=\0value", 8), CONFIG_INI, 1, CONFIG_INVALID_TEXT, 2);
        check("x=\x01", CONFIG_INI, 1, CONFIG_INVALID_TEXT, 2);
        check(std::string(8 * 1024 * 1024 + 1, ' '), CONFIG_JSON, 2);
        check(std::string(129, '[') + "0" + std::string(129, ']'), CONFIG_JSON, 2);
        CONFIG_DIAGNOSTIC diagnostic = {};
        expect(ConfigValidate(nullptr, 1, CONFIG_INI, &diagnostic) == 1, "null input handled");
        expect(ConfigValidate("", 0, 123, &diagnostic) == 1, "unsupported kind handled");
        expect(ConfigValidate("", 0, CONFIG_JSON, nullptr) == 1, "null diagnostic handled");
        std::ifstream defaults("../Notepad3/Build/Notepad3.ini", std::ios::binary);
        std::string defaultText{ std::istreambuf_iterator<char>(defaults), std::istreambuf_iterator<char>() };
        expect(defaultText.size() > 100, "read actual shipped INI fixture");
        check(defaultText, CONFIG_INI, 0);
        std::printf("%d configuration checks passed\n", checks); return 0;
    } catch (const std::exception& error) { std::fprintf(stderr, "FAIL %s\n", error.what()); return 1; }
}
