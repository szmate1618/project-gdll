#include "file_fingerprint.hpp"
#include "zombie_navmesh_paths.hpp"

#include <json.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using Json = nlohmann::json;
using Clock = std::chrono::steady_clock;
using namespace viewer::navmesh_path;

struct Options {
    std::filesystem::path navmesh, replay, output = "build/navmesh/results";
    std::vector<std::size_t> batches{1, 10, 100, 1000};
    std::size_t repeats = 5;
    double heuristicWeight = 2;
    bool reopenClosed = false;
};

std::size_t positiveCount(const std::string& text, const std::string& argument, std::size_t maximum) {
    std::size_t end = 0;
    const auto value = std::stoull(text, &end);
    if (text.empty() || text.front() == '-' || end != text.size() || !value || value > maximum)
        throw std::runtime_error(argument + " must be between 1 and " + std::to_string(maximum));
    return value;
}

double heuristicWeight(const std::string& text) {
    std::size_t end = 0;
    const double value = std::stod(text, &end);
    if (end != text.size() || !std::isfinite(value) || value < 1)
        throw std::runtime_error("--heuristic-weight must be finite and at least 1");
    return value;
}

Options parseOptions(int argc, char** argv) {
    Options result;
    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        auto value = [&]() -> std::string {
            if (++i == argc) throw std::runtime_error("Missing value for " + argument);
            return argv[i];
        };
        if (argument == "--navmesh") result.navmesh = value();
        else if (argument == "--replay") result.replay = value();
        else if (argument == "--output") result.output = value();
        else if (argument == "--repeats") result.repeats = positiveCount(value(), argument, 100);
        else if (argument == "--heuristic-weight") result.heuristicWeight = heuristicWeight(value());
        else if (argument == "--reopen-closed") result.reopenClosed = true;
        else if (argument == "--batches") {
            const auto list = value();
            result.batches.clear();
            for (std::size_t first = 0; first <= list.size();) {
                const auto last = list.find(',', first);
                result.batches.push_back(positiveCount(list.substr(first, last-first), argument, 100000));
                if (last == std::string::npos) break;
                first = last+1;
            }
        } else if (argument == "--help") {
            std::cout << "benchmark_zombie_navmesh --navmesh world.navmesh --replay paths.json\n"
                         "  [--batches 1,10,100,1000] [--repeats 5] [--output DIR]\n"
                         "  [--heuristic-weight 2]  Priority = g + weight * h; finite weight >= 1.\n"
                         "  [--reopen-closed]  Re-expand improved closed triangles; disabled by default.\n"
                         "Reuses recorded endpoints exactly; all searches run independently and serially.\n"
                         "Each batch gets one warm-up followed by the requested measured repeats.\n";
            std::exit(0);
        } else throw std::runtime_error("Unknown argument: " + argument);
    }
    if (result.navmesh.empty() || result.replay.empty())
        throw std::runtime_error("Both --navmesh and --replay are required");
    return result;
}

Point point(const Json& value) {
    if (!value.is_array() || value.size() != 2) throw std::runtime_error("Invalid recorded X/Z point");
    Point result{value[0].get<float>(), value[1].get<float>()};
    if (!std::isfinite(result.x) || !std::isfinite(result.z))
        throw std::runtime_error("Nonfinite recorded endpoint");
    return result;
}
Json pointJson(Point point) { return {point.x, point.z}; }
struct Input { Point start, goal; };
std::vector<Input> readInputs(const Json& document) {
    const auto& batches = document.at("batches");
    if (batches.empty()) throw std::runtime_error("No recorded batches");
    const auto largest = std::max_element(batches.begin(), batches.end(), [](const auto& a, const auto& b) {
        return a.at("routes").size() < b.at("routes").size();
    });
    std::vector<Input> result;
    result.reserve(largest->at("routes").size());
    for (const auto& route : largest->at("routes"))
        result.push_back({point(route.at("start")), point(route.at("goal"))});
    return result;
}

// A separate source-geometry validator, never used to guide or time the search.
// Uniform bins only accelerate its exact slab intersections with padded OBBs.
class GeometryValidator {
    struct Box {
        double x, z, ux, uz, hx, hz;
    };
    static constexpr double binSize = 32;
    double originX_, originZ_;
    int width_, height_;
    std::vector<Box> boxes_;
    std::vector<std::vector<std::uint32_t>> bins_;
    std::vector<std::uint32_t> marks_;
    std::uint32_t epoch_ = 0;

