#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <functional>

namespace np3::batch {
enum class Operation { Replace, SortLines, UniqueLines, TrimTrailing, RemoveEmpty, JsonFormat, JsonCompact };
struct Options {
    Operation operation = Operation::Replace;
    std::string find;
    std::string replacement;
    std::string eol = "\r\n";
};
struct Source {
    uint64_t id = 0;
    uint64_t version = 0;
    std::wstring label;
    std::string text;
    bool readOnly = false;
    std::string eol;
};
enum class Status { Ready, Unchanged, ReadOnly, InvalidText, InvalidOptions, Limit, InvalidJson, Canceled };
struct Proposal {
    Source source;
    std::string result;
    Status status = Status::Unchanged;
    size_t errorOffset = 0;
};
// No filesystem or editor mutation. All input snapshots and proposals are owned.
// Throws std::length_error above 64 sources; allocation failure propagates.
// UTF-8 text is required, excluding embedded NUL. Line tools use options.eol.
// 8 MiB per document, 32 MiB aggregate input/output, 100000 lines per document.
// Inputs rejected before transformation retain metadata only; valid input snapshots
// are retained for reviewing transformation errors as well as Ready/Unchanged results.
std::vector<Proposal> Prepare(const std::vector<Source>& sources, const Options& options, const std::function<bool()>& cancel = {});
// Revalidate every selected proposal before beginning an application transaction.
bool Matches(const Proposal& proposal, const Source& current);
}
