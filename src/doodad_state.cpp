#include "doodad_state.hpp"

#include <json.hpp>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace viewer {
namespace {
using Json=nlohmann::json;
bool equal(const DoodadPlacement& a,const DoodadPlacement& b) {
    return a.id==b.id && a.kind==b.kind && a.position==b.position && a.rotation==b.rotation && a.scale==b.scale;
}
void validate(const DoodadPlacement& p) {
    if (static_cast<unsigned>(p.kind)>=doodadKindCount || !std::isfinite(p.scale) || p.scale<=0 || p.scale>10 ||
        !std::isfinite(p.position.x)||!std::isfinite(p.position.y)||!std::isfinite(p.position.z)||
        glm::any(glm::greaterThan(glm::abs(p.position),glm::vec3(1000000))) ||
        !std::isfinite(glm::length(p.rotation)) || std::abs(glm::length(p.rotation)-1)>.001f)
        throw std::runtime_error("Invalid saved doodad pose");
}
std::string idText(std::uint64_t id) {
    std::ostringstream stream; stream<<std::hex<<std::setfill('0')<<std::setw(16)<<id;
    return stream.str();
}
std::uint64_t readId(const Json& value) {
    const auto text=value.get<std::string>();
    if (text.size()!=16 || text.find_first_not_of("0123456789abcdef")!=std::string::npos)
        throw std::runtime_error("Invalid saved doodad ID");
    return std::stoull(text,nullptr,16);
}
float number(const Json& value) {
    if (!value.is_number()) throw std::runtime_error("Expected numeric doodad coordinate");
    const double result=value.get<double>();
    if (!std::isfinite(result)) throw std::runtime_error("Nonfinite doodad coordinate");
    return static_cast<float>(result);
}
}

DoodadState::DoodadState(std::string identity) : identity_(std::move(identity)) {
    if (identity_.empty()) throw std::invalid_argument("Doodad state requires map identity");
}
bool DoodadState::contains(std::uint64_t id) const { return changes_.find(id)!=changes_.end(); }
void DoodadState::set(const DoodadPlacement& placement) {
    validate(placement);
    const auto cell=doodadCellAt(placement.position);
    const auto previous=changes_.find(placement.id);
    if (previous!=changes_.end()) {
        if (equal(previous->second,placement)) return;
        const auto oldCell=doodadCellAt(previous->second.position);
        auto old=cells_.find(oldCell);
        old->second.erase(placement.id);
        if (old->second.empty()) cells_.erase(old);
    }
    changes_[placement.id]=placement;
    cells_[cell].insert(placement.id);
    ++revision_;
}
std::vector<DoodadPlacement> DoodadState::inCell(DoodadCell cell) const {
    std::vector<DoodadPlacement> result;
    const auto found=cells_.find(cell);
    if (found!=cells_.end()) for (const auto id:found->second) result.push_back(changes_.at(id));
    return result;
}

void DoodadState::load(const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input) {
        if (!std::filesystem::exists(path)) return;
        throw std::runtime_error("Cannot read doodad save: "+path.u8string());
    }
    const auto document=Json::parse(input);
    if (document.at("schema")!="godollo.doodad_state" || document.at("version")!=1 ||
        document.at("generation_version")!=1 || document.at("seed")!=doodadGenerationSeed ||
        document.at("cell_size_m")!=doodadCellSize || document.at("identity")!=identity_)
        throw std::runtime_error("Doodad save identity or generation version mismatch: "+path.u8string());
    const auto& changes=document.at("changes");
    if (!changes.is_array() || changes.size()>1000000) throw std::runtime_error("Invalid saved doodad changes");
    DoodadState loaded(identity_);
    for (const auto& record:changes) {
        const auto& position=record.at("position");
        const auto& rotation=record.at("rotation_xyzw");
        const auto& kind=record.at("kind");
        if (!position.is_array() || position.size()!=3 || !rotation.is_array() || rotation.size()!=4 ||
            !kind.is_number_integer() || kind.get<double>()<0 || kind.get<double>()>=double(doodadKindCount))
            throw std::runtime_error("Malformed saved doodad pose");
        DoodadPlacement placement;
        placement.id=readId(record.at("id"));
        placement.kind=static_cast<DoodadKind>(kind.get<int>());
        placement.position={number(position[0]),number(position[1]),number(position[2])};
        placement.rotation={number(rotation[3]),number(rotation[0]),number(rotation[1]),number(rotation[2])};
        placement.scale=number(record.at("scale"));
        if (loaded.contains(placement.id)) throw std::runtime_error("Duplicate saved doodad ID");
        loaded.set(placement);
    }
    bool changed=changes_.size()!=loaded.changes_.size();
    if (!changed) {
        auto current=changes_.begin();
        for (const auto& entry:loaded.changes_) {
            if (current->first!=entry.first || !equal(current->second,entry.second)) {changed=true;break;}
            ++current;
        }
    }
    if (!changed) return;
    changes_.swap(loaded.changes_);
    cells_.swap(loaded.cells_);
    ++revision_;
}

void DoodadState::save(const std::filesystem::path& path) const {
    Json records=Json::array();
    for (const auto& entry:changes_) {
        const auto& p=entry.second;
        validate(p);
        records.push_back({{"id",idText(p.id)},{"kind",static_cast<int>(p.kind)},
            {"position",{p.position.x,p.position.y,p.position.z}},
            {"rotation_xyzw",{p.rotation.x,p.rotation.y,p.rotation.z,p.rotation.w}},{"scale",p.scale}});
    }
    const Json document={{"schema","godollo.doodad_state"},{"version",1},{"generation_version",1},
        {"seed",doodadGenerationSeed},{"cell_size_m",doodadCellSize},{"identity",identity_},{"changes",std::move(records)}};
    if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
    const auto temporary=std::filesystem::path(path.u8string()+".tmp");
    {
        std::ofstream output(temporary,std::ios::binary|std::ios::trunc);
        if (!output || !(output<<document.dump(2)<<'\n') || !output.flush())
            throw std::runtime_error("Cannot write doodad save: "+temporary.u8string());
    }
    DoodadState verified(identity_);
    verified.load(temporary);
    std::filesystem::rename(temporary,path);
}
} // namespace viewer
