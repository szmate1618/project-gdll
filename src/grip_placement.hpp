#pragma once
#include "grip_interaction.hpp"
#include "grip_preview.hpp"
#include <filesystem>
#include <memory>
#include <vector>

namespace viewer {
class GroundItems;
class PhysicsWorld;
class BoxLayer;
class GripPlacement final : public GripInteraction {
public:
    GripPlacement(GroundItems&,std::shared_ptr<PhysicsWorld>,std::filesystem::path saveDirectory);
    ~GripPlacement() override;
    std::uint64_t begin(const Camera&) override;
    bool active(std::uint64_t) const override;
    void move(std::uint64_t,const Camera&) override;
    void release(std::uint64_t) override;
    glm::quat rotation(std::uint64_t) const override;
    void rotate(std::uint64_t,glm::quat) override;
    void finish(std::uint64_t) override;
    void cancel(std::uint64_t) override;
    const std::vector<GripPreviewPose>& previews() const;
    void setBoxes(BoxLayer* boxes);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
