#include "../Notepad3/src/TextTools/BatchTransform.h"
#include <cstdio>
#include <stdexcept>
#include <cstdlib>
using namespace np3::batch;
static int checks = 0;
static void Check(bool value) { ++checks; if (!value) { std::printf("FAIL %d\n", checks); std::exit(1); } }
static Proposal Run(std::string text, Operation op, std::string eol = "\n") {
    Options options; options.operation = op; options.eol = eol;
    return Prepare({Source{1, 3, L"中文.md", std::move(text), false}}, options).front();
}
int main() {
    Check(Run("b\na\nb\n", Operation::SortLines).result == "a\nb\nb\n");
    Check(Run("b\na\nb", Operation::UniqueLines).result == "b\na");
    Check(Run("b\na\nb\n", Operation::UniqueLines).result == "b\na\n");
    Check(Run("a \t\r\nb\t\r\n", Operation::TrimTrailing, "\r\n").result == "a\r\nb\r\n");
    Check(Run("\na\n\nb\n", Operation::RemoveEmpty).result == "a\nb\n");
    Check(Run(" \n\t\n", Operation::RemoveEmpty).status == Status::Unchanged);
    Check(Run("\n\n", Operation::RemoveEmpty).result.empty());
    Check(Run("", Operation::SortLines).status == Status::Unchanged);
    Check(Run("单行", Operation::SortLines).status == Status::Unchanged);
    Check(Run("a\r\nb\rc\n", Operation::SortLines).result == "a\nb\nc\n");
    Check(Run("a\n\n", Operation::UniqueLines).status == Status::Unchanged);
    Check(Run("z\né\n中\n", Operation::SortLines).result == "z\né\n中\n");
    Check(Run(std::string("a\0b", 3), Operation::SortLines).status == Status::InvalidText);
    Check(Run(std::string("\xC0\xAF", 2), Operation::SortLines).status == Status::InvalidText);
    Check(Run(std::string(8 * 1024 * 1024 + 1, 'a'), Operation::SortLines).status == Status::Limit);
    Check(Run(std::string(100001, '\n'), Operation::SortLines).status == Status::Limit);
    Check(Run("a", Operation::SortLines, "bad").status == Status::InvalidOptions);
    Check(Run("a", static_cast<Operation>(100)).status == Status::InvalidOptions);
    Check(Run("{\"x\":1}", Operation::JsonFormat).result == "{\n  \"x\": 1\n}");
    Check(Run("{ \"x\": 1, \"x\": 2 }", Operation::JsonCompact).result == "{\"x\":1,\"x\":2}");
    const auto invalid = Run("{bad}", Operation::JsonFormat);
    Check(invalid.status == Status::InvalidJson && invalid.result.empty() && invalid.errorOffset > 0);
    Options options; options.find = "中"; options.replacement = "文本";
    Source original{1, 9, L"中文 & #.txt", "中中a", false};
    auto p = Prepare({original}, options).front();
    Check(p.result == "文本文本a" && p.source.label == original.label);
    Check(Matches(p, original));
    auto changed = original; ++changed.version; Check(!Matches(p, changed));
    changed = original; ++changed.id; Check(!Matches(p, changed));
    changed = original; changed.text += "b"; Check(!Matches(p, changed));
    changed = original; changed.readOnly = true; Check(!Matches(p, changed));
    Check(Prepare({changed}, options).front().status == Status::ReadOnly);
    Check(Prepare({original, original}, options).back().status == Status::InvalidOptions);
    original.id = 0; Check(Prepare({original}, options).front().status == Status::InvalidOptions);
    options.find.clear(); Check(Prepare({changed}, options).front().status == Status::InvalidOptions);
    options.find = "a"; options.replacement = std::string(1024, 'x');
    changed.readOnly = false; changed.text.assign(40000, 'a');
    Check(Prepare({changed}, options).front().status == Status::Limit);
    options.find = "aa"; options.replacement = "a"; changed.text = "aaaaa";
    Check(Prepare({changed}, options).front().result == "aaa");
    options.find = "b"; Check(Prepare({changed}, options).front().status == Status::Unchanged);
    bool limited = false;
    try { Prepare(std::vector<Source>(65), options); } catch (const std::length_error&) { limited = true; }
    Check(limited);
    Check(Prepare({}, options).empty());
    std::vector<Source> large;
    for (uint64_t id = 1; id <= 5; ++id) large.push_back(Source{id, 0, L"large", std::string(8 * 1024 * 1024, 'a'), true});
    auto bounded = Prepare(large, options);
    Check(bounded[3].status == Status::ReadOnly && bounded[4].status == Status::Limit);
    Check(bounded[4].source.text.empty());
    changed.label.assign(32769, L'a');
    Check(Prepare({changed}, options).front().status == Status::Limit);
    Check(original.text == "中中a");
    Check(Run("a\xC2\xA0\n", Operation::TrimTrailing).status == Status::Unchanged);
    Check(Run("\xED\xA0\x80", Operation::SortLines).status == Status::InvalidText);
    changed.label = L"expanding"; changed.text.assign(20000, 'a');
    options.find = "a"; options.replacement.assign(1024, 'x');
    auto other = changed; ++other.id;
    auto expanded = Prepare({changed, other}, options);
    Check(expanded[0].status == Status::Ready && expanded[1].status == Status::Limit);
    Check(expanded[1].result.empty());
    std::printf("%d batch transform checks passed\n", checks);
}