    int column(double x) const { return static_cast<int>(std::floor((x-originX_)/binSize)); }
    int row(double z) const { return static_cast<int>(std::floor((z-originZ_)/binSize)); }
    bool intersects(const Box& box, Point a, Point b) const {
        const double ax = a.x-box.x, az = a.z-box.z;
        const double dx = double(b.x)-a.x, dz = double(b.z)-a.z;
        const double start[2]{ax*box.ux+az*box.uz, -ax*box.uz+az*box.ux};
        const double delta[2]{dx*box.ux+dz*box.uz, -dx*box.uz+dz*box.ux};
        const double half[2]{box.hx, box.hz};
        double low = 0, high = 1;
        for (int axis = 0; axis != 2; ++axis) {
            if (std::abs(delta[axis]) < 1e-15) {
                if (std::abs(start[axis]) > half[axis]) return false;
                continue;
            }
            double near = (-half[axis]-start[axis])/delta[axis];
            double far = (half[axis]-start[axis])/delta[axis];
            if (near > far) std::swap(near, far);
            low = std::max(low, near);
            high = std::min(high, far);
            if (low > high) return false;
        }
        return true;
    }
    bool binClear(int x, int z, Point a, Point b) {
        if (x < 0 || z < 0 || x >= width_ || z >= height_) return true;
        for (const auto index : bins_[static_cast<std::size_t>(z)*width_+x]) {
            if (marks_[index] == epoch_) continue;
            marks_[index] = epoch_;
            if (intersects(boxes_[index], a, b)) return false;
        }
        return true;
    }
public:
    GeometryValidator(const Json& source, const Mesh& mesh)
        : originX_(mesh.minimumX()), originZ_(mesh.minimumZ()),
          width_(static_cast<int>(std::ceil((mesh.maximumX()-mesh.minimumX())/binSize))),
          height_(static_cast<int>(std::ceil((mesh.maximumZ()-mesh.minimumZ())/binSize))),
          bins_(static_cast<std::size_t>(width_)*height_) {
        boxes_.reserve(source.at("obstacles").size());
        for (const auto& item : source.at("obstacles")) {
            if (!item.is_array() || item.size() != 7) throw std::runtime_error("Invalid recorded obstacle");
            const int kind = item[0].get<int>();
            if (kind < 0 || kind > 2) throw std::runtime_error("Invalid recorded obstacle kind");
            double ux = item[3].get<double>(), uz = item[4].get<double>();
            const double length = std::hypot(ux, uz);
            if (!std::isfinite(length) || length < 1e-12) throw std::runtime_error("Invalid obstacle axis");
            ux /= length; uz /= length;
            const double padding = mesh.radius()+mesh.clearance()+(kind == 0 ? mesh.buildingExtra() : 0);
            Box box{item[1].get<double>(), item[2].get<double>(), ux, uz,
                    item[5].get<double>()+padding, item[6].get<double>()+padding};
            const auto index = static_cast<std::uint32_t>(boxes_.size());
            boxes_.push_back(box);
            const double ex = std::abs(ux)*box.hx+std::abs(uz)*box.hz;
            const double ez = std::abs(uz)*box.hx+std::abs(ux)*box.hz;
            const int x0 = std::max(0, column(box.x-ex)), x1 = std::min(width_-1, column(box.x+ex));
            const int z0 = std::max(0, row(box.z-ez)), z1 = std::min(height_-1, row(box.z+ez));
            for (int z = z0; z <= z1; ++z) for (int x = x0; x <= x1; ++x)
                bins_[static_cast<std::size_t>(z)*width_+x].push_back(index);
        }
        marks_.resize(boxes_.size());
    }
    bool clear(Point a, Point b) {
        if (++epoch_ == 0) { std::fill(marks_.begin(), marks_.end(), 0); ++epoch_; }
        int x = column(a.x), z = row(a.z);
        const int endX = column(b.x), endZ = row(b.z);
        const double dx = double(b.x)-a.x, dz = double(b.z)-a.z;
        const int sx = dx > 0 ? 1 : dx < 0 ? -1 : 0, sz = dz > 0 ? 1 : dz < 0 ? -1 : 0;
        const double infinity = std::numeric_limits<double>::infinity();
        const double stepX = sx ? binSize/std::abs(dx) : infinity;
        const double stepZ = sz ? binSize/std::abs(dz) : infinity;
        double nextX = sx ? (originX_+(x+(sx > 0))*binSize-a.x)/dx : infinity;
        double nextZ = sz ? (originZ_+(z+(sz > 0))*binSize-a.z)/dz : infinity;
        // A navmesh turn can land exactly on a bin boundary. Stop by segment
        // parameter as well as cell index: floating rounding at t=1 can step
        // one axis past its endpoint cell before the other axis arrives.
        const int maximumSteps = std::abs(endX-x)+std::abs(endZ-z)+4;
        for (int step = 0; step < maximumSteps; ++step) {
            if (!binClear(x, z, a, b)) return false;
            if (x == endX && z == endZ) return true;
            if (std::min(nextX, nextZ) >= 1) return binClear(endX, endZ, a, b);
            // Include both side cells when crossing a bin corner.
            if (nextX == nextZ) {
                if (!binClear(x+sx, z, a, b) || !binClear(x, z+sz, a, b)) return false;
                x += sx; z += sz; nextX += stepX; nextZ += stepZ;
            } else if (nextX < nextZ) { x += sx; nextX += stepX; }
            else { z += sz; nextZ += stepZ; }
        }
        throw std::runtime_error("Source geometry validator exceeded segment bin traversal bound");
    }
};

