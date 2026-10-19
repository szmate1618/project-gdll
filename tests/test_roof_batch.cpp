#include "building_batch.hpp"

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <glm/gtc/matrix_transform.hpp>

namespace {
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

viewer::Model fixture() {
    viewer::Model model;
    model.materials.resize(2);
    viewer::Primitive roof;
    roof.material = 0;
    for (const auto position : {glm::vec3(1, 0, 1), glm::vec3(1, 0, 3), glm::vec3(3, 0, 1)}) {
        viewer::Vertex vertex;
        vertex.position = position;
        vertex.normal = {0, 1, 0};
        vertex.uv = {position.x, position.z};
        vertex.color = {.2f, .4f, .6f, 1};
        roof.vertices.push_back(vertex);
    }
    roof.indices = {0, 1, 2};
    model.primitives.push_back(roof);
    model.draws.push_back({0, glm::mat4(1), "building_a_roof"});
    model.draws.push_back({0, glm::translate(glm::mat4(1), {8, 0, 0}), "building_b_roof"});
    return model;
}

void groupingAndEligibility() {
    auto model = fixture();
    auto secondMaterial = model.primitives[0];
    secondMaterial.material = 1;
    model.primitives.push_back(secondMaterial);
    model.draws.push_back({1, glm::mat4(1), "building_c_roof"});
    model.draws.push_back({0, glm::translate(glm::mat4(1), {128, 0, 0}), "building_d_roof"});
    model.draws.push_back({0, glm::translate(glm::mat4(1), {-8, 0, 0}), "building_e_roof"});
    const auto firstExcluded = model.draws.size();
    model.draws.push_back({0, glm::mat4(1), "building_a_walls"});
    model.draws.push_back({0, glm::mat4(1), "building_a_doors"});
    model.draws.push_back({0, glm::mat4(1), "building_detail_roof", 120});
    model.draws.push_back({0, glm::mat4(1), "building_moving_roof"});
    model.draws.back().horizontalMotionRadius = 2;
    model.draws.push_back({0, glm::mat4(1), "building_group_roof"});
    model.draws.back().detailGroup = "alternative";
    model.draws.push_back({0, glm::mat4(1), "building_fence_roof"});
    model.draws.back().fence = true;
    auto transparent = model.primitives[0];
    transparent.material = 2;
    model.primitives.push_back(transparent);
    model.materials.emplace_back();
    model.materials.back().alphaMode = "BLEND";
    model.draws.push_back({2, glm::mat4(1), "building_blended_roof"});
    model.draws.push_back({0, glm::scale(glm::mat4(1), {1, 0, 1}), "building_flat_roof"});
    const auto batches = viewer::buildRoofBatches(model);
    require(batches.meshes.size() == 4, "Material or positive/negative spatial cells were merged");
    require(batches.batchForDraw[0] == batches.batchForDraw[1], "Compatible distinct roofs did not merge");
    require(batches.meshes[0].indices == std::vector<std::uint32_t>({0, 1, 2, 3, 4, 5}),
            "Concatenated roof indices do not reference the appended vertices");
    for (std::size_t i = firstExcluded; i < model.draws.size(); ++i)
        require(batches.batchForDraw[i] == viewer::noBuildingBatch, "Non-static or non-opaque draw was batched");
    require(model.draws.size() == batches.batchForDraw.size() && model.primitives[0].vertices.size() == 3,
            "Batch preparation modified the source model");
    require(viewer::buildRoofBatches(viewer::Model{}).meshes.empty(), "Empty model produced batches");
}

void transformsAndWinding() {
    auto model = fixture();
    model.draws.resize(1);
    glm::mat4 transform(1);
    transform[0][0] = -2;
    transform[1][1] = 3;
    transform[2][0] = .5f; // Shear in X from Z.
    transform[3] = {20, 5, 10, 1};
    model.draws[0].transform = transform;
    const auto batches = viewer::buildRoofBatches(model);
    const auto& mesh = batches.meshes.at(0);
    require(glm::length(mesh.vertices[0].position - glm::vec3(18.5f, 5, 11)) < 1e-5f,
            "Runtime world transform was not baked exactly once");
    require(glm::length(mesh.vertices[0].normal - glm::vec3(0, 1.f/3, 0)) < 1e-5f,
            "Non-uniform scale did not preserve transformed normal magnitude");
    const auto a = mesh.vertices[mesh.indices[0]].position;
    const auto b = mesh.vertices[mesh.indices[1]].position;
    const auto c = mesh.vertices[mesh.indices[2]].position;
    require(glm::dot(glm::cross(b-a, c-a), mesh.vertices[0].normal) > 0,
            "Reflected roof has inward triangle winding");
    require(mesh.vertices[0].uv == model.primitives[0].vertices[0].uv &&
            mesh.vertices[0].color == model.primitives[0].vertices[0].color,
            "Batching changed texture coordinates or vertex colors");
    model.primitives[0].indices[0] = 99;
    bool rejected = false;
    try { viewer::buildRoofBatches(model); }
    catch (const std::invalid_argument&) { rejected = true; }
    require(rejected, "Invalid roof indices were accepted");
}
} // namespace

int main() {
    try { groupingAndEligibility(); transformsAndWinding(); }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
    std::cout << "Roof batching geometry checks passed\n";
}
