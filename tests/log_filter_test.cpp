#include "../Notepad3/src/TextTools/LogFilter.h"
#include <cstdio>
#include <string>
#include <stdexcept>
int checks = 0;
void expect(bool value, const char* label) { ++checks; if (!value) throw std::runtime_error(label); }
int cancel(void* context) { return ++*static_cast<int*>(context) > 20; }
int main() {
    LOG_FILTER_RESULT result{}; LOG_FILTER_OPTIONS options{}; options.caseSensitive = 1;
    auto run = [&](const std::string& text, const std::string& pattern) { LogFilter_Free(&result); return LogFilter_Run(text.data(), text.size(), pattern.data(), pattern.size(), &options, &result); };
    try {
        expect(run("info\r\nERROR failure\nwarn\rERROR next", "ERROR") == LOG_FILTER_OK, "mixed EOL keyword status");
        expect(result.count == 2 && result.rows[0].line == 1 && result.rows[0].offset == 6 && result.rows[1].line == 3, "original lines and byte offsets");
        expect(result.rows[0].length == 13 && result.rows[0].matchLength == 5, "line and match spans exclude EOL");
        expect(run("[a].*\naxx", "[a].*") == LOG_FILTER_OK && result.count == 1, "keyword metacharacters remain literal");
        expect(run("中文错误\nnormal", "错误") == LOG_FILTER_OK && result.rows[0].matchOffset == 6 && result.rows[0].matchLength == 6, "Chinese match uses UTF8 byte spans");
        options.caseSensitive = 0;
        expect(run("ÄPFEL\napfel", "äpfel") == LOG_FILTER_OK && result.count == 1, "Unicode insensitive matching");
        options.regex = 1;
        expect(run("ERROR 42\nERROR no", "^ERROR [0-9]+$") == LOG_FILTER_OK && result.count == 1, "regex line anchors and quantifiers");
        options.invert = 1;
        expect(run("ERROR 42\nERROR no", "^ERROR [0-9]+$") == LOG_FILTER_OK && result.count == 1 && result.rows[0].line == 1 && result.rows[0].matchLength == 0, "inverted regex selects unmatched source line");
        options.invert = 0;
        expect(run("a\r\n", "") == LOG_FILTER_OK && result.count == 2 && result.rows[1].length == 0, "trailing EOL retains editor final empty line");
        expect(run("", "") == LOG_FILTER_OK && result.count == 1, "empty document has one line");
        expect(run("abc", "missing") == LOG_FILTER_OK && result.count == 0 && !result.rows, "no matches is successful empty result");
        expect(run("abc", "[") == LOG_FILTER_INVALID_PATTERN && result.errorCode != 0 && !result.rows, "invalid regex reports compile diagnostic");
        expect(run(std::string("\xFF", 1), ".") == LOG_FILTER_INVALID_TEXT && !result.rows, "invalid UTF8 rejected without partial rows");
        expect(run(std::string(8 * 1024 * 1024 + 1, 'a'), "a") == LOG_FILTER_LIMIT, "snapshot input cap");
        expect(run(std::string(1024 * 1024 + 1, 'a'), "a") == LOG_FILTER_LIMIT, "single line cap");
        expect(run("x", std::string(4097, 'a')) == LOG_FILTER_LIMIT, "pattern cap");
        expect(run(std::string(200, 'a') + "!", "^(a+)+$") == LOG_FILTER_LIMIT && !result.rows, "pathological backtracking bounded");
        expect(run(std::string(100000, '\n'), "") == LOG_FILTER_LIMIT && !result.rows, "result row cap returns no partial success");
        int canceled = 0; options.cancel = cancel; options.context = &canceled;
        expect(run(std::string(200, 'a') + "!", "^(a+)+$") == LOG_FILTER_CANCELED && !result.rows, "cancel during regex evaluation");
        LogFilter_Free(&result);
        expect(!result.rows && result.count == 0, "free clears ownership");
        std::printf("PASS %d log filter checks\n", checks); return 0;
    } catch (const std::exception& error) { std::fprintf(stderr, "FAIL %s after %d checks\n", error.what(), checks); LogFilter_Free(&result); return 1; }
}
