#pragma once

#include "house_layout.hpp"

namespace viewer::house_layout::rules {
class Random {
public:
    Random(const std::string& building, std::uint64_t domain);
    std::uint32_t pick(std::uint32_t count);
private:
    std::uint64_t state_;
};
float area(Rectangle r);
bool usable(Rectangle r);
bool usable(Rectangle r, const std::vector<glm::vec2>& outline, float minimumSide = 2.5f, float minimumArea = 10.f);
bool openingsFit(const Partition& wall, const std::vector<glm::vec2>& outline);
glm::vec2 point(int axis, float position, float along);
bool contains(Rectangle r, glm::vec2 p, float margin = .01f);
bool clearsWindows(int axis, float cut, float low, float high, const std::vector<Rectangle>& windows);
std::vector<float> partitionPositions(int axis, float low, float high, float preferred,
                                      float start, float end, const std::vector<Rectangle>& windows);
struct StairOption { std::size_t room; Rectangle ramp; int axis; bool ascending, exterior; };
std::vector<StairOption> stairOptions(const std::vector<Room>& rooms, const std::vector<Partition>& partitions,
    const std::vector<Entrance>& entrances, Rectangle footprint, float rise,
    const std::vector<Rectangle>& blocked = {}, const std::vector<glm::vec2>& outline = {});
bool splitClearsDoors(const Room& room, int axis, float cut, const std::vector<Partition>& walls,
                      const std::vector<Entrance>& entrances);
void subdivideLiving(std::vector<Room>& rooms, std::vector<Partition>& walls, Random& random,
    Rectangle footprint, const std::vector<Entrance>& entrances, const std::vector<Rectangle>& windows, float rise = 0,
    const std::vector<glm::vec2>& outline = {});
Stair makeStair(const StairOption& option, std::size_t room, float bottom, float top);
} // namespace viewer::house_layout::rules
