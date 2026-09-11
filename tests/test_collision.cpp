#include "collision_world.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

void triangle(viewer::Primitive& p, glm::vec3 a, glm::vec3 b, glm::vec3 c) {
    const auto start = static_cast<std::uint32_t>(p.vertices.size());
    for (const auto point : {a, b, c}) { viewer::Vertex v; v.position = point; p.vertices.push_back(v); }
    p.indices.insert(p.indices.end(), {start, start + 1, start + 2});
}

void floor(viewer::Primitive& p, float x0, float z0, float x1, float z1, float y, bool upward = true) {
    const glm::vec3 a(x0, y, z0), b(x0, y, z1), c(x1, y, z1), d(x1, y, z0);
    if (upward) { triangle(p, a, b, c); triangle(p, a, c, d); }
    else { triangle(p, a, c, b); triangle(p, a, d, c); }
}

void draw(viewer::Model& model, viewer::Primitive p, const std::string& name = "",
          glm::mat4 transform = glm::mat4(1)) {
    model.draws.push_back({model.primitives.size(), transform, name});
    model.primitives.push_back(std::move(p));
}

void addGround(viewer::Model& model, float size = 20) {
    viewer::Primitive p; floor(p, -size, -size, size, size, 0);
    draw(model, std::move(p), "terrain");
}

void addBox(viewer::Model& model, const std::string& name = "") {
    viewer::Primitive p;
    floor(p, -2, -2, 2, 2, -0.1f, false);
    floor(p, -2, -2, 2, 2, 4);
    const glm::vec3 a(-2, -0.1f, -2), b(-2, -0.1f, 2), c(2, -0.1f, 2), d(2, -0.1f, -2);
    const glm::vec3 up(0, 4.1f, 0);
    for (const auto& edge : {std::make_pair(a, b), std::make_pair(b, c), std::make_pair(c, d), std::make_pair(d, a)}) {
        triangle(p, edge.first, edge.first + up, edge.second + up);
        triangle(p, edge.first, edge.second + up, edge.second);
    }
    draw(model, std::move(p), name);
}

void flatGroundAndContacts() {
    viewer::Model model; addGround(model);
    const viewer::CollisionWorld world(model);
    const auto hit = world.groundAt(0, 0, -1, 1);
    require(hit && std::abs(hit->height) < 1e-6f && hit->normal.y > 0.999f, "Ground height and normal");
    require(!world.groundAt(0, 0, 0.2f, 1), "Respect bounded vertical query");
    require(!world.groundAt(30, 0, -1, 1), "Respect finite terrain extent");
    require(world.contacts({0, 0.02f, 0}, 0.3f, 1.8f).empty(), "Clear capsule should not collide");
    auto contacts = world.contacts({0, -0.08f, 0}, 0.3f, 1.8f);
    require(!contacts.empty(), "Capsule penetrates floor");
    require(contacts[0].normal.y > 0.999f && std::abs(contacts[0].depth - 0.08f) < 1e-4f, "Floor penetration depth and normal");
    require(!world.contacts({0, 0.02f, 0}, 0.3f, 1.8f, 0.03f).empty(), "Contact skin adds query reach");
    const auto spawn = world.findSpawn({0, 8, 0}, 1.7f, 0.3f, 1.8f, 45);
    require(spawn && spawn->y >= 0 && spawn->y < 0.02f, "Spawn lands on ground below camera");
    require(world.stats().triangles == 2 && world.stats().terrainTriangles == 2 && world.stats().bvhNodes == 1, "Collision counts");
    require(world.debugLines({0, 0, 0}).size() == 12, "Triangle debug lines");
}

void wallsCornersAndSolidSpawn() {
    viewer::Model model; addGround(model); addBox(model);
    const viewer::CollisionWorld world(model);
    const auto contacts = world.contacts({2.2f, 0.1f, 0}, 0.3f, 1.8f);
    require(!contacts.empty() && contacts[0].normal.x > 0.999f && std::abs(contacts[0].depth - 0.1f) < 1e-4f,
            "Capsule side against vertical wall");
    const auto corner = world.contacts({2.15f, 0.1f, 2.15f}, 0.3f, 1.8f);
    require(!corner.empty() && corner[0].normal.x > 0.6f && corner[0].normal.z > 0.6f,
            "Rounded capsule produces diagonal corner response");
    require(world.insideBuilding({0, 0, 0}, 0.3f, 1.8f), "Detect enclosed interior without touching walls");
    require(!world.insideBuilding({0, 4.01f, 0}, 0.3f, 1.8f), "Standing on roof is outside volume");
    require(!world.insideBuilding({3, 0, 0}, 0.3f, 1.8f), "Outside volume");
    const auto spawn = world.findSpawn({0, 10, 0}, 1.7f, 0.3f, 1.8f, 45);
    require(spawn && !world.insideBuilding(*spawn, 0.3f, 1.8f) && spawn->y < 0.1f &&
            (std::abs(spawn->x) > 2.3f || std::abs(spawn->z) > 2.3f), "Move spawn from building onto nearby terrain");
    viewer::Model onlyBox; addBox(onlyBox);
    const viewer::CollisionWorld generic(onlyBox);
    const auto roofSpawn = generic.findSpawn({0, 10, 0}, 1.7f, 0.3f, 1.8f, 45);
    require(roofSpawn && roofSpawn->y > 4 && roofSpawn->y < 4.02f, "Unnamed cube is usable generic support");
    onlyBox.draws[0].transform = glm::scale(glm::mat4(1), glm::vec3(-1, 1, 1));
    const viewer::CollisionWorld mirrored(onlyBox);
    const auto mirroredGround = mirrored.groundAt(0, 0, -1, 10);
    require(mirroredGround && std::abs(mirroredGround->height - 4) < 1e-5f, "Mirrored nodes preserve rendered support orientation");
}

