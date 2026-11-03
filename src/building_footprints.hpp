#pragma once

#include "model.hpp"
#include "visibility.hpp"
#include <map>
#include <optional>

namespace viewer {

// Immutable world-space building footprints, independent of interior residency.
// Rings retain concavity and courtyard holes. Heights include all shell vertices.
class BuildingFootprints {
public:
    explicit BuildingFootprints(const Model& model);
    std::optional<std::string> find(glm::vec3 point) const;
    bool contains(const std::string& building, glm::vec3 point) const;
    std::size_t size() const { return buildings_.size(); }
private:
    struct Building {
        std::string id;
        std::vector<std::vector<glm::vec2>> rings;
        float low, high, area;
        bool contains(glm::vec3 point) const;
    };
    std::vector<Building> buildings_;
    std::map<std::string, std::size_t> byId_;
    VisibilityIndex index_;
};

} // namespace viewer
