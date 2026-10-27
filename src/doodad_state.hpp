#pragma once

#include "doodad_placement.hpp"
#include <filesystem>
#include <map>
#include <set>
#include <string>

namespace viewer {
// Sparse exceptions to deterministic placement. The destination-cell index lets
// moved objects stream in outside their original forest and original cell.
class DoodadState {
public:
    explicit DoodadState(std::string identity);
    void load(const std::filesystem::path& path);
    void save(const std::filesystem::path& path) const;
    void set(const DoodadPlacement& placement);
    bool contains(std::uint64_t id) const;
    std::vector<DoodadPlacement> inCell(DoodadCell cell) const;
    std::uint64_t revision() const { return revision_; }
    const std::map<std::uint64_t,DoodadPlacement>& changes() const { return changes_; }
private:
    std::string identity_;
    std::map<std::uint64_t,DoodadPlacement> changes_;
    std::map<DoodadCell,std::set<std::uint64_t>> cells_;
    std::uint64_t revision_ = 0;
};
} // namespace viewer
