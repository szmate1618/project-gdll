#include "shrub_cover.hpp"
#include "collision_world.hpp"
#include "tree_layer.hpp"
#include "zombie_attack.hpp"
#include "zombie_layer.hpp"
#include "zombie_perception.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <array>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

viewer::TreeLayer shrub(int width = 4, int height = 4) {
    viewer::TreeLayer trees;
    viewer::TreeAsset asset;
    auto& model = asset.model;
    viewer::ImageData image;
    image.width = width;
    image.height = height;
    image.rgba.assign(static_cast<std::size_t>(width) * height * 4, 255);
    model.images.push_back(std::move(image));
    viewer::Texture texture;
    texture.image = 0;
    texture.wrapS = texture.wrapT = 33071; // GL_CLAMP_TO_EDGE, as on forest shrubs.
    model.textures.push_back(texture);
    viewer::Material material;
    material.baseColorTexture = 0;
    material.alphaMode = "MASK";
    material.alphaCutoff = .35f;
    material.doubleSided = true;
    model.materials.push_back(material);
    viewer::Primitive card;
    card.material = 0;
    const std::array<glm::vec3, 4> positions{{{-1, 2, 5}, {-1, 0, 5}, {1, 0, 5}, {1, 2, 5}}};
    const std::array<glm::vec2, 4> uvs{{{0, 0}, {0, 1}, {1, 1}, {1, 0}}};
    for (std::size_t i = 0; i < positions.size(); ++i) {
        viewer::Vertex vertex;
        vertex.position = positions[i];
        vertex.uv = uvs[i];
        card.vertices.push_back(vertex);
    }
    card.indices = {0, 1, 2, 0, 2, 3};
    model.primitives.push_back(std::move(card));
    model.draws.push_back({0, glm::mat4(1), "forest_shrub"});
    model.boundsMin = {-1, 0, 5};
    model.boundsMax = {1, 2, 5};
    trees.assets.push_back(std::move(asset));
    viewer::TreeInstance instance;
    instance.undergrowth = viewer::UndergrowthKind::leafy;
    instance.collidable = false;
    trees.instances.push_back(instance);
    return trees;
}

void alpha(viewer::TreeLayer& trees, int x, int y, std::uint8_t value) {
    auto& image = trees.assets[0].model.images[0];
    image.rgba[(static_cast<std::size_t>(y) * image.width + x) * 4 + 3] = value;
}

bool blocked(const viewer::ShrubCover& cover, float x, float y) {
    return cover.blocksSight({x, y, 0}, {x, y, 10});
}

void silhouetteAndNativeImageOrientation() {
    auto trees = shrub();
    for (int y = 0; y < 4; ++y) for (int x = 0; x < 4; ++x)
        alpha(trees, x, y, x < 2 && y < 2 ? 255 : 0);
    const viewer::ShrubCover cover(trees);
    require(cover.size() == 1 && blocked(cover, -.75f, 1.75f),
            "Opaque shrub texels must block the sight segment");
    require(cover.blocksSight({-.75f, 1.75f, 10}, {-.75f, 1.75f, 0}),
            "Shrub alpha masks must block from both sides");
    require(!blocked(cover, .75f, 1.75f) && !blocked(cover, -.75f, .25f),
            "Transparent texels must stay clear with native, unflipped image UVs");
    require(!blocked(cover, -2, 1.75f) && !blocked(cover, -.75f, 3),
            "Sight outside the actual shrub card must stay clear");
    require(!cover.blocksSight({-.75f, 1.75f, 0}, {-.75f, 1.75f, 4}),
            "An opaque shrub beyond the target must not hide it");
    require(!cover.blocksSight({-.75f, 1.75f, 6}, {-.75f, 1.75f, 10}),
            "An opaque shrub behind the observer must not hide the target");
}

void nativeAlphaAndMaterialFactors() {
    auto trees = shrub(2, 1);
    alpha(trees, 0, 0, 0);
    const viewer::ShrubCover native(trees);
    require(!blocked(native, -.01f, 1) && blocked(native, .01f, 1),
            "Native nearest texel sampling must preserve sharp transparent gaps");

    trees.assets[0].model.materials[0].baseColor.a = .3f;
    const viewer::ShrubCover faintMaterial(trees);
    require(!blocked(faintMaterial, .75f, 1), "Material alpha must multiply texture alpha before cutoff");
    trees.assets[0].model.materials[0].baseColor.a = 1;
    alpha(trees, 0, 0, 255);
    auto& vertices = trees.assets[0].model.primitives[0].vertices;
    vertices[0].color.a = vertices[1].color.a = 0;
    const viewer::ShrubCover vertexFade(trees);
    require(!blocked(vertexFade, -.75f, 1) && blocked(vertexFade, .75f, 1),
            "Interpolated vertex alpha must participate in the rendered silhouette");

    auto equal = shrub(1, 1);
    alpha(equal, 0, 0, 128);
    equal.assets[0].model.materials[0].alphaCutoff = 128.f / 255.f;
    const viewer::ShrubCover cutoff(equal);
    require(blocked(cutoff, 0, 1), "Alpha exactly at the cutoff remains opaque");
}

