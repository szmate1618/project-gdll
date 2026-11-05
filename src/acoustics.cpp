#include "acoustics.hpp"
#include <toml++/toml.hpp>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace viewer {
namespace {
constexpr const char* suffix = ".acoustics.toml";
float energy(float mass, float speed) { return .5f * mass * speed * speed; }
}
bool validImpactSound(const ImpactSound& s) {
    return static_cast<unsigned>(s.material) < acousticMaterialCount &&
        std::isfinite(s.position.x) && std::isfinite(s.position.y) && std::isfinite(s.position.z) &&
        std::isfinite(s.sharpness) && s.sharpness >= 0 && s.sharpness <= 1 &&
        std::isfinite(s.hollowness) && s.hollowness >= 0 && s.hollowness <= 1 &&
        std::isfinite(s.size) && s.size >= .25f && s.size <= 4 &&
        std::isfinite(s.gain) && s.gain >= 0 && s.gain <= 1;
}
std::string acousticConfigId(const std::filesystem::path& path) {
    const auto name = path.filename().string();
    const std::string ending = suffix;
    if (name.size() <= ending.size() || name.compare(name.size()-ending.size(), ending.size(), ending))
        throw std::runtime_error("Expected name.acoustics.toml: " + path.string());
    return name.substr(0, name.size()-ending.size());
}
std::vector<std::filesystem::path> acousticConfigFiles(const std::filesystem::path& storage) {
    std::vector<std::filesystem::path> result;
    if (storage.empty()) return result;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(storage)) {
        const auto name = entry.path().filename().string();
        const std::string ending = suffix;
        if (entry.is_regular_file() && name.size() > ending.size() &&
            name.compare(name.size()-ending.size(), ending.size(), ending) == 0) result.push_back(entry.path());
    }
    std::sort(result.begin(), result.end());
    return result;
}
AcousticConfig loadAcousticConfig(const std::filesystem::path& path) {
    try {
        const auto table = toml::parse_file(path.string());
        if (!table["schema_version"].is_integer() || table["schema_version"].value<int>() != 1)
            throw std::runtime_error("schema_version must be integer 1");
        const auto material = table["material"].value<std::string>();
        const std::vector<std::string> names{"metal", "wood", "glass", "rock", "soft"};
        const auto found = material ? std::find(names.begin(), names.end(), *material) : names.end();
        if (found == names.end()) throw std::runtime_error("material must be metal, wood, glass, rock or soft");
        AcousticConfig c;
        c.material = static_cast<AcousticMaterial>(found-names.begin());
        const auto number = [&](const char* key, float low, float high) {
            const auto value = table[key].value<double>();
            if (!value || !std::isfinite(*value) || *value < low || *value > high)
                throw std::runtime_error(std::string("Invalid acoustic field: ") + key);
            return static_cast<float>(*value);
        };
        c.hollowness = number("hollowness", 0, 1);
        c.size = number("size", .25f, 4);
        c.gain = number("gain", 0, 1);
        c.minSpeed = number("min_speed", .1f, 20);
        c.noiseDb = number("noise_db", 0, 90);
        for (const auto& [key, value] : table) {
            (void)value;
            if (key != "schema_version" && key != "material" && key != "hollowness" && key != "size" &&
                key != "gain" && key != "min_speed" && key != "noise_db")
                throw std::runtime_error("Unknown acoustic field: " + std::string(key.str()));
        }
        return c;
    } catch (const std::exception& e) { throw std::runtime_error(path.string() + ": " + e.what()); }
}
ImpactSound makeImpactSound(const AcousticConfig& c, float mass, float speed, float scale, bool hard, glm::vec3 p) {
    const float e = std::clamp(energy(mass, speed), 0.f, 10000.f);
    return {p, c.material, hard, std::clamp(std::log1p(e)/std::log(41.f), 0.f, 1.f), c.hollowness,
            std::clamp(c.size*scale, .25f, 4.f), std::clamp(c.gain*.6f*std::pow(e, .25f), 0.f, 1.f)};
}
float impactNoiseDb(const AcousticConfig& c, float mass, float speed) {
    return std::clamp(c.noiseDb + 10*std::log10(std::max(energy(mass, speed), .0001f)), 0.f, 100.f);
}
} // namespace viewer
