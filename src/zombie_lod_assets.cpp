#include "zombie_lod.hpp"
#include "file_fingerprint.hpp"
#include <json.hpp>
#include <fstream>
#include <iostream>
#include <stdexcept>
namespace viewer {
namespace {
void validateRig(const AnimatedModel& base, const AnimatedModel& lod) {
    bool same = base.skeleton.size() == lod.skeleton.size() && base.frameCount == lod.frameCount &&
        base.duration == lod.duration && base.clipName == lod.clipName && base.boneFrames == lod.boneFrames &&
        base.ragdollBones == lod.ragdollBones;
    for (std::size_t i = 0; same && i < base.skeleton.size(); ++i) {
        const auto& a = base.skeleton[i]; const auto& b = lod.skeleton[i];
        same = a.name == b.name && a.parent == b.parent && a.referenceWorld == b.referenceWorld && a.inverseBind == b.inverseBind;
    }
    for (const auto& pair : {std::pair{&base.walkClip, &lod.walkClip}, std::pair{&base.runClip, &lod.runClip},
                            std::pair{&base.attackLeftClip, &lod.attackLeftClip}, std::pair{&base.attackRightClip, &lod.attackRightClip}}) {
        if (pair.first->has_value() != pair.second->has_value()) same = false;
        else if (*pair.first) {
            const auto& a = **pair.first; const auto& b = **pair.second;
            same = same && a.name == b.name && a.duration == b.duration && a.frameCount == b.frameCount &&
                a.firstFrame == b.firstFrame && a.boneFrames == b.boneFrames && a.loop == b.loop;
        }
    }
    if (!same) throw std::runtime_error("LOD rig or animation differs from the original");
}
}
std::vector<AnimatedModel> loadZombieLods(const std::filesystem::path& basePath, const AnimatedModel& base) {
    const auto manifestPath = basePath.parent_path() / "lods.json";
    if (!std::filesystem::exists(manifestPath)) return {};
    try {
        std::ifstream stream(manifestPath);
        const auto manifest = nlohmann::json::parse(stream);
        if (manifest.at("version") != 1) throw std::runtime_error("Unsupported zombie LOD manifest version");
        const auto& models = manifest.at("models");
        if (!models.contains(basePath.filename().string())) return {};
        const auto& entry = models.at(basePath.filename().string());
        if (entry.at("source_sha256") != fileSha256(basePath)) throw std::runtime_error("Stale zombie LOD source fingerprint");
        const auto& levels = entry.at("levels");
        if (!levels.is_array() || levels.size() != 3) throw std::runtime_error("Expected three zombie LOD levels");
        std::vector<AnimatedModel> result;
        const auto root = std::filesystem::weakly_canonical(basePath.parent_path());
        for (const auto& level : levels) {
            const auto relative = std::filesystem::path(level.at("path").get<std::string>());
            if (relative.is_absolute()) throw std::runtime_error("Absolute zombie LOD path");
            const auto path = std::filesystem::weakly_canonical(root / relative);
            auto tail = path.lexically_relative(root);
            if (tail.empty() || *tail.begin() == "..") throw std::runtime_error("Zombie LOD path leaves asset directory");
            if (level.at("sha256") != fileSha256(path)) throw std::runtime_error("Zombie LOD fingerprint mismatch");
            auto lod = loadZombieModel(path);
            validateRig(base, lod);
            result.push_back(std::move(lod));
        }
        return result;
    } catch (const std::exception& error) {
        // Loading an old local asset cache should not break the viewer. Never
        // combine incompatible rigs: use the authoritative original instead.
        std::cerr << "Zombie LOD warning: " << basePath.filename() << ": " << error.what()
                  << "; using original mesh. Rebuild with tools/prepare_zombie_lods.py\n";
        return {};
    }
}
}