Json statsJson(const Stats& stats) {
    return {{"stored_nodes", stats.storedNodes}, {"expanded_nodes", stats.expandedNodes},
            {"peak_queue", stats.peakQueue}, {"corridor_triangles", stats.corridorTriangles},
            {"neighbor_tests", stats.neighborTests}, {"funnel_tests", stats.funnelTests},
            {"location_tests", stats.locationTests}, {"graph_cost_m", stats.graphCost}};
}
struct Totals {
    std::uint64_t stored = 0, expanded = 0, corridor = 0, neighbors = 0, funnel = 0, location = 0;
    std::uint32_t peakQueue = 0;
    std::array<std::size_t, 6> statuses{};
};
struct Batch {
    std::size_t count = 0, failed = 0, waypoints = 0, arenaBytes = 0;
    std::vector<PathId> ids;
    std::vector<Status> statuses;
    std::vector<Stats> stats;
    std::vector<double> milliseconds;
    Totals totals;
};

double median(std::vector<double> samples) {
    std::sort(samples.begin(), samples.end());
    return samples.size()%2 ? samples[samples.size()/2] : .5*(samples[samples.size()/2-1]+samples[samples.size()/2]);
}

void verify(const PathManager& manager, const Mesh& mesh, GeometryValidator& geometry,
            const Input& input, PathId id, bool sourceGeometry) {
    if (!id) return;
    const auto path = manager.view(id);
    if (path.size < 1 || !(path[0] == input.start) || !(path[path.size-1] == input.goal))
        throw std::runtime_error("Successful route omitted or moved an endpoint");
    for (std::size_t i = 1; i < path.size; ++i) {
        if (!mesh.segmentClear(path[i-1], path[i]))
            throw std::runtime_error("Successful route crosses a non-walkable navmesh space");
        if (sourceGeometry && !geometry.clear(path[i-1], path[i]))
            throw std::runtime_error("Successful route intersects an inflated source obstacle");
    }
}

