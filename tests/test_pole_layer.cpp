#include "pole_layer.hpp"
#include "file_fingerprint.hpp"
#include "collision_world.hpp"
#include "model_visibility.hpp"

#include <json.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
using Json = nlohmann::json;
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void geometry() {
    const std::vector<viewer::PoleColumn> columns{{"a", {10, 3, 10}, 7, .2f},
        {"b", {20, 3, 10}, 9, .3f}, {"c", {-1, 3, 10}, 25, 1}};
    auto model = viewer::buildPoleBatches(columns);
    require(model.draws.size() == 2, "Two nearby poles must share one spatial batch; negative cell stays separate");
    require(model.primitives[0].indices.size() == 72, "Two columns need 24 triangles in one draw");
    require(model.boundsMax.y == 28, "Column height must extend upward from terrain base");
    for (const auto& mesh : model.primitives) {
        for (std::size_t i = 0; i < mesh.indices.size(); i += 3) {
            const auto& a = mesh.vertices.at(mesh.indices[i]);
            const auto& b = mesh.vertices.at(mesh.indices[i + 1]);
            const auto& c = mesh.vertices.at(mesh.indices[i + 2]);
            require(glm::dot(glm::cross(b.position - a.position, c.position - a.position), a.normal) > 0,
                    "Every column face needs outward CCW winding and matching normal");
        }
    }
    const viewer::CollisionWorld world(model);
    require(world.stats().triangles == 36 && world.stats().terrainTriangles == 0, "Pole collision classification");
    require(!world.lineOfSight({9, 4, 10}, {11, 4, 10}), "Pole must block sight through its side");
    require(world.lineOfSight({9, 4, 11}, {11, 4, 11}), "Empty space beside a pole must stay clear");
    require(!world.contacts({10.2f, 3, 10}, .3f, 1.8f).empty(), "Player must collide with pole sides");
    require(!world.groundAt(10, 10, 0, 20, true), "Pole tops must not become terrain ground");
    viewer::ModelVisibility visibility(model);
    std::vector<std::size_t> visible;
    visibility.query(glm::ortho(-100.f, 100.f, -100.f, 100.f, -100.f, 100.f), visible);
    require(visible.size() == 2, "Visibility must index batches rather than individual columns");
    viewer::Model town;
    town.primitives.resize(1);
    town.materials.resize(2);
    town.draws.push_back({0, glm::mat4(1), "existing"});
    viewer::appendPoleLayer(town, {columns, std::move(model)});
    require(town.draws[0].primitive == 0 && town.draws[1].primitive == 1,
            "Appending poles must preserve original draw indices and offset new primitives");
    require(town.primitives[1].material == 2, "Pole material index must be offset into town materials");
    require(viewer::buildPoleBatches({}).draws.empty(), "No mapped poles is a valid empty layer");
}

void manifest(const std::filesystem::path& root) {
    const auto map = root / "map.glb", path = root / "poles.json";
    std::ofstream(map) << "fingerprinted map fixture";
    Json document = {{"schema", "godollo.pole-instances"}, {"version", 1},
        {"map", {{"map_sha256", viewer::fileSha256(map)}}},
        {"columns", Json::array({{{"id", "node/1"}, {"kind", "street_lamp"},
                                  {"position", {10, 3, -10}}, {"height_m", 7}, {"width_m", .2}}})}};
    const auto write = [&](const Json& data) { std::ofstream(path) << data.dump(); };
    write(document);
    const auto loaded = viewer::loadPoleLayer(path, map);
    require(loaded.columns.size() == 1 && loaded.columns[0].position.z == -10, "Manifest runtime frame changed");
    const auto rejects = [&](const Json& data) {
        write(data);
        try { (void)viewer::loadPoleLayer(path, map); }
        catch (const std::runtime_error& error) {
            require(std::string(error.what()).find(path.string()) != std::string::npos, "Error must include manifest path");
            return;
        }
        throw std::runtime_error("Malformed or stale pole manifest was accepted");
    };
    auto bad = document;
    bad["map"]["map_sha256"] = std::string(64, '0');
    rejects(bad);
    bad = document; bad["columns"].push_back(bad["columns"][0]); rejects(bad);
    for (const auto& value : {Json(-1), Json(0), Json("7"), Json(101), Json(nullptr)}) {
        bad = document; bad["columns"][0]["height_m"] = value; rejects(bad);
    }
    bad = document; bad["version"] = 3; rejects(bad);
    bad = document; bad["columns"][0]["position"] = {1, 2}; rejects(bad);
    bad = document; bad["columns"][0]["kind"] = "unknown"; rejects(bad);
    bad = document; bad["columns"][0]["kind"] = "hev_catenary"; rejects(bad);
    auto green = document;
    green["version"] = 2;
    auto hev = green["columns"][0];
    hev["id"] = "hev/0";
    hev["kind"] = "hev_catenary";
    green["columns"].push_back(hev);
    write(green);
    const auto mixed = viewer::loadPoleLayer(path, map);
    require(mixed.columns[1].kind == viewer::PoleKind::hevCatenary, "HÉV kind lost in loader");
    require(mixed.batches.draws.size() == 2, "Grey and green columns in one cell need separate batches");
    require(mixed.batches.primitives[0].material == 0 && mixed.batches.primitives[1].material == 1,
            "Grey and HÉV material assignments must remain distinct");
    const auto color = mixed.batches.materials[1].baseColor;
    require(color.g > color.r && color.g > color.b && color.g < .5f,
            "HÉV columns must be muted green darker than medium green");
}
} // namespace

int main() {
    const auto root = std::filesystem::current_path() /
        ("pole-fixtures-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(root);
    int result = 0;
    try { geometry(); manifest(root); std::cout << "Pole batching, geometry, collision and manifests passed\n"; }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; result = 1; }
    std::filesystem::remove_all(root);
    return result;
}
