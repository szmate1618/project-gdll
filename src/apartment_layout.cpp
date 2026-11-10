#include "apartment_layout.hpp"
#include "house_layout_rules.hpp"
#include "house_polygon.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <numeric>
#include <set>
#include <tuple>

namespace viewer::apartment_layout {
namespace {
using namespace house_layout;
using namespace house_layout::rules;
using Polygon = house_polygon::Polygon;
using Kind = HouseRoom::Kind;
struct Zone { Rectangle bounds; Kind kind; };
struct Cell { Rectangle bounds; Polygon polygon; int owner = -1; };
struct Region { Kind kind; std::vector<std::size_t> cells; Polygon polygon; };
struct Stage { std::vector<Room> rooms; std::vector<Partition> walls; std::size_t core = 0; };
std::optional<std::pair<float,float>> contact(const Cell& a, const Cell& b, int axis) {
    if (a.polygon.empty() || b.polygon.empty()) return std::nullopt;
    const float cut = a.bounds.high[axis];
    const auto interval = [&](const Polygon& polygon) {
        float low=std::numeric_limits<float>::max(), high=-low;
        for (const auto p : polygon) if (std::abs(p[axis]-cut)<.00005f) { low=std::min(low,p[1-axis]); high=std::max(high,p[1-axis]); }
        return std::pair<float,float>{low,high};
    };
    const auto first=interval(a.polygon),second=interval(b.polygon);
    const float low=std::max(first.first,second.first),high=std::min(first.second,second.second);
    return high-low>.001f ? std::optional<std::pair<float,float>>({low,high}) : std::nullopt;
}
bool common(Kind kind) { return kind != Kind::Apartment; }
Rectangle bounds(const Polygon& p) {
    Rectangle r{p.front(), p.front()};
    for (const auto q : p) { r.low = glm::min(r.low, q); r.high = glm::max(r.high, q); }
    return r;
}
Polygon rectangle(Rectangle r) { return {r.low, {r.low.x, r.high.y}, r.high, {r.high.x, r.low.y}}; }
float signedArea(const Polygon& p) {
    double result = 0;
    for (std::size_t i = 0; i < p.size(); ++i) {
        const auto a = p[i] - p[0], b = p[(i + 1) % p.size()] - p[0];
        result += double(a.x) * b.y - double(a.y) * b.x;
    }
    return static_cast<float>(result * .5);
}
// Disjoint tiles share grid cuts, with sub-millimetre projection residues.
std::optional<Polygon> boundary(const std::vector<Cell>& cells, const std::vector<std::size_t>& ids) {
    std::vector<glm::vec2> vertices;
    const auto vertex = [&](glm::vec2 p) {
        for (std::size_t i = 0; i < vertices.size(); ++i) if (glm::distance(vertices[i], p) < .00002f) return i;
        vertices.push_back(p); return vertices.size() - 1;
    };
    std::vector<std::pair<std::size_t, std::size_t>> original;
    for (const auto id : ids) {
        auto p = cells[id].polygon;
        if (signedArea(p) > 0) std::reverse(p.begin(), p.end());
        for (std::size_t i = 0; i < p.size(); ++i) {
            const auto a = vertex(p[i]), b = vertex(p[(i + 1) % p.size()]);
            if (a != b) original.emplace_back(a, b);
        }
    }
    std::set<std::pair<std::size_t, std::size_t>> edges;
    for (const auto& [a, b] : original) {
        const auto delta = vertices[b] - vertices[a];
        const float length = glm::length(delta);
        std::vector<std::pair<float, std::size_t>> cuts{{0,a},{1,b}};
        // Neighboring clipped tiles may retain different collinear vertices.
        // Split their shared grid edges before cancelling the opposite halves.
        if (std::min(std::abs(delta.x),std::abs(delta.y)) < .00002f)
            for (std::size_t i = 0; i < vertices.size(); ++i) {
                if (i == a || i == b) continue;
                const float t = glm::dot(vertices[i]-vertices[a],delta)/(length*length);
                if (t > .00002f/length && t < 1-.00002f/length &&
                    glm::distance(vertices[i],vertices[a]+delta*t) < .00002f) cuts.emplace_back(t,i);
            }
        std::sort(cuts.begin(),cuts.end());
        for (std::size_t i = 1; i < cuts.size(); ++i) {
            const auto first=cuts[i-1].second, second=cuts[i].second;
            if (!edges.erase({second,first}) && !edges.emplace(first,second).second) return std::nullopt;
        }
    }
    std::map<std::size_t,std::size_t> next;
    for (const auto& edge : edges) if (!next.emplace(edge).second) return std::nullopt;
    if (next.empty()) return std::nullopt;
    Polygon p;
    const auto start = next.begin()->first;
    auto current = start;
    do {
        p.push_back(vertices[current]);
        const auto found = next.find(current);
        if (found == next.end()) return std::nullopt;
        current = found->second;
    } while (current != start && p.size() <= edges.size());
    if (current != start || p.size() != edges.size()) return std::nullopt;

    const auto r = bounds(p);
    p = house_polygon::clip(p, r.low, r.high);
    return p.empty() ? std::nullopt : std::optional<Polygon>(std::move(p));
}
// Keep even tiny facade fragments until tiles are joined. The regular polygon
// clipper simplifies sub-millimetre edges, which can give adjacent tiles
// different boundaries and leave a hole when the building is slightly skewed.
Polygon clipTile(const Polygon& shell, Rectangle tile) {
    auto result = shell;
    for (int axis = 0; axis < 2; ++axis) for (const bool upper : {false, true}) {
        const float cut = upper ? tile.high[axis] : tile.low[axis];
        Polygon next;
        const auto inside = [&](glm::vec2 p) { return upper ? p[axis] <= cut : p[axis] >= cut; };
        for (std::size_t i = 0; i < result.size(); ++i) {
            const auto a = result[i], b = result[(i + 1) % result.size()];
            if (inside(a)) next.push_back(a);
            if (inside(a) != inside(b)) {
                const double t = (double(cut) - a[axis]) / (double(b[axis]) - a[axis]);
                glm::vec2 p;
                p[axis] = cut;
                p[1-axis] = static_cast<float>(a[1-axis] + (double(b[1-axis]) - a[1-axis]) * t);
                next.push_back(p);
            }
        }
        result = std::move(next);
    }
    return house_polygon::area(result) > 1e-8f ? result : Polygon{};
}
bool apartment(const Polygon& p, const Polygon& shell) {
    const auto r = bounds(p);
    if (house_polygon::area(p) < 25 || std::min(r.high.x - r.low.x, r.high.y - r.low.y) < 2.999f) return false;
    for (std::size_t i = 0; i < p.size(); ++i) {
        const auto middle = (p[i] + p[(i + 1) % p.size()]) * .5f;
        for (std::size_t j = 0; j < shell.size(); ++j) {
            const auto a = shell[j], edge = shell[(j + 1) % shell.size()] - a;
            const auto nearest = a + edge * glm::clamp(glm::dot(middle - a, edge) / glm::dot(edge, edge), 0.f, 1.f);
            if (glm::distance(middle, nearest) < .002f) return true;
        }
    }
    return false;
}
bool addBays(std::vector<Zone>& zones, Rectangle wing, int axis, Random& random,
             const std::vector<Rectangle>& windows) {
    const int across = 1 - axis;
    if (wing.high[axis] <= wing.low[axis] || wing.high[across] <= wing.low[across]) return true;
    const float length = wing.high[axis] - wing.low[axis], depth = wing.high[across] - wing.low[across];
    const float minimum = std::max(3.f, 25.f / depth);
    const float preferredBay = 6 + random.pick(3001) / 1000.f;
    const int target = std::max(1, int(std::round(length / preferredBay)));
    for (int count = target; count > 0; --count) {
        std::vector<Zone> trial;
        auto remaining = wing;
        bool fits = true;
        for (int i = 1; i < count; ++i) {
            const float preferred = remaining.low[axis] + (wing.high[axis] - remaining.low[axis]) / float(count - i + 1);
            const auto positions = partitionPositions(axis, remaining.low[axis] + minimum,
                wing.high[axis] - minimum * float(count - i), preferred, wing.low[across], wing.high[across], windows);
            if (positions.empty()) { fits = false; break; }
            auto bay = remaining; bay.high[axis] = positions.front(); remaining.low[axis] = positions.front();
            trial.push_back({bay, Kind::Apartment});
        }
        if (!fits) continue;
        trial.push_back({remaining, Kind::Apartment});
        zones.insert(zones.end(), trial.begin(), trial.end()); return true;
    }
    return false;
}
std::optional<Stage> tile(Rectangle footprint, const Polygon& shell, const std::vector<Zone>& zones,
                          const std::vector<Rectangle>& windows, const std::vector<Entrance>& entrances) {
    std::array<std::vector<float>, 2> coordinates;
    for (int axis = 0; axis < 2; ++axis) {
        auto& values = coordinates[axis]; values = {footprint.low[axis], footprint.high[axis]};
        for (const auto& zone : zones) for (const float p : {zone.bounds.low[axis], zone.bounds.high[axis]})
            if (p > footprint.low[axis] && p < footprint.high[axis]) values.push_back(p);
        // Facade vertices stay in the clipped polygons. Adding their almost
        // coincident projections as grid cuts creates unnecessary sliver tiles.
        std::sort(values.begin(), values.end());
        values.erase(std::unique(values.begin(), values.end(), [](float a, float b) { return std::abs(a - b) < .0005f; }), values.end());
    }
    const auto nx = coordinates[0].size() - 1, ny = coordinates[1].size() - 1;
    if (nx * ny > 20000) return std::nullopt;
    std::vector<Cell> cells(nx * ny);
    for (std::size_t x = 0; x < nx; ++x) for (std::size_t y = 0; y < ny; ++y) {
        auto& cell = cells[x * ny + y];
        cell.bounds = {{coordinates[0][x], coordinates[1][y]}, {coordinates[0][x + 1], coordinates[1][y + 1]}};
        cell.polygon = clipTile(shell, cell.bounds);
        if (cell.polygon.empty()) continue;
        const auto center = (cell.bounds.low + cell.bounds.high) * .5f;
        for (std::size_t i = 0; i < zones.size(); ++i) if (contains(zones[i].bounds, center, 0)) { cell.owner = int(i); break; }
        if (cell.owner < 0) return std::nullopt;
    }
    const auto neighbors = [&](std::size_t id) {
        std::vector<std::size_t> result;
        if (id >= ny && contact(cells[id-ny],cells[id],0)) result.push_back(id - ny);
        if (id + ny < cells.size() && contact(cells[id],cells[id+ny],0)) result.push_back(id + ny);
        if (id % ny && contact(cells[id-1],cells[id],1)) result.push_back(id - 1);
        if (id % ny + 1 < ny && contact(cells[id],cells[id+1],1)) result.push_back(id + 1);
        return result;
    };
    std::vector<bool> visited(cells.size());
    std::vector<Region> regions;
    for (std::size_t id = 0; id < cells.size(); ++id) {
        if (cells[id].owner < 0 || visited[id]) continue;
        const int original = cells[id].owner;
        Region region{zones[std::size_t(original)].kind, {}, {}};
        std::vector<std::size_t> pending{id}; visited[id] = true;
        while (!pending.empty()) {
            const auto member = pending.back(); pending.pop_back(); region.cells.push_back(member);
            for (const auto n : neighbors(member)) if (!visited[n] && cells[n].owner == original) { visited[n] = true; pending.push_back(n); }
        }
        auto polygon = boundary(cells, region.cells);
        std::vector<Region> pieces;
        if (polygon) {
            region.polygon = std::move(*polygon);
            pieces.push_back(std::move(region));
        } else {
            // A lobby connector can wrap around the stair core. Room polygons
            // have no holes, so represent that open circulation as simple strips.
            // These shared-space boundaries never create physical walls.
            if (!common(region.kind) || region.kind == Kind::StairArea) return std::nullopt;
            std::set<std::size_t> members(region.cells.begin(), region.cells.end());
            std::map<std::pair<std::size_t, std::size_t>, std::size_t> previous;
            for (std::size_t x = 0; x < nx; ++x) {
                std::map<std::pair<std::size_t, std::size_t>, std::size_t> current;
                for (std::size_t y = 0; y < ny;) {
                    if (!members.count(x * ny + y)) { ++y; continue; }
                    const auto first = y++;
                    while (y < ny && members.count(x * ny + y) && contact(cells[x*ny+y-1], cells[x*ny+y], 1)) ++y;
                    const auto interval = std::make_pair(first, y);
                    const auto found = previous.find(interval);
                    const auto piece = found == previous.end() ? pieces.size() : found->second;
                    if (piece == pieces.size()) pieces.push_back({region.kind, {}, {}});
                    current.emplace(interval, piece);
                    for (auto row = first; row < y; ++row) pieces[piece].cells.push_back(x * ny + row);
                }
                previous = std::move(current);
            }
            for (auto& piece : pieces) {
                auto outline = boundary(cells, piece.cells);
                if (!outline) return std::nullopt;
                piece.polygon = std::move(*outline);
            }
        }
        for (auto& piece : pieces) {
            for (const auto member : piece.cells) cells[member].owner = int(regions.size());
            regions.push_back(std::move(piece));
        }
    }
    // Merge undersized remnants with adjacent apartments. A corner isolated by
    // a ground entrance becomes part of the lobby instead of an unusable flat.
    for (std::size_t pass = 0; pass < regions.size(); ++pass) {
        bool changed = false;
        for (std::size_t i = 0; i < regions.size(); ++i) {
            auto& r = regions[i];
            if (r.kind != Kind::Apartment || r.cells.empty() || apartment(r.polygon, shell)) continue;
            std::set<int> adjacent;
            for (const auto id : r.cells) for (const auto n : neighbors(id))
                if (cells[n].owner >= 0 && cells[n].owner != int(i)) adjacent.insert(cells[n].owner);
            bool merged = false;
            for (const auto other : adjacent) {
                auto& destination = regions[std::size_t(other)];
                if (destination.kind != Kind::Apartment || destination.cells.empty()) continue;
                auto combined = destination.cells; combined.insert(combined.end(), r.cells.begin(), r.cells.end());
                const auto polygon = boundary(cells, combined);
                if (!polygon) continue;
                for (const auto id : r.cells) cells[id].owner = other;
                destination.cells = std::move(combined); destination.polygon = *polygon; r.cells.clear(); merged = true; break;
            }
            if (!merged) {
                if (std::none_of(adjacent.begin(), adjacent.end(), [&](int n) { return common(regions[std::size_t(n)].kind); })) return std::nullopt;
                r.kind = Kind::Lobby;
            }
            changed = true;
        }
        if (!changed) break;
    }
    Stage result;
    std::vector<int> roomForRegion(regions.size(), -1);
    int apartments = 0;
    for (std::size_t i = 0; i < regions.size(); ++i) {
        const auto& r = regions[i]; if (r.cells.empty()) continue;
        if (r.kind == Kind::Apartment) { if (!apartment(r.polygon, shell)) return std::nullopt; ++apartments; }
        roomForRegion[i] = int(result.rooms.size());
        if (r.kind == Kind::StairArea) result.core = result.rooms.size();
        result.rooms.push_back({r.kind, bounds(r.polygon), {}, 0, r.polygon});
    }
    if (apartments < 2) return std::nullopt;
    // Collect shared cell edges, then merge their collinear intervals into the
    // minimum wall spans. Common/common boundaries remain open circulation.
    using Key = std::tuple<int, float, int, int>;
    std::map<Key, std::vector<std::pair<float, float>>> edges;
    for (std::size_t x = 0; x < nx; ++x) for (std::size_t y = 0; y < ny; ++y) {
        const auto id = x * ny + y;
        for (int axis = 0; axis < 2; ++axis) {
            if ((axis == 0 && x + 1 == nx) || (axis == 1 && y + 1 == ny)) continue;
            const auto other = id + (axis == 0 ? ny : 1);
            const int a = cells[id].owner, b = cells[other].owner;
            if (a < 0 || b < 0 || a == b || (common(regions[std::size_t(a)].kind) && common(regions[std::size_t(b)].kind))) continue;
            const auto& r = cells[id].bounds;
            if (const auto span = contact(cells[id],cells[other],axis))
                edges[{axis, r.high[axis], std::min(a, b), std::max(a, b)}].push_back(*span);
        }
    }
    struct Wall { Partition partition; int first, second; };
    std::vector<Wall> walls;
    for (auto& [key, spans] : edges) {
        std::sort(spans.begin(), spans.end());
        std::vector<std::pair<float, float>> merged;
        for (const auto& span : spans) {
            if (!merged.empty() && span.first <= merged.back().second + .0005f) merged.back().second = std::max(merged.back().second, span.second);
            else merged.push_back(span);
        }
        const auto [axis, position, first, second] = key;
        for (const auto& span : merged) {
            if (!clearsWindows(axis, position, span.first, span.second, windows)) return std::nullopt;
            walls.push_back({{axis, position, span.first, span.second, {}}, first, second});
        }
    }
    for (std::size_t owner = 0; owner < regions.size(); ++owner) {
        if (regions[owner].kind != Kind::Apartment || regions[owner].cells.empty()) continue;
        std::vector<std::size_t> options;
        for (std::size_t i = 0; i < walls.size(); ++i) {
            const auto& w = walls[i];
            const int other = w.first == int(owner) ? w.second : w.second == int(owner) ? w.first : -1;
            if (other >= 0 && common(regions[std::size_t(other)].kind) && regions[std::size_t(other)].kind != Kind::StairArea &&
                w.partition.high - w.partition.low >= 1.2f) options.push_back(i);
        }
        std::stable_sort(options.begin(), options.end(), [&](auto a, auto b) {
            return walls[a].partition.high - walls[a].partition.low > walls[b].partition.high - walls[b].partition.low;
        });
        bool fitted = false;
        for (const auto i : options) {
            auto& wall = walls[i].partition;
            const float center = (wall.low + wall.high) * .5f;
            glm::vec2 normal(0);
            const auto sample = point(wall.axis, wall.position + .05f, center);
            normal[wall.axis] = house_polygon::contains(regions[owner].polygon, sample) ? -1.f : 1.f;
            wall.openings = {{center - .45f, center + .45f, normal, std::nullopt, true}};
            const int other = walls[i].first == int(owner) ? walls[i].second : walls[i].first;
            bool approachesFit = true;
            for (const float offset : {-.3f, 0.f, .3f}) {
                const auto doorway = point(wall.axis, wall.position, center + offset);
                approachesFit = approachesFit && house_polygon::contains(regions[owner].polygon, doorway - normal * .35f) &&
                    house_polygon::contains(regions[std::size_t(other)].polygon, doorway + normal * .35f);
            }
            if (approachesFit && openingsFit(wall, shell)) { fitted = true; break; }
            wall.openings.clear();
        }
        if (!fitted) return std::nullopt;
    }
    for (const auto& entrance : entrances) {
        const auto inside = entrance.center - entrance.outward * .3f;
        bool found = false;
        for (std::size_t i = 0; i < regions.size(); ++i) if (!regions[i].cells.empty() && common(regions[i].kind) && house_polygon::contains(regions[i].polygon, inside)) {
            result.rooms[std::size_t(roomForRegion[i])].entrances.push_back(entrance.id); found = true; break;
        }
        if (!found) return std::nullopt;
    }
    // Every shared-space component must connect to the stair core.
    std::set<int> reached;
    std::vector<int> pending;
    for (std::size_t i = 0; i < regions.size(); ++i) if (regions[i].kind == Kind::StairArea && !regions[i].cells.empty()) { pending.push_back(int(i)); reached.insert(int(i)); }
    while (!pending.empty()) {
        const auto r = pending.back(); pending.pop_back();
        for (const auto id : regions[std::size_t(r)].cells) for (const auto n : neighbors(id)) {
            const auto next = cells[n].owner;
            if (next >= 0 && common(regions[std::size_t(next)].kind) && reached.insert(next).second) pending.push_back(next);
        }
    }
    for (std::size_t i = 0; i < regions.size(); ++i) if (!regions[i].cells.empty() && common(regions[i].kind) && !reached.count(int(i))) return std::nullopt;
    for (auto& wall : walls) result.walls.push_back(std::move(wall.partition));
    return result;
}
} // namespace

std::optional<house_layout::Layout> generate(const std::string& building, Rectangle footprint,
    const Polygon& outline, bool slab, float floor, const std::vector<float>& upperFloors,
    const std::vector<Entrance>& entrances, const std::vector<Rectangle>& windows) {
    const auto size = footprint.high - footprint.low;
    const int axis = size.x >= size.y ? 0 : 1, across = 1 - axis;
    const Polygon shell = outline.empty() ? rectangle(footprint) : outline;
    const float middle = (footprint.low[across] + footprint.high[across]) * .5f;
    const auto centers = partitionPositions(across, footprint.low[across] + (slab ? 3.f : 5.75f),
        footprint.high[across] - (slab ? 5.f : 5.75f),
        slab ? middle - 1 : middle, footprint.low[axis], footprint.high[axis], windows);
    for (const float candidate : centers) {
        const float center = slab ? candidate + 1 : candidate;
        Rectangle corridor = footprint, core = footprint;
        core.low[across] = center - 1.75f; core.high[across] = center + 1.75f;
        std::vector<Zone> commonZones;
        if (slab) {
            corridor.low[across] = center - 1; corridor.high[across] = center + 1;
            if (!clearsWindows(across, corridor.high[across], footprint.low[axis], footprint.high[axis], windows)) continue;
            const auto ends = partitionPositions(axis, footprint.low[axis] + 9, footprint.high[axis] - 6,
                footprint.low[axis] + 9, footprint.low[across], footprint.high[across], windows);
            const auto end = std::find_if(ends.begin(), ends.end(), [&](float p) {
                auto trial = core; trial.low[axis] = p - 8; trial.high[axis] = p;
                return house_polygon::covers(shell, trial.low, trial.high);
            });
            if (end == ends.end()) continue;
            core.low[axis] = *end - 8; core.high[axis] = *end;
            corridor.low[axis] = core.high[axis];
        } else {
            const float preferred = (footprint.low[axis] + footprint.high[axis] - 10) * .5f;
            const auto starts = partitionPositions(axis, footprint.low[axis], footprint.high[axis] - 10, preferred,
                footprint.low[across], footprint.high[across], windows);
            auto start = std::find_if(starts.begin(), starts.end(), [&](float p) { return clearsWindows(axis, p + 2, footprint.low[across], footprint.high[across], windows); });
            if (start == starts.end()) continue;
            corridor.low[axis] = *start; corridor.high[axis] = *start + 2;
            core.low[axis] = corridor.high[axis]; core.high[axis] = core.low[axis] + 8;
        }
        if (!house_polygon::covers(shell, core.low, core.high)) continue;
        commonZones.push_back({core, Kind::StairArea});
        commonZones.push_back({corridor, slab ? Kind::Corridor : Kind::Lobby});
        for (const bool high : {false, true}) {
            auto lobby = core;
            lobby.low[across] = high ? core.high[across] : slab ? footprint.low[across] : core.low[across] - 1;
            lobby.high[across] = high ? slab ? footprint.high[across] : core.high[across] + 1 : core.low[across];
            if (slab) lobby.low[axis] = footprint.low[axis];
            commonZones.push_back({lobby, Kind::Lobby});
        }
        if (slab) { auto front = footprint; front.high[axis] = core.low[axis]; commonZones.push_back({front, Kind::Lobby}); }
        const auto makeStage = [&](bool ground) -> std::optional<Stage> {
            auto zones = commonZones;
            if (ground) for (const auto& door : entrances) {
                const auto inside = door.center - door.outward * .3f;
                if (std::any_of(zones.begin(), zones.end(), [&](const Zone& z) { return contains(z.bounds, inside, 0); })) continue;
                const int normal = std::abs(door.outward.x) > std::abs(door.outward.y) ? 0 : 1, along = 1 - normal;
                const auto target = (corridor.low + corridor.high) * .5f;
                const float start = door.outward[normal] < 0 ? footprint.low[normal]-.01f : target[normal]-.01f;
                const float end = door.outward[normal] > 0 ? footprint.high[normal]+.01f : target[normal]+.01f;
                const auto lowCuts = partitionPositions(along, std::max(footprint.low[along], door.center[along] - 3.f), door.center[along] - .75f,
                    door.center[along] - 1, start, end, windows);
                const auto highCuts = partitionPositions(along, door.center[along] + .75f, std::min(footprint.high[along], door.center[along] + 3.f),
                    door.center[along] + 1, start, end, windows);
                if (lowCuts.empty() || highCuts.empty()) return std::nullopt;
                Rectangle spur;
                spur.low[along] = lowCuts.front(); spur.high[along] = highCuts.front();
                // Reach the whole facade, including its skew relative to our
                // axes. Stopping at the door centre leaves thin apartment strips
                // trapped between the lobby and the actual exterior wall.
                spur.low[normal] = start; spur.high[normal] = end;
                zones.push_back({spur, Kind::Lobby});
                // Complete the L-shaped connector for entrances beside a tower core.
                Rectangle link = spur;
                link.low[normal] = target[normal] - .8f; link.high[normal] = target[normal] + .8f;
                link.low[along] = std::min(door.center[along], target[along]) - .8f;
                link.high[along] = std::max(door.center[along], target[along]) + .8f;
                zones.push_back({link, Kind::Lobby});
            }
            Random random(building, ground ? 0x61707467726f756eull : 0x6170747570706572ull);
            if (slab) {
                for (const bool high : {false, true}) {
                    auto wing = footprint; wing.low[axis] = core.high[axis];
                    if (high) wing.low[across] = corridor.high[across]; else wing.high[across] = corridor.low[across];
                    if (!addBays(zones, wing, axis, random, windows)) return std::nullopt;
                }
            } else for (const bool highAlong : {false, true}) for (const bool highAcross : {false, true}) {
                auto sector = footprint;
                if (highAlong) sector.low[axis] = corridor.high[axis]; else sector.high[axis] = corridor.low[axis];
                if (highAcross) sector.low[across] = center; else sector.high[across] = center;
                if (sector.high[axis] > sector.low[axis] && sector.high[across] > sector.low[across]) zones.push_back({sector, Kind::Apartment});
            }
            return tile(footprint, shell, zones, windows, ground ? entrances : std::vector<Entrance>{});
        };
        const auto ground = makeStage(true), upper = upperFloors.empty() ? ground : makeStage(false);
        if (!ground || !upper) continue;
        Layout result; result.wholeGroundFloor = true;
        result.rooms = ground->rooms; result.partitions = ground->walls;
        const auto groundCore = ground->core;
        std::vector<std::size_t> cores{groundCore};
        for (std::size_t level = 1; level <= upperFloors.size(); ++level) {
            const auto offset = result.rooms.size(); cores.push_back(offset + upper->core);
            for (auto room : upper->rooms) { room.storey = level; result.rooms.push_back(std::move(room)); }
            for (auto wall : upper->walls) { wall.storey = level; result.partitions.push_back(std::move(wall)); }
        }
        bool stairsFit = true;
        Random choice(building, 0x6170747374616972ull);
        const bool firstSide = choice.pick(2) != 0;
        for (std::size_t level = 0; level < upperFloors.size(); ++level) {
            const float bottom = level == 0 ? floor + .01f : upperFloors[level - 1], top = upperFloors[level], rise = top - bottom;
            const float run = std::min(rise * 1.75f, 8.f - 1.72f);
            if (run < rise / .98f) { stairsFit = false; break; }
            auto ramp = core; ramp.low[axis] += .86f; ramp.high[axis] = ramp.low[axis] + run;
            const bool side = (level % 2 != 0) != firstSide;
            ramp.low[across] += side ? 1.86f : .06f; ramp.high[across] = ramp.low[across] + 1.1f;
            result.stairs.push_back(makeStair({0, ramp, axis, level % 2 == 0, false}, cores[level], bottom, top));
        }
        if (stairsFit) return result;
    }
    return std::nullopt;
}
} // namespace viewer::apartment_layout
