#include "item_model_config.hpp"

#include <toml++/toml.hpp>
#include <glm/common.hpp>
#include <glm/vector_relational.hpp>
#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <stdexcept>

namespace viewer {
namespace {
[[noreturn]] void invalid(const std::filesystem::path& file, const std::string& reason) {
    throw std::runtime_error(file.string() + ": model " + reason);
}
float number(const toml::node& node, const std::filesystem::path& file, const std::string& field) {
    const auto value = node.value<double>();
    if (!value || !std::isfinite(*value) || std::abs(*value) > std::numeric_limits<float>::max())
        invalid(file, field + " must be a finite number");
    return static_cast<float>(*value);
}
template<int Count>
glm::vec<Count, float, glm::defaultp> vector(const toml::table& table, const char* key,
    const std::filesystem::path& file, const std::string& part) {
    const auto* array = table[key].as_array();
    if (!array || array->size() != static_cast<std::size_t>(Count)) invalid(file, part + "." + key + " must have " + std::to_string(Count) + " numbers");
    glm::vec<Count, float, glm::defaultp> result{};
    for (int i = 0; i < Count; ++i) result[i] = number(*array->get(static_cast<std::size_t>(i)), file, part + "." + key);
    return result;
}
std::string label(const toml::table& table, const char* key, const std::filesystem::path& file) {
    const auto value = table[key].value<std::string>();
    if (!value || value->empty()) invalid(file, std::string(key) + " must be a nonempty string");
    return *value;
}
} // namespace

ItemModelConfig loadItemModelConfig(const std::filesystem::path& file) {
    toml::table descriptor;
    try { descriptor = toml::parse_file(file.string()); }
    catch (const toml::parse_error& error) { invalid(file, std::string(error.description())); }
    if (!descriptor.contains("model")) return {};
    const auto* model = descriptor["model"].as_table();
    if (!model) invalid(file, "must be a table");
    const auto* mass = model->get("mass_kg");
    if (!mass) invalid(file, "mass_kg is required");
    ItemModelConfig result;
    result.massKg = number(*mass, file, "mass_kg");
    if (result.massKg <= 0) invalid(file, "mass_kg must be positive");
    const auto* parts = (*model)["parts"].as_array();
    if (!parts || parts->empty() || parts->size() > 64) invalid(file, "parts must contain 1–64 part tables");
    std::set<std::string> names;
    glm::vec3 minimum(std::numeric_limits<float>::infinity()), maximum(-minimum);
    for (const auto& node : *parts) {
        const auto* entry = node.as_table();
        if (!entry) invalid(file, "parts must contain tables");
        ItemModelPart part;
        part.name = label(*entry, "name", file);
        if (!names.insert(part.name).second) invalid(file, "duplicate part name " + part.name);
        const auto shape = label(*entry, "shape", file);
        if (shape == "box") part.shape = ItemModelShape::Box;
        else if (shape == "frustum") part.shape = ItemModelShape::Frustum;
        else invalid(file, part.name + ".shape must be 'box' or 'frustum'");
        part.material = label(*entry, "material", file);
        if (!std::all_of(part.material.begin(), part.material.end(), [](char c) {
            return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
        })) invalid(file, part.name + ".material must be a lowercase material ID");
        part.center = vector<3>(*entry, "center", file, part.name);
        part.size = vector<3>(*entry, "size", file, part.name);
        part.color = vector<3>(*entry, "color", file, part.name);
        if (glm::any(glm::lessThanEqual(part.size, glm::vec3(0))) || glm::any(glm::greaterThan(part.size, glm::vec3(4))))
            invalid(file, part.name + ".size must be positive and at most 4 metres");
        if (glm::any(glm::lessThan(part.color, glm::vec3(0))) || glm::any(glm::greaterThan(part.color, glm::vec3(1))))
            invalid(file, part.name + ".color must be between 0 and 1");
        part.topSize = {part.size.x, part.size.z};
        if (part.shape == ItemModelShape::Frustum) {
            part.topSize = vector<2>(*entry, "top_size", file, part.name);
            if (glm::any(glm::lessThanEqual(part.topSize, glm::vec2(0))) || glm::any(glm::greaterThan(part.topSize, glm::vec2(4))))
                invalid(file, part.name + ".top_size must be positive and at most 4 metres");
        } else if (entry->contains("top_size")) invalid(file, part.name + ".top_size is only valid for a frustum");
        const glm::vec3 extent(std::max(part.size.x, part.topSize.x), part.size.y, std::max(part.size.z, part.topSize.y));
        minimum = glm::min(minimum, part.center - extent * .5f);
        maximum = glm::max(maximum, part.center + extent * .5f);
        result.parts.push_back(std::move(part));
    }
    if (glm::any(glm::greaterThan(maximum - minimum, glm::vec3(4))))
        invalid(file, "combined bounds exceed the 4 metre physical-body limit");
    return result;
}
} // namespace viewer
