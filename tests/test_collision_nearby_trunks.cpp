#include "collision_world.hpp"

#include <array>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <stdexcept>
#include <vector>

namespace {
std::atomic<std::size_t> allocations{0};
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void nearbyTrunkQueries() {
    std::vector<viewer::TrunkCollider> trunks;
    for (int x = -8; x <= 8; ++x) for (int z = -8; z <= 8; ++z) {
        // Vary height and radius to exercise vertical and exact-circle filtering.
        trunks.push_back({{float(x), float((x + z + 16) % 5), float(z)},
                          .1f + .025f * ((x + 8) % 4), 2.0f});
    }
    const viewer::CollisionWorld world({}, trunks);
    const viewer::CollisionWorld isolated({}, {{{1.25f, 0, 0}, .25f, 4},
                                               {{1.2f, 0, 1.2f}, .25f, 4},
                                               {{0, 4, 0}, .25f, 4}});
    const viewer::CollisionWorld empty({});
    std::array<viewer::NearbyTrunk, 300> hits{}, again{};
    const auto before = allocations.load();
    bool overflow = true;
    require(isolated.nearbyTrunks({0, 0, 0}, 1.8f, 1, hits.data(), hits.size(), overflow) == 1 &&
            !overflow && hits[0].id == 0 && hits[0].collider.base.x == 1.25f,
            "Nearby trunk range must include the surface boundary and reject box-only/vertical candidates");
    require(isolated.nearbyTrunks({0, 2.2f, 0}, 1.8f, 0, hits.data(), hits.size(), overflow) == 1 &&
            !overflow && hits[0].id == 2,
            "Closed vertical interval touching must include the authoritative trunk");
    require(isolated.nearbyTrunks({0, 0, 0}, 1.8f, 1, nullptr, 20, overflow) == 0 && overflow,
            "Null output must be a safe zero-capacity query with overflow detection");
    require(isolated.nearbyTrunks({0, 0, 0}, 1.8f, 1, hits.data(), 0, overflow) == 0 && overflow,
            "Zero-capacity output must report omitted nearby trunks");
    for (int query = 0; query < 41; ++query) {
        const glm::vec3 feet(-7.7f + .37f * query, float(query % 7), -6.1f + .31f * query);
        const float range = .5f + .17f * (query % 9), height = .7f + .2f * (query % 4);
        const auto count = world.nearbyTrunks(feet, height, range, hits.data(), hits.size(), overflow);
        require(!overflow, "Complete BVH query unexpectedly overflowed its buffer");
        bool repeatOverflow = true;
        const auto repeated = world.nearbyTrunks(feet, height, range, again.data(), again.size(), repeatOverflow);
        require(repeated == count && !repeatOverflow, "Repeated trunk query changed its count");
        std::array<bool, 300> present{};
        for (std::size_t i = 0; i < count; ++i) {
            const auto id = hits[i].id;
            require(id < trunks.size() && !present[id] && id == again[i].id,
                    "Trunk IDs must remain stable, unique, and deterministic");
            present[id] = true;
            require(hits[i].collider.base == trunks[id].base &&
                    hits[i].collider.radius == trunks[id].radius &&
                    hits[i].collider.height == trunks[id].height,
                    "Nearby query changed authoritative collision geometry");
        }
        std::size_t expected = 0;
        for (std::size_t id = 0; id < trunks.size(); ++id) {
            const auto& trunk = trunks[id];
            const double dx = double(feet.x) - trunk.base.x, dz = double(feet.z) - trunk.base.z;
            const bool inside = std::hypot(dx, dz) <= double(range) + trunk.radius &&
                                feet.y <= trunk.base.y + trunk.height &&
                                feet.y + height >= trunk.base.y;
            require(present[id] == inside, "Trunk BVH query differs from independent exhaustive geometry check");
            expected += inside;
        }
        require(expected == count, "Trunk query count differs from exhaustive geometry check");
        if (count > 1) {
            require(world.nearbyTrunks(feet, height, range, again.data(), 1, overflow) == 1 &&
                    overflow && again[0].id == hits[0].id,
                    "Bounded trunk output must be deterministic and report truncation");
        }
    }
    const float nan = std::numeric_limits<float>::quiet_NaN();
    for (const auto feet : {glm::vec3(nan, 0, 0), glm::vec3(0, nan, 0), glm::vec3(0, 0, nan)})
        require(world.nearbyTrunks(feet, 1.8f, 1, hits.data(), hits.size(), overflow) == 0 && !overflow,
                "Nonfinite trunk-query position must stop safely");
    require(world.nearbyTrunks({0, 0, 0}, -1, 1, hits.data(), hits.size(), overflow) == 0 && !overflow &&
            world.nearbyTrunks({0, 0, 0}, 1.8f, -1, hits.data(), hits.size(), overflow) == 0 && !overflow &&
            world.nearbyTrunks({0, 0, 0}, 1.8f, nan, hits.data(), hits.size(), overflow) == 0 && !overflow &&
            empty.nearbyTrunks({0, 0, 0}, 1.8f, 1, hits.data(), hits.size(), overflow) == 0 && !overflow,
            "Invalid and empty nearby trunk queries must reset output status");
    require(allocations.load() == before, "Nearby trunk queries must not allocate");
}
} // namespace

[[gnu::noinline]] void* operator new(std::size_t size) {
    ++allocations;
    if (void* pointer = std::malloc(size ? size : 1)) return pointer;
    throw std::bad_alloc();
}
[[gnu::noinline]] void* operator new[](std::size_t size) { return ::operator new(size); }
[[gnu::noinline]] void operator delete(void* pointer) noexcept { std::free(pointer); }
[[gnu::noinline]] void operator delete[](void* pointer) noexcept { std::free(pointer); }
[[gnu::noinline]] void operator delete(void* pointer, std::size_t) noexcept { std::free(pointer); }
[[gnu::noinline]] void operator delete[](void* pointer, std::size_t) noexcept { std::free(pointer); }

int main() try {
    nearbyTrunkQueries();
    std::cout << "Indexed nearby-trunk boundary, overflow, exhaustive and allocation checks passed\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
