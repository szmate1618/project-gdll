#include "inventory/item_config.hpp"
#include "inventory/item_model_config.hpp"
#include "item_model_geometry.hpp"

#include <array>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
bool close(float a, float b) { return std::abs(a-b) < 1e-6f; }
void closedGeometry(const viewer::Model& model, const viewer::ItemModelConfig& config) {
    require(model.primitives.size() == config.parts.size() && model.draws.size() == config.parts.size(),
        "Each authored part must retain a separate named draw");
    for (std::size_t index = 0; index < model.primitives.size(); ++index) {
        const auto& primitive = model.primitives[index];
        require(model.draws[index].name == config.parts[index].name, "An authored part name was lost");
        using Point = std::array<float, 3>;
        using Edge = std::pair<Point, Point>;
        std::map<Edge, std::pair<int, int>> edges;
        double volume = 0;
        for (std::size_t triangle = 0; triangle < primitive.indices.size(); triangle += 3) {
            const auto& a = primitive.vertices.at(primitive.indices[triangle]);
            const auto& b = primitive.vertices.at(primitive.indices[triangle + 1]);
            const auto& c = primitive.vertices.at(primitive.indices[triangle + 2]);
            const auto cross = glm::cross(b.position-a.position, c.position-a.position);
            require(glm::dot(cross, (a.position+b.position+c.position)/3.f-config.parts[index].center) > 0,
                "A part triangle has inward or degenerate winding");
            require(glm::dot(glm::normalize(cross), a.normal) > .999f && close(glm::length(a.normal),1),
                "A part normal is invalid or disagrees with its triangle");
            volume += glm::dot(glm::dvec3(a.position), glm::cross(glm::dvec3(b.position),glm::dvec3(c.position))) / 6;
            const std::array<glm::vec3,3> points{a.position,b.position,c.position};
            for (std::size_t side=0; side<3; ++side) {
                const auto first=points[side], second=points[(side+1)%3];
                Point from{first.x,first.y,first.z}, to{second.x,second.y,second.z};
                const int direction=from<to ? 1 : -1;
                if (to<from) std::swap(from,to);
                auto& edge=edges[{from,to}]; ++edge.first; edge.second+=direction;
            }
        }
        require(volume>0,"An item part has no outward closed volume");
        for (const auto& [edge, counts] : edges) {
            (void)edge;
            require(counts.first==2 && counts.second==0,"Item model contains an open or inconsistently wound edge");
        }
        for (const auto& vertex : primitive.vertices)
            require(glm::all(glm::greaterThanEqual(vertex.position,model.boundsMin-glm::vec3(1e-7f))) &&
                glm::all(glm::lessThanEqual(vertex.position,model.boundsMax+glm::vec3(1e-7f))),
                "Item model bounds exclude a rendered vertex");
    }
}
void invalidModels() {
    const auto directory=std::filesystem::current_path()/
        ("item-model-fixture-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directory(directory);
    struct Cleanup { std::filesystem::path path; ~Cleanup() { std::filesystem::remove_all(path); } } cleanup{directory};
    const auto file=directory/"item.inventory.toml";
    const std::string valid=R"([model]
mass_kg=0.2
[[model.parts]]
name="body"
shape="box"
material="wood"
center=[0,0,0]
size=[0.1,0.2,0.3]
color=[0.4,0.3,0.2]
)";
    for (const auto& [before,after] : std::array<std::pair<std::string,std::string>,8>{{
        {"mass_kg=0.2","mass_kg=nan"}, {"mass_kg=0.2","mass_kg=0"},
        {"shape=\"box\"","shape=\"sphere\""}, {"size=[0.1,0.2,0.3]","size=[0.1,0,0.3]"},
        {"center=[0,0,0]","center=[0,inf,0]"}, {"color=[0.4,0.3,0.2]","color=[2,0.3,0.2]"},
        {"shape=\"box\"","shape=\"frustum\"\ntop_size=[0.2,-0.1]"},
        {"[[model.parts]]","parts=[]\n[[unused]]"}}}) {
        auto text=valid;
        text.replace(text.find(before),before.size(),after);
        { std::ofstream output(file); output<<text; }
        bool rejected=false;
        try { (void)viewer::loadItemModelConfig(file); }
        catch (const std::runtime_error& error) {
            rejected=true;
            require(std::string(error.what()).find(file.string())!=std::string::npos,"Model validation error omitted its input path");
        }
        require(rejected,"Invalid item geometry was accepted");
    }
}
} // namespace

int main() {
    try {
        const std::filesystem::path items=std::filesystem::path(VIEWER_ITEM_ROOT)/"storage/items";
        for (const auto* id : {"duct_tape","wire_spool","nail","broom","cleaver","knife"}) {
            const auto item=viewer::loadItem(items,id);
            const auto& config=item.inventory().model;
            require(!config.parts.empty() && config.massKg>0,"An authored item model was not loaded");
            const auto model=viewer::buildItemModel(config);
            closedGeometry(model,config);
            require(viewer::itemModelGeometryKey(config)==viewer::itemModelGeometryKey(item.renamed("transferred").inventory().model),
                "Renaming a transferred item must retain its model batch");
            auto recolored=config; recolored.parts[0].color.x+=.01f;
            require(viewer::itemModelGeometryKey(config)!=viewer::itemModelGeometryKey(recolored),"Different material colors must not share a GPU model");
            if (std::string(id)=="nail") {
                require(close(model.boundsMax.y-model.boundsMin.y,.1f),"The nail must be ten centimetres long");
                require(config.parts[0].shape==viewer::ItemModelShape::Frustum,"The nail shaft must retain its tapered rectangular frustum");
            }
            if (std::string(id)=="broom") {
                require(close(model.boundsMax.y-model.boundsMin.y,1.3f),"The broom must retain its authored 1.3 metre height");
                int woodenParts=0;
                for (const auto& part:config.parts) if (part.material=="wood") ++woodenParts;
                require(woodenParts==2,"The broom head and handle must retain separate wood-material parts");
            }
        }
        require(viewer::loadItemModelConfig(items/"support_stake.inventory.toml").parts.empty(),
            "Items without model parts must retain their existing rendering path");
        invalidModels();
        std::cout<<"Item model parsing and geometry checks passed\n";
    } catch (const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