void fullTransformsAndAssetOwnership() {
    auto trees = shrub();
    for (int y = 0; y < 4; ++y) for (int x = 2; x < 4; ++x) alpha(trees, x, y, 0);
    auto& draw = trees.assets[0].model.draws[0];
    draw.transform = glm::translate(glm::mat4(1), {1, .2f, -.5f}) *
                     glm::rotate(glm::mat4(1), glm::radians(27.f), glm::vec3(0, 1, 0));
    glm::mat4 placement(1);
    placement[0] = {-2, .1f, .25f, 0}; // Reflection, nonuniform scale and shear.
    placement[1] = {.35f, 1.7f, .1f, 0};
    placement[2] = {.2f, .15f, .6f, 0};
    placement[3] = {7, 3, -4, 1};
    trees.instances[0].transform = placement;
    const auto composed = placement * draw.transform;
    const auto point = [&](glm::vec3 local) { return glm::vec3(composed * glm::vec4(local, 1)); };
    const viewer::ShrubCover cover(trees);
    require(cover.blocksSight(point({-.75f, 1.75f, 0}), point({-.75f, 1.75f, 10})),
            "Sight must compose the complete instance and asset-node transforms exactly once");
    require(!cover.blocksSight(point({.75f, 1.75f, 0}), point({.75f, 1.75f, 10})),
            "Transformed alpha gaps must remain aligned with rendered texels");

    const auto owned = [] {
        auto source = shrub();
        auto second = source.instances[0];
        second.transform = glm::translate(glm::mat4(1), {8, 0, 0});
        source.instances.push_back(second);
        return viewer::ShrubCover(source);
    }();
    require(owned.size() == 2 && blocked(owned, 0, 1) && blocked(owned, 8, 1),
            "Shared shrub assets must remain usable after the source layer is destroyed");
}

void shrubSelectionAndSpatialIndex() {
    const viewer::ShrubCover empty(viewer::TreeLayer{});
    require(empty.size() == 0 && !blocked(empty, 0, 1), "An empty shrub layer cannot block sight");
    auto trees = shrub();
    trees.instances[0].undergrowth = viewer::UndergrowthKind::none;
    auto dry = trees.instances[0];
    dry.undergrowth = viewer::UndergrowthKind::dry;
    dry.transform = glm::translate(glm::mat4(1), {8, 0, 0});
    trees.instances.push_back(dry);
    const viewer::ShrubCover selected(trees);
    require(selected.size() == 1 && !blocked(selected, 0, 1) && blocked(selected, 8, 1),
            "Dry shrubs provide cover while ordinary tree canopy cards stay excluded");

    trees.instances.clear();
    for (int i = 0; i < 128; ++i) {
        dry.transform = glm::translate(glm::mat4(1), {i * 10.f, 0, i % 2 ? 30.f : 0.f});
        trees.instances.push_back(dry);
    }
    const viewer::ShrubCover indexed(trees);
    require(indexed.size() == 128 && blocked(indexed, 1200, 1) && !blocked(indexed, 1210, 1),
            "Spatial selection must retain crossed shrub cards and reject distant cards");
}

void gameplaySightWithoutSolidCollision() {
    auto trees = shrub();
    viewer::CollisionWorld world(viewer::Model{});
    world.setShrubCover(trees);
    require(!world.perceptionLineOfSight({0, 1, 0}, {0, 1, 10}),
            "The perception query must include opaque shrub texels");
    require(world.lineOfSight({0, 1, 0}, {0, 1, 10}),
            "Shrub cover must not obstruct weapon, blast or interaction geometry queries");
    require(world.contacts({0, .1f, 5}, .3f, 1.8f).empty() && !world.groundAt(0, 5, -1, 3),
            "Sight cover must not create solid walking contacts or supporting ground");
    viewer::ZombieInstance zombie;
    std::size_t rays = 0;
    const viewer::ZombiePlayer standing{{0, 0, 10}};
    require(viewer::sampleZombieSight(zombie, standing, world, rays) == viewer::ZombieSight::none,
            "A fully opaque shrub must hide all three player sight samples");

    // At z=5, standing head sight passes above 1.5 m cover; lower targets do not.
    for (auto& vertex : trees.assets[0].model.primitives[0].vertices)
        if (vertex.position.y > 0) vertex.position.y = 1.5f;
    trees.assets[0].model.boundsMax.y = 1.5f;
    world.setShrubCover(trees);
    require(viewer::sampleZombieSight(zombie, standing, world, rays) == viewer::ZombieSight::partial,
            "Seeing a standing player's head over a shrub must remain a partial sighting");
    viewer::ZombiePlayer crouched = standing;
    crouched.eyeHeight = 1;
    crouched.height = 1.1f;
    require(viewer::sampleZombieSight(zombie, crouched, world, rays) == viewer::ZombieSight::none,
            "Lowered crouching sight targets must fit behind the same shrub cover");

    for (int y = 0; y < 4; ++y) for (int x = 0; x < 4; ++x) alpha(trees, x, y, 0);
    world.setShrubCover(trees);
    require(viewer::sampleZombieSight(zombie, standing, world, rays) == viewer::ZombieSight::full,
            "Transparent gaps must preserve the normal full-sighting classification");

    auto nearby = shrub();
    nearby.instances[0].transform = glm::translate(glm::mat4(1), {0, 0, -4.5f});
    world.setShrubCover(nearby);
    const viewer::ZombiePlayer close{{0, 0, 1}};
    require(!viewer::canZombieAttack(zombie, close, world, rays),
            "Opaque bush cover must prevent starting an attack on a hidden nearby player");
    world.setShrubCover(viewer::TreeLayer{});
    require(viewer::canZombieAttack(zombie, close, world, rays),
            "A nearby player becomes attackable when bush cover is removed");
}
} // namespace

int main() {
    try {
        silhouetteAndNativeImageOrientation();
        nativeAlphaAndMaterialFactors();
        fullTransformsAndAssetOwnership();
        shrubSelectionAndSpatialIndex();
        gameplaySightWithoutSolidCollision();
        std::cout << "Shrub alpha-mask cover tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