Batch measure(PathManager& manager, const Mesh& mesh, GeometryValidator& geometry,
              const std::vector<Input>& inputs, std::size_t count, std::size_t repeats) {
    Batch result;
    result.count = count;
    result.ids.resize(count); result.statuses.resize(count); result.stats.resize(count);
    result.milliseconds.reserve(repeats);
    for (std::size_t repeat = 0; repeat <= repeats; ++repeat) {
        manager.clear();
        if (manager.usedBytes()) throw std::runtime_error("Path arena was not reclaimed");
        const auto begin = Clock::now();
        for (std::size_t zombie = 0; zombie < count; ++zombie) {
            result.ids[zombie] = manager.request(zombie+1, inputs[zombie].start, inputs[zombie].goal);
            result.statuses[zombie] = manager.lastStatus();
            result.stats[zombie] = manager.lastStats();
        }
        const auto elapsed = std::chrono::duration<double, std::milli>(Clock::now()-begin).count();
        std::cerr << "  " << (repeat ? "Measured repeat " : "Warm-up ") << repeat << ": " << elapsed << " ms\n";
        if (repeat) result.milliseconds.push_back(elapsed);
        for (std::size_t zombie = 0; zombie < count; ++zombie) {
            if (manager.pathFor(zombie+1) != result.ids[zombie])
                throw std::runtime_error("Zombie-to-path mapping is inconsistent");
            if ((result.ids[zombie] != invalidPath) != (result.statuses[zombie] == Status::found))
                throw std::runtime_error("Path ID/status mismatch");
            try {
                verify(manager, mesh, geometry, inputs[zombie], result.ids[zombie], repeat == repeats);
            } catch (const std::exception& error) {
                throw std::runtime_error("Zombie " + std::to_string(zombie+1) + ", repeat " +
                                         std::to_string(repeat) + ": " + error.what());
            }
        }
    }
    for (std::size_t zombie = 0; zombie < count; ++zombie) {
        result.failed += result.ids[zombie] == invalidPath;
        result.waypoints += manager.view(result.ids[zombie]).size;
        ++result.totals.statuses.at(static_cast<std::size_t>(result.statuses[zombie]));
        const auto& stats = result.stats[zombie];
        result.totals.stored += stats.storedNodes;
        result.totals.expanded += stats.expandedNodes;
        result.totals.corridor += stats.corridorTriangles;
        result.totals.neighbors += stats.neighborTests;
        result.totals.funnel += stats.funnelTests;
        result.totals.location += stats.locationTests;
        result.totals.peakQueue = std::max(result.totals.peakQueue, stats.peakQueue);
    }
    result.arenaBytes = manager.usedBytes();
    return result;
}

Json batchJson(const Batch& batch, const PathManager& manager, const Mesh& mesh,
               GeometryValidator& geometry, const std::vector<Input>& inputs) {
    Json counts = Json::object();
    for (std::size_t status = 0; status < batch.totals.statuses.size(); ++status)
        counts[statusName(static_cast<Status>(status))] = batch.totals.statuses[status];
    Json routes = Json::array();
    std::size_t physicallyBlockedRequests = 0, conservativeMeshBlockedRequests = 0;
    for (std::size_t i = 0; i < batch.count; ++i) {
        Json route{{"zombie_id", i+1}, {"path_id", batch.ids[i]}, {"status", statusName(batch.statuses[i])},
                   {"start", pointJson(inputs[i].start)}, {"goal", pointJson(inputs[i].goal)},
                   {"stats", statsJson(batch.stats[i])}};
        route["points"] = Json::array();
        const auto path = manager.view(batch.ids[i]);
        double length = 0;
        for (std::size_t waypoint = 0; waypoint < path.size; ++waypoint) {
            route["points"].push_back(pointJson(path[waypoint]));
            if (waypoint) length += std::hypot(path[waypoint].x-path[waypoint-1].x,
                                               path[waypoint].z-path[waypoint-1].z);
        }
        route["length_m"] = length;
        if (batch.statuses[i] == Status::blockedEndpoint) {
            const bool startMeshBlocked = mesh.locate(inputs[i].start) == noTriangle;
            const bool goalMeshBlocked = mesh.locate(inputs[i].goal) == noTriangle;
            const bool startPhysicalBlocked = !geometry.clear(inputs[i].start, inputs[i].start);
            const bool goalPhysicalBlocked = !geometry.clear(inputs[i].goal, inputs[i].goal);
            route["navmesh_start_blocked"] = startMeshBlocked;
            route["navmesh_goal_blocked"] = goalMeshBlocked;
            route["physical_start_blocked"] = startPhysicalBlocked;
            route["physical_goal_blocked"] = goalPhysicalBlocked;
            if (startPhysicalBlocked || goalPhysicalBlocked) ++physicallyBlockedRequests;
            else ++conservativeMeshBlockedRequests;
        }
        routes.push_back(std::move(route));
    }
    return {{"zombies", batch.count}, {"failed", batch.failed}, {"waypoints", batch.waypoints},
            {"arena_bytes", batch.arenaBytes}, {"median_ms", median(batch.milliseconds)},
            {"min_ms", *std::min_element(batch.milliseconds.begin(), batch.milliseconds.end())},
            {"max_ms", *std::max_element(batch.milliseconds.begin(), batch.milliseconds.end())},
            {"samples_ms", batch.milliseconds}, {"status_counts", std::move(counts)},
            {"physical_blocked_endpoint_requests", physicallyBlockedRequests},
            {"numeric_margin_blocked_endpoint_requests", conservativeMeshBlockedRequests},
            {"stats", {{"stored_nodes", batch.totals.stored}, {"expanded_nodes", batch.totals.expanded},
                       {"corridor_triangles", batch.totals.corridor}, {"neighbor_tests", batch.totals.neighbors},
                       {"funnel_tests", batch.totals.funnel}, {"location_tests", batch.totals.location},
                       {"peak_queue", batch.totals.peakQueue}}}, {"routes", std::move(routes)}};
}