void courtyardRemainsOpen() {
    viewer::Model model; addGround(model);
    viewer::Primitive roof, base;
    // Four strips form a concave ring with a real courtyard hole [-1, 1]^2.
    for (const auto bounds : {glm::vec4(-3, -3, -1, 3), glm::vec4(1, -3, 3, 3),
                              glm::vec4(-1, -3, 1, -1), glm::vec4(-1, 1, 1, 3)}) {
        floor(roof, bounds.x, bounds.y, bounds.z, bounds.w, 4);
        floor(base, bounds.x, bounds.y, bounds.z, bounds.w, -0.1f, false);
    }
    draw(model, std::move(roof), "building_courtyard_roof");
    draw(model, std::move(base), "building_courtyard_walls");
    const viewer::CollisionWorld world(model);
    require(world.stats().buildings == 1 && world.stats().buildingTriangles == 16, "Group roof and walls by building name");
    require(!world.insideBuilding({0, 0, 0}, 0.3f, 1.8f), "Courtyard must not be filled by a building AABB");
    require(world.insideBuilding({2, 0, 0}, 0.3f, 1.8f), "Ring footprint is solid below roof");
    const auto spawn = world.findSpawn({0, 8, 0}, 1.7f, 0.3f, 1.8f, 45);
    require(spawn && std::abs(spawn->x) < 1e-6f && std::abs(spawn->z) < 1e-6f, "Valid courtyard spawn should remain local");
}

void transformedSlopeAndCeiling() {
    viewer::Model model;
    viewer::Primitive p;
    triangle(p, {-5, -5, 0}, {5, -5, 0}, {5, 5, 0});
    triangle(p, {-5, -5, 0}, {5, 5, 0}, {-5, 5, 0});
    auto transform = glm::translate(glm::mat4(1), glm::vec3(7, 2, -3));
    transform = glm::rotate(transform, glm::radians(-70.0f), glm::vec3(1, 0, 0));
    draw(model, std::move(p), "terrain", transform);
    viewer::Primitive ceiling; floor(ceiling, 0, -10, 15, 10, 6, false);
    draw(model, std::move(ceiling));
    const viewer::CollisionWorld world(model);
    const auto ground = world.groundAt(7, -3, -10, 10);
    require(ground && std::abs(ground->height - 2) < 1e-4f && ground->normal.y > 0.93f && ground->normal.y < 0.95f,
            "Ground uses transformed coordinates and geometric slope; ceiling is not support");
    const auto spawn = world.findSpawn({7, 9, -3}, 1.7f, 0.3f, 1.8f, 45);
    require(spawn && world.contacts(*spawn, 0.3f, 1.8f).empty(), "Slope spawn accounts for capsule tangency");
    require(!world.findSpawn({7, 9, -3}, 1.7f, 0.3f, 1.8f, 10), "Spawn respects maximum slope");
    const auto contacts = world.contacts({7, 4.3f, -3}, 0.3f, 1.8f);
    require(!contacts.empty() && contacts[0].normal.y < -0.999f, "Ceiling collision prevents rising capsule");
}

void degenerateAndEmptyGeometry() {
    viewer::Model model; viewer::Primitive p;
    triangle(p, {0, 0, 0}, {1, 0, 0}, {2, 0, 0});
    draw(model, std::move(p));
    const viewer::CollisionWorld world(model);
    require(world.stats().triangles == 0 && world.stats().skippedDegenerateTriangles == 1, "Skip degenerate triangles");
    require(!world.findSpawn({0, 0, 0}, 1.7f, 0.3f, 1.8f, 45), "Empty worlds have no spawn");
    require(world.contacts({0, 0, 0}, 0.3f, 1.8f).empty(), "Empty collision query");
}

}  // namespace

int main() {
    try {
        flatGroundAndContacts(); wallsCornersAndSolidSpawn(); courtyardRemainsOpen();
        transformedSlopeAndCeiling(); degenerateAndEmptyGeometry();
        std::cout << "Collision world tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Collision world test failed: " << error.what() << '\n';
        return 1;
    }
}
