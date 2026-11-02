#include "boid/bird_assets.hpp"
#include <json.hpp>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
}
int main(int argc, char** argv) {
    try {
        if (argc != 3) throw std::runtime_error("Expected asset path and scratch directory");
        const auto asset = viewer::loadBirdAssets(argv[1]);
        require(asset.meshes[0].indices.size() == 710 * 3, "Near flock mesh must use the approved full pigeon");
        require(asset.meshes[1].indices.size() < 300 * 3 && asset.meshes[2].indices.size() < 80 * 3,
                "Distant pigeon meshes must reduce vertex work");
        require(asset.frameCount > 2 && asset.duration > 0, "Pigeon wing animation missing");
        bool movement = false;
        const auto& mesh = asset.meshes[0];
        for (std::size_t i = 0; i < mesh.uv.size(); ++i)
            movement |= glm::length(mesh.samples[i].position - mesh.samples[i + mesh.uv.size()].position) > .001f;
        require(movement, "Pigeon animation must move the mesh");
        require(viewer::birdDetailLevel(1, asset.radius, 1.732f, 256) == 0, "Near bird must use full detail");
        require(viewer::birdDetailLevel(5, asset.radius, 1.732f, 256) == 1, "Middle bird must use reduced detail");
        require(viewer::birdDetailLevel(30, asset.radius, 1.732f, 256) == 2, "Far bird must use lowest detail");
        require(viewer::birdDetailLevel(5, asset.radius, 3.464f, 256) == 0, "Zoom must increase bird detail");
        require(viewer::birdDetailLevel(5, asset.radius, 1.732f, 512) == 0, "Higher pixel density must increase detail");
        std::ifstream input(argv[1]); auto malformed = nlohmann::json::parse(input);
        malformed["levels"][0]["indices"][0] = 999999;
        std::filesystem::create_directories(argv[2]);
        const auto bad = std::filesystem::path(argv[2]) / "bad-pigeon.json";
        { std::ofstream stream(bad); stream << malformed; }
        bool rejected = false;
        try { (void)viewer::loadBirdAssets(bad); } catch (const std::runtime_error&) { rejected = true; }
        require(rejected, "Invalid pigeon indices must be rejected before upload");
        const auto missing = std::filesystem::path(argv[2]) / "missing-pigeon.json";
        require(!std::filesystem::exists(missing), "Missing-asset fixture must not exist");
        rejected = false;
        try { (void)viewer::loadBirdAssets(missing); }
        catch (const std::runtime_error& error) {
            const std::string message(error.what());
            rejected = message.find(missing.string()) != std::string::npos &&
                message.find("Cannot open pigeon asset") != std::string::npos;
        }
        require(rejected, "Missing pigeon asset must report an open failure with its path");
        std::cout << "Pigeon asset and LoD tests passed\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