void writeDocument(const std::filesystem::path& destination, const Json& document) {
    auto temporary = destination;
    temporary += ".tmp";
    {
        std::ofstream output(temporary);
        output.exceptions(std::ios::badbit | std::ios::failbit);
        output << document.dump() << '\n';
    }
    std::filesystem::rename(temporary, destination);
}

void writeCsv(std::ostream& out, const Batch& batch, const PathManager& manager) {
    out << std::fixed << std::setprecision(6) << batch.count << ',' << batch.failed << ','
        << median(batch.milliseconds) << ',' << *std::min_element(batch.milliseconds.begin(), batch.milliseconds.end())
        << ',' << *std::max_element(batch.milliseconds.begin(), batch.milliseconds.end()) << ','
        << median(batch.milliseconds)*1000/batch.count << ',' << batch.waypoints << ',' << batch.arenaBytes
        << ',' << manager.capacityBytes() << ',' << batch.totals.stored << ',' << batch.totals.expanded
        << ',' << batch.totals.corridor << ',' << batch.totals.neighbors << ',' << batch.totals.funnel
        << ',' << batch.totals.location << ',' << batch.totals.peakQueue;
    for (const auto count : batch.totals.statuses) out << ',' << count;
    out << '\n';
}
} // namespace

int main(int argc, char** argv) {
    try {
        const auto config = parseOptions(argc, argv);
        const auto inputDirectory = std::filesystem::weakly_canonical(std::filesystem::absolute(config.replay).parent_path());
        const auto outputDirectory = std::filesystem::weakly_canonical(config.output);
        if (inputDirectory == outputDirectory) throw std::runtime_error("Output must differ from the replay directory");
        std::ifstream recorded(config.replay);
        recorded.exceptions(std::ios::badbit | std::ios::failbit);
        Json document = Json::parse(recorded);
        const auto inputs = readInputs(document);
        const auto maximumBatch = *std::max_element(config.batches.begin(), config.batches.end());
        if (maximumBatch > inputs.size()) throw std::runtime_error("Requested batch exceeds recorded input count");
        const auto loadBegin = Clock::now();
        auto mesh = Mesh::load(config.navmesh.string());
        const double meshLoadMs = std::chrono::duration<double, std::milli>(Clock::now()-loadBegin).count();
        const auto fingerprint = viewer::fileSha256(config.replay);
        if (fingerprint != mesh.sourceSha256()) throw std::runtime_error("Navmesh provenance differs from replay source SHA-256");
        const auto setupBegin = Clock::now();
        PathManager manager(mesh, static_cast<std::uint32_t>(std::max<std::size_t>(1000, maximumBatch)),
                            Search::defaultNodeLimit, config.heuristicWeight, config.reopenClosed);
        const double setupMs = std::chrono::duration<double, std::milli>(Clock::now()-setupBegin).count();
        GeometryValidator geometry(document, mesh);
        std::filesystem::create_directories(config.output);
        auto& raster = document.at("raster");
        std::filesystem::copy_file(config.replay.parent_path()/raster.at("file").get<std::string>(),
                                  config.output/"aerial.ppm", std::filesystem::copy_options::overwrite_existing);
        raster["file"] = "aerial.ppm";
        document["algorithm"] = "Navmesh triangle centroid graph weighted A* followed by portal funnel";
        document["heuristic_weight"] = manager.heuristicWeight();
        document["reopen_closed"] = manager.reopenClosed();
        document["closed_triangle_policy"] = manager.reopenClosed()
            ? "Reopen expanded triangles when a lower g is found"
            : "Expand each triangle at most once; ignore later improvements to closed triangles";
        document["priority_formula"] = "g + heuristic_weight * h";
        document["heuristic"] = "Euclidean distance between the current and goal triangle centroids, ignoring obstacles";
        document["replay_source"] = config.replay.string();
        document["replay_sha256"] = fingerprint;
        document["navmesh_source"] = config.navmesh.string();
        document["repeats"] = config.repeats;
        document["warmup_repeats"] = 1;
        document["setup_ms"] = setupMs;
        document["navmesh_load_ms"] = meshLoadMs;
        document["search_node_limit"] = manager.nodeLimit();
        document["search_limit_definition"] = "Maximum distinct stored navmesh triangles per independent request";
        document["arena_capacity_bytes"] = manager.capacityBytes();
        document["navmesh"] = {{"vertices", mesh.vertices().size()}, {"triangles", mesh.triangles().size()},
                              {"minimum", {mesh.minimumX(), mesh.minimumZ()}},
                              {"maximum", {mesh.maximumX(), mesh.maximumZ()}},
                              {"file_bytes", std::filesystem::file_size(config.navmesh)}, {"radius_m", mesh.radius()},
                              {"clearance_m", mesh.clearance()}, {"building_extra_m", mesh.buildingExtra()},
                              {"numeric_margin_m", mesh.numericMargin()}};
        document["memory"] = {{"mesh_bytes", mesh.meshBytes()}, {"spatial_index_bytes", mesh.indexBytes()},
                              {"geometry_cache_bytes", mesh.geometryCacheBytes()},
                              {"search_workspace_bytes", manager.workspaceBytes()},
                              {"path_arena_capacity_bytes", manager.capacityBytes()}, {"manager_metadata_bytes", manager.metadataBytes()},
                              {"core_payload_bytes", mesh.meshBytes()+mesh.indexBytes()+mesh.geometryCacheBytes()+manager.workspaceBytes()+manager.capacityBytes()+manager.metadataBytes()},
                              {"scope", "Allocated navigation buffers; excludes process/benchmark JSON, source geometry validation and aerial raster"}};
        document["timing_scope"] = "Independent serial requests including path allocation, zombie-ID mapping and status/stat collection; includes search-state resets; excludes batch clearing, input loading, validation, JSON and images";
        document["validation"] = {{"navmesh_segments_every_repeat", true}, {"exact_padded_source_geometry_last_repeat", true},
                                  {"endpoint_tolerance_m", 0}, {"input_endpoints_moved", false},
                                  {"source_obb_validation_excludes_bake_numeric_margin", true}};
        document["batches"] = Json::array();
        const std::string header = "zombies,failed,median_ms,min_ms,max_ms,microseconds_per_zombie,waypoints,arena_bytes,"
            "arena_capacity_bytes,stored_nodes,expanded_nodes,corridor_triangles,neighbor_tests,funnel_tests,location_tests,peak_queue,"
            "found,invalid_input,blocked_endpoint,no_path,search_limit,buffer_full";
        std::ofstream csv(config.output/"measurements.csv");
        csv.exceptions(std::ios::badbit | std::ios::failbit);
        csv << header << '\n'; std::cout << header << '\n';
        std::cerr << "Search priority: g + " << manager.heuristicWeight()
                  << " * h (Euclidean centroid distance); " << manager.nodeLimit() << " stored-triangle limit; "
                  << (manager.reopenClosed() ? "reopen closed triangles" : "single expansion per triangle") << '\n';
        for (const auto count : config.batches) {
            std::cerr << "Measuring " << count << " zombies (one warm-up + " << config.repeats << " repeats)\n";
            const auto batch = measure(manager, mesh, geometry, inputs, count, config.repeats);
            document["batches"].push_back(batchJson(batch, manager, mesh, geometry, inputs));
            writeCsv(csv, batch, manager); csv.flush(); writeCsv(std::cout, batch, manager);
            // Preserve a useful complete artifact if a later batch is interrupted.
            writeDocument(config.output/"paths.json", document);
        }
        manager.clear();
        if (manager.usedBytes()) throw std::runtime_error("Path arena did not reclaim every path");
        std::cerr << "All successful endpoints, navmesh segments and inflated source OBBs verified; arena fully reclaimed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "benchmark_zombie_navmesh: " << error.what() << '\n';
        return 1;
    }
}
