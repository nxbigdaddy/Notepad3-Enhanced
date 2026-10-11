#include "../Notepad3/src/AI/AiDiff.h"
#include "../Notepad3/src/AI/AiCore.h"
#include "../Notepad3/src/TextTools/JsonFormat.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <stdexcept>
int checks = 0;
void expect(bool condition, const char* label) { ++checks; if (!condition) throw std::runtime_error(label); }
int main() {
    char* output = nullptr; size_t length = 0;
    std::wstring profile = L"test-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64());
    bool created = false;
    auto response = [&](const std::string& json, int expected) { AiProtocol_Free(output); output = nullptr; expect(AiProtocol_Response(json.data(), json.size(), &output, &length) == expected, "response status"); };
    try {
        auto diff=np3::AiDifference(L"old\runchanged\rsecond",L"new\runchanged\rthird");
        expect(diff.precise && diff.ranges.size()==2,"two separate changes preserve unchanged paragraph");
        expect(diff.ranges[0].beforeEnd<=4 && diff.ranges[1].beforeStart>=14,"diff ranges exclude middle context");
        expect(np3::AiDifference(L"same",L"same").ranges.empty(),"identical text has no highlight");
        auto inserted=np3::AiDifference(L"a\rc",L"a\rb\rc");
        expect(inserted.ranges.size()==1 && inserted.ranges[0].beforeStart==inserted.ranges[0].beforeEnd && inserted.ranges[0].afterEnd-inserted.ranges[0].afterStart==2,"inserted line has zero source range");
        auto deleted=np3::AiDifference(L"a\rb\rc",L"a\rc");
        expect(deleted.ranges.size()==1 && deleted.ranges[0].afterStart==deleted.ranges[0].afterEnd,"deleted line has zero result range");
        auto unicode=np3::AiDifference(L"x\xd83d\xde00y",L"x\xd83d\xde01y");
        expect(unicode.ranges.size()==1 && unicode.ranges[0].beforeStart==1 && unicode.ranges[0].beforeEnd==3,"UTF16 surrogate pair stays intact");
        std::wstring huge; for(int line=0;line<1100;++line) huge+=L"line\r";
        expect(np3::AiDifference(huge,huge+L"more").precise,"large unchanged prefix remains precisely aligned");
        std::wstring other; for(int line=0;line<1100;++line) other+=L"other\r";
        expect(!np3::AiDifference(huge,other).precise,"large changed alignment bounded with explicit coarse flag");
        std::wstring largeBefore,largeAfter;
        for(int line=0;line<3000;++line) { auto text=L"row "+std::to_wstring(line)+L"\r"; largeBefore+=text; largeAfter+=(line==10 || line==2900 ? L"changed\r" : text); }
        auto anchored=np3::AiDifference(largeBefore,largeAfter);
        expect(anchored.precise && anchored.ranges.size()==2,"large unique context anchors separate distant changes");
        std::wstring repeatedBefore=L"old\r"+huge+L"old end", repeatedAfter=L"new\r"+huge+L"new end";
        auto repeated=np3::AiDifference(repeatedBefore,repeatedAfter);
        expect(repeated.precise && repeated.ranges.size()==2,"large aligned repeated context remains unhighlighted");
        expect(np3::AiDifference(L"",huge).precise && np3::AiDifference(L"",huge).ranges.size()==1,"large pure insertion needs no alignment matrix");
        unsigned sequence=1234567;
        for(int trial=0;trial<200;++trial) {
            std::wstring input,target; const wchar_t* words[]={L"a",L"重复",L"x\xd83d\xde00",L"tail"};
            for(int side=0;side<2;++side) { std::wstring& text=side ? target : input;
                sequence=sequence*1664525u+1013904223u; unsigned count=sequence%25;
                for(unsigned line=0;line<count;++line) { sequence=sequence*1664525u+1013904223u; text+=words[(sequence>>16)%4]; text+=L'\r'; }
            }
            auto changes=np3::AiDifference(input,target); std::wstring rebuilt; size_t consumed=0; bool ordered=true;
            for(const auto& range:changes.ranges) {
                if(range.beforeStart<consumed || range.beforeEnd<range.beforeStart || range.beforeEnd>input.size() || range.afterEnd<range.afterStart || range.afterEnd>target.size()) { ordered=false; break; }
                rebuilt+=input.substr(consumed,range.beforeStart-consumed); rebuilt+=target.substr(range.afterStart,range.afterEnd-range.afterStart); consumed=range.beforeEnd;
            }
            rebuilt+=input.substr(consumed);
            expect(ordered && rebuilt==target,"diff ranges reconstruct target across repeated Unicode paragraphs");
        }


        expect(AiEndpoint_Validate(L"https://example.test/v1/chat/completions") == AI_OK, "HTTPS service endpoint");
        expect(AiEndpoint_Validate(L"http://127.0.0.1:18888/v1/chat/completions") == AI_OK, "loopback local model endpoint");
        expect(AiEndpoint_Validate(L"http://localhost/v1/chat/completions") == AI_OK, "localhost endpoint");
        expect(AiEndpoint_Validate(L"http://remote.test/v1/chat/completions") == AI_CONFIG, "remote plaintext rejected");
        expect(AiEndpoint_Validate(L"https://user:password@example.test/v1") == AI_CONFIG, "userinfo credential rejected");
        expect(AiEndpoint_Validate(L"https://example.test/v1#fragment") == AI_CONFIG, "fragment rejected");
        expect(AiEndpoint_Validate(L"https://example.test/\r\n") == AI_CONFIG, "URL controls rejected");
        wchar_t serviceA[73] = {}, serviceB[73] = {}, serviceAgain[73] = {};
        expect(AiCredential_ServiceProfile(L"https://example.test/v1", serviceA, 73) == ERROR_SUCCESS && wcslen(serviceA) == 72, "service profile fixed length");
        expect(AiCredential_ServiceProfile(L"https://example.test/v1", serviceAgain, 73) == ERROR_SUCCESS && !wcscmp(serviceA, serviceAgain), "service profile stable across calls");
        expect(AiCredential_ServiceProfile(L"https://other.test/v1", serviceB, 73) == ERROR_SUCCESS && wcscmp(serviceA, serviceB), "different service isolates credentials");
        expect(AiCredential_ServiceProfile(L"https://example.test/v2", serviceB, 73) == ERROR_SUCCESS && wcscmp(serviceA, serviceB), "different endpoint path isolates credentials");
        expect(AiCredential_ServiceProfile(L"https://example.test/v1", serviceB, 72) == ERROR_INVALID_PARAMETER, "short profile buffer rejected");
        expect(AiCredential_ServiceProfile(L"http://remote.test/v1", serviceB, 73) == ERROR_INVALID_PARAMETER, "unsafe service has no credential profile");
        const std::string input = "中文\n\"quote\"\\path";
        const char* addedPrompts[]={"Summarize","Polish","Expand","Shorten","Correct"};
        for (int action = AI_EXPLAIN; action <= AI_CORRECT; ++action) {
            expect(AiProtocol_Request("custom-model", action, input.data(), input.size(), "Keep headings.", &output, &length) == AI_OK, "text actions generate valid request");
            char* compact = nullptr; size_t compactLength = 0, error = 0;
            expect(JsonFormat(output, length, 1, "", &compact, &compactLength, &error) == 0, "request strict JSON validates");
            expect(std::string(output, length).find("\\u000a") != std::string::npos && std::string(output, length).find("\\\"quote\\\"") != std::string::npos, "request escapes original source");
            if(action>=AI_SUMMARIZE)expect(std::string(output,length).find(addedPrompts[action-AI_SUMMARIZE])!=std::string::npos && std::string(output,length).find("Keep headings.")!=std::string::npos,"new action uses distinct instruction and retains custom constraint");
            JsonFormat_Free(compact); AiProtocol_Free(output); output = nullptr;
        }
        expect(AiProtocol_Request("model", AI_ASK, input.data(), input.size(), "What is supported?", &output, &length) == AI_OK,"document question request succeeds");
        const std::string questionBody(output,length);
        expect(questionBody.find("Question:") != std::string::npos && questionBody.find("Current document:") != std::string::npos,"question and full document are labeled");
        expect(questionBody.find("What is supported?") > questionBody.find("\"role\":\"user\""),"question is user content rather than system instruction");
        char* questionCompact=nullptr; size_t questionSize=0,questionError=0;
        expect(JsonFormat(output,length,1,"",&questionCompact,&questionSize,&questionError)==0,"question request validates strict JSON");
        JsonFormat_Free(questionCompact); AiProtocol_Free(output); output=nullptr;
        expect(AiProtocol_Request("model",AI_ASK,"x",1,nullptr,&output,&length)==AI_CONFIG,"missing question rejected");
        expect(AiProtocol_Request("model",AI_ASK,nullptr,0,"question",&output,&length)==AI_CONFIG,"empty document rejected");
        expect(AiProtocol_Request("model", 99, "x", 1, nullptr, &output, &length) == AI_CONFIG, "invalid action");
        expect(AiProtocol_Request("model", AI_EXPLAIN, "\xFF", 1, nullptr, &output, &length) == AI_TEXT, "invalid source UTF8");
        std::string large(256 * 1024 + 1, 'x');
        expect(AiProtocol_Request("model", AI_EXPLAIN, large.data(), large.size(), nullptr, &output, &length) == AI_LIMIT, "source cap");
        response(R"({"choices":[{"message":{"content":"中文\ntext\uD83D\uDE00"},"finish_reason":"stop"}]})", AI_OK);
        expect(std::string(output, length) == "中文\ntext😀", "Unicode escapes and surrogate pair decoded");
        response(R"({"choices":[{"message":{"content":"first"}},{"message":{"content":"second"}}]})", AI_OK);
        expect(std::string(output) == "first", "only first completion selected");
        response(R"({"choices":[{"message":{"content":"partial"},"finish_reason":"length"}]})", AI_INCOMPLETE);
        response(R"({"choices":[{"message":{"content":null},"finish_reason":"tool_calls"}]})", AI_RESPONSE);
        response(R"({"choices":[{"message":{"content":""}}]})", AI_RESPONSE);
        response(R"({"choices":[{"message":{"content":"\uD800"}}]})", AI_RESPONSE);
        response(R"({"choices":[{"message":{"content":"\uDC00"}}]})", AI_RESPONSE);
        response(R"({"choices":[{"message":{"content":"\u0000"}}]})", AI_RESPONSE);
        response(R"({"choices":[{"message":{"content":"a","content":"b"}}]})", AI_RESPONSE);
        response(R"({"choices":[{"message":{"content":"a"},"finish_reason":"stop","finish_reason":"length"}]})", AI_RESPONSE);
        response(R"({"choices":[]})", AI_RESPONSE);
        response("{broken", AI_RESPONSE);
        response(std::string(2 * 1024 * 1024 + 1, 'x'), AI_LIMIT);
        char* secret = nullptr;
        expect(AiCredential_Load(profile.c_str(), &secret) == ERROR_NOT_FOUND, "isolated credential initially absent");
        expect(AiCredential_Save(profile.c_str(), "fake-test-key-1") == ERROR_SUCCESS, "write Windows credential"); created = true;
        expect(AiCredential_Load(profile.c_str(), &secret) == ERROR_SUCCESS && !strcmp(secret, "fake-test-key-1"), "independent credential readback"); AiCredential_Free(secret); secret = nullptr;
        expect(AiCredential_Save(profile.c_str(), "fake\r\nheader") == ERROR_INVALID_PARAMETER, "header injection rejected");
        expect(AiCredential_Load(profile.c_str(), &secret) == ERROR_SUCCESS && !strcmp(secret, "fake-test-key-1"), "invalid write preserves existing key"); AiCredential_Free(secret); secret = nullptr;
        expect(AiCredential_Save(profile.c_str(), "fake-test-key-2") == ERROR_SUCCESS, "update own credential");
        expect(AiCredential_Load(profile.c_str(), &secret) == ERROR_SUCCESS && !strcmp(secret, "fake-test-key-2"), "updated key persists"); AiCredential_Free(secret); secret = nullptr;
        expect(AiCredential_Save(L"../invalid", "fake") == ERROR_INVALID_PARAMETER, "invalid profile rejected");
        expect(AiCredential_Delete(profile.c_str()) == ERROR_SUCCESS, "delete isolated test credential"); created = false;
        expect(AiCredential_Load(profile.c_str(), &secret) == ERROR_NOT_FOUND && !secret, "deletion independently verified");
        std::printf("PASS %d AI protocol and credential checks\n", checks); return 0;
    } catch (const std::exception& error) { AiProtocol_Free(output); if (created) AiCredential_Delete(profile.c_str()); std::fprintf(stderr, "FAIL %s after %d checks\n", error.what(), checks); return 1; }
}
