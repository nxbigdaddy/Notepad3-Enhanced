#include "BatchTransform.h"
#include "JsonFormat.h"
#include <algorithm>
#include <memory>
#include <stdexcept>
#include <unordered_set>
#include <windows.h>

namespace np3::batch {
namespace {
constexpr size_t InputLimit = 8 * 1024 * 1024;
constexpr size_t TotalLimit = 32 * 1024 * 1024;
constexpr size_t LineLimit = 100000;
bool Valid(const std::string& text) {
    return text.find('\0') == std::string::npos && (text.empty() ||
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0));
}
struct TooLarge {};
void Append(std::string& target, const std::string& text) {
    if (text.size() > TotalLimit - target.size()) throw TooLarge{};
    target += text;
}
std::string Lines(const std::string& text, const Options& options) {
    std::vector<std::string> lines;
    size_t begin = 0;
    bool finalEol = false;
    while (begin < text.size()) {
        const auto end = text.find_first_of("\r\n", begin);
        lines.push_back(text.substr(begin, end == std::string::npos ? end : end - begin));
        if (lines.size() > LineLimit) throw TooLarge{};
        if (end == std::string::npos) { finalEol = false; break; }
        begin = end + 1;
        if (text[end] == '\r' && begin < text.size() && text[begin] == '\n') ++begin;
        finalEol = begin == text.size();
    }
    if (options.operation == Operation::SortLines) std::stable_sort(lines.begin(), lines.end());
    if (options.operation == Operation::UniqueLines) {
        std::unordered_set<std::string> seen;
        lines.erase(std::remove_if(lines.begin(), lines.end(), [&](const std::string& line) { return !seen.insert(line).second; }), lines.end());
    }
    if (options.operation == Operation::TrimTrailing) {
        for (auto& line : lines) {
            const auto last = line.find_last_not_of(" \t");
            line.resize(last == std::string::npos ? 0 : last + 1);
        }
    }
    if (options.operation == Operation::RemoveEmpty)
        lines.erase(std::remove_if(lines.begin(), lines.end(), [](const std::string& line) { return line.empty(); }), lines.end());
    std::string output;
    for (size_t i = 0; i < lines.size(); ++i) {
        Append(output, lines[i]);
        if (i + 1 < lines.size() || finalEol) Append(output, options.eol);
    }
    return output;
}
}
std::vector<Proposal> Prepare(const std::vector<Source>& sources, const Options& options) {
    if (sources.size() > 64) throw std::length_error("Batch supports at most 64 documents");
    bool invalidOptions = options.eol != "\r\n" && options.eol != "\n" && options.eol != "\r";
    invalidOptions = invalidOptions || options.operation < Operation::Replace || options.operation > Operation::JsonCompact;
    invalidOptions = invalidOptions || options.find.size() > InputLimit || options.replacement.size() > InputLimit;
    if (!invalidOptions) invalidOptions = !Valid(options.find) || !Valid(options.replacement);
    if (options.operation == Operation::Replace && options.find.empty()) invalidOptions = true;
    size_t totalInput = 0, totalOutput = 0;
    std::unordered_set<uint64_t> ids;
    std::vector<Proposal> proposals;
    proposals.reserve(sources.size());
    for (const auto& source : sources) {
        Proposal p;
        p.source.id = source.id;
        p.source.version = source.version;
        p.source.readOnly = source.readOnly;
        if (source.label.size() <= 32768) p.source.label = source.label;
        if (source.label.size() > 32768 || source.text.size() > InputLimit || source.text.size() > TotalLimit - totalInput) p.status = Status::Limit;
        else if (!source.id || !ids.insert(source.id).second || invalidOptions) p.status = Status::InvalidOptions;
        else if (source.readOnly) p.status = Status::ReadOnly;
        else if (!Valid(source.text)) p.status = Status::InvalidText;
        else {
            p.source.text = source.text;
            try {
                if (options.operation == Operation::Replace) {
                    size_t start = 0;
                    for (;;) {
                        const auto match = source.text.find(options.find, start);
                        Append(p.result, source.text.substr(start, match == std::string::npos ? match : match - start));
                        if (match == std::string::npos) break;
                        Append(p.result, options.replacement);
                        start = match + options.find.size();
                    }
                } else if (options.operation == Operation::JsonFormat || options.operation == Operation::JsonCompact) {
                    char* data = nullptr;
                    size_t length = 0;
                    const int code = JsonFormat(source.text.data(), source.text.size(), options.operation == Operation::JsonCompact,
                        options.eol.c_str(), &data, &length, &p.errorOffset);
                    std::unique_ptr<char, decltype(&JsonFormat_Free)> owned(data, &JsonFormat_Free);
                    if (code == 3) throw std::bad_alloc{};
                    if (code) p.status = code == 1 ? Status::InvalidJson : Status::Limit;
                    else p.result.assign(data, length);
                } else p.result = Lines(source.text, options);
                if (p.status != Status::InvalidJson && p.status != Status::Limit) {
                    if (p.result.size() > TotalLimit - totalOutput) p.status = Status::Limit;
                    else {
                        totalOutput += p.result.size();
                        p.status = p.result == source.text ? Status::Unchanged : Status::Ready;
                    }
                }
            } catch (const TooLarge&) { p.status = Status::Limit; }
        }
        if (source.text.size() <= TotalLimit - totalInput) totalInput += source.text.size();
        else totalInput = TotalLimit;
        if (p.status != Status::Ready && p.status != Status::Unchanged) p.result.clear();
        proposals.push_back(std::move(p));
    }
    return proposals;
}
bool Matches(const Proposal& proposal, const Source& current) {
    return proposal.status == Status::Ready && !current.readOnly && current.id == proposal.source.id &&
        current.version == proposal.source.version && current.text == proposal.source.text;
}
}
