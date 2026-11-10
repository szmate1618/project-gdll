#include "crafted_item.hpp"
#include <toml++/toml.hpp>
#include <chrono>
#include <cmath>
#include <fstream>
#include <set>
#include <stdexcept>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace viewer {
namespace {
bool identifier(const std::string& value,std::size_t maximum) {
    if (value.empty() || value.size()>maximum) return false;
    for (const unsigned char c:value)
        if (!(c>='a' && c<='z') && !(c>='A' && c<='Z') && !(c>='0' && c<='9') && c!='_' && c!='-') return false;
    return true;
}
std::string text(const toml::table& table,const char* field) {
    const auto value=table[field].value<std::string>();
    if (!value) throw std::runtime_error(std::string("Missing crafted item ")+field);
    return *value;
}
template<int Count> glm::vec<Count,float,glm::defaultp> vector(const toml::table& table,const char* field) {
    const auto* array=table[field].as_array();
    if (!array || array->size()!=Count) throw std::runtime_error(std::string("Invalid crafted item vector ")+field);
    glm::vec<Count,float,glm::defaultp> value{};
    for (int i=0;i<Count;++i) {
        const auto number=(*array)[static_cast<std::size_t>(i)].value<double>();
        if (!number || !std::isfinite(*number) || std::abs(*number)>1e6) throw std::runtime_error("Invalid crafted item coordinate");
        value[i]=static_cast<float>(*number);
    }
    return value;
}
glm::quat quaternion(const toml::table& table,const char* field) {
    const auto value=vector<4>(table,field);
    return {value.x,value.y,value.z,value.w}; // File order is w,x,y,z.
}
template<int Count> toml::array array(glm::vec<Count,float,glm::defaultp> value) {
    toml::array result;
    for (int i=0;i<Count;++i) result.push_back(static_cast<double>(value[i]));
    return result;
}
toml::array array(glm::quat value) { return toml::array{double(value.w),double(value.x),double(value.y),double(value.z)}; }
CraftedItemData read(const std::filesystem::path& path) {
    if (std::filesystem::file_size(path)>2*1024*1024) throw std::runtime_error("Crafted item file is too large");
    const auto table=toml::parse_file(path.string());
    const auto schema=table["schema_version"].value_or(0);
    if (schema!=1 && schema!=2) throw std::runtime_error("Unsupported crafted item schema");
    CraftedItemData data;
    data.id=text(table,"id"); data.prototype=text(table,"prototype"); data.name=text(table,"name"); data.acoustics=text(table,"acoustics");
    data.width=table["width"].value_or(0); data.height=table["height"].value_or(0);
    const auto* tags=table["tags"].as_array();
    if (!tags || tags->size()>32) throw std::runtime_error("Invalid crafted item tags");
    for (const auto& node:*tags) {
        const auto tag=node.value<std::string>();
        if (!tag) throw std::runtime_error("Invalid crafted item tag");
        data.tags.push_back(*tag);
    }
    const auto* model=table["model"].as_table();
    const auto* parts=model ? (*model)["parts"].as_array() : nullptr;
    if (!model || !parts || parts->empty() || parts->size()>64) throw std::runtime_error("Invalid crafted item parts");
    data.model.massKg=static_cast<float>((*model)["mass_kg"].value_or(-1.0));
    for (const auto& node:*parts) {
        const auto* entry=node.as_table();
        if (!entry) throw std::runtime_error("Invalid crafted item part");
        ItemModelPart part;
        part.name=text(*entry,"name"); part.material=text(*entry,"material");
        const auto shape=text(*entry,"shape");
        if (shape!="box" && shape!="frustum") throw std::runtime_error("Unknown crafted item shape");
        part.shape=shape=="box" ? ItemModelShape::Box : ItemModelShape::Frustum;
        part.center=vector<3>(*entry,"center"); part.size=vector<3>(*entry,"size"); part.color=vector<3>(*entry,"color");
        part.topSize=vector<2>(*entry,"top_size"); part.rotation=quaternion(*entry,"rotation");
        data.model.parts.push_back(std::move(part));
    }
    if (model->contains("mesh")) {
        const auto* mesh=(*model)["mesh"].as_array();
        if (!mesh || mesh->size()>4095 || mesh->size()%3 || (schema==1 && !mesh->empty()))
            throw std::runtime_error("Invalid custom item mesh");
        for (const auto& node:*mesh) {
            const auto* entry=node.as_table();
            if (!entry) throw std::runtime_error("Invalid custom item vertex");
            data.model.mesh.push_back({vector<3>(*entry,"position"),vector<3>(*entry,"normal"),vector<3>(*entry,"color")});
        }
    }
    const auto* grips=table["grips"].as_array();
    if (!grips || (schema==1 && grips->empty()) || grips->size()>8) throw std::runtime_error("Invalid crafted item grips");
    for (const auto& node:*grips) {
        const auto* entry=node.as_table();
        if (!entry) throw std::runtime_error("Invalid crafted item grip");
        data.grips.push_back({vector<3>(*entry,"position"),quaternion(*entry,"rotation")});
    }
    return data;
}
}

CraftedItemData captureCraftedItem(const Item& item) {
    if (!item.custom()) throw std::invalid_argument("Item has no complete custom definition");
    const auto& inventory=item.inventory();
    CraftedItemData data{item.id(),item.prototype(),inventory.name,inventory.acoustics,inventory.width,inventory.height,
        item.tags(),inventory.model,item.grips()};
    (void)restoreCraftedItem(data);
    return data;
}
Item restoreCraftedItem(const CraftedItemData& data) {
    if (!identifier(data.id,128) || !identifier(data.prototype,128) || data.name.empty() || data.name.size()>128 ||
        data.name.find('\0')!=std::string::npos || (!data.acoustics.empty() && !identifier(data.acoustics,64)) || data.tags.size()>32)
        throw std::invalid_argument("Invalid crafted item metadata");
    std::set<std::string> unique;
    for (const auto& tag:data.tags)
        if (!identifier(tag,64) || !unique.insert(tag).second) throw std::invalid_argument("Invalid crafted item tags");
    ItemInventoryConfig inventory;
    inventory.name=data.name; inventory.type="equipment"; inventory.width=data.width; inventory.height=data.height;
    inventory.acoustics=data.acoustics;
    auto item=Item(data.prototype,std::move(inventory),WeaponConfig{},data.tags);
    return (data.grips.empty() ? item.withModel(data.model) : item.withCrafting(data.model,data.grips)).renamed(data.id);
}
void saveCraftedItem(const std::filesystem::path& path,const Item& item) {
    const auto data=captureCraftedItem(item);
    toml::array tags,parts,mesh,grips;
    for (const auto& tag:data.tags) tags.push_back(tag);
    for (const auto& part:data.model.parts)
        parts.push_back(toml::table{{"name",part.name},{"shape",part.shape==ItemModelShape::Box ? "box" : "frustum"},
            {"material",part.material},{"center",array(part.center)},{"size",array(part.size)},{"color",array(part.color)},
            {"top_size",array(part.topSize)},{"rotation",array(part.rotation)}});
    for (const auto& vertex:data.model.mesh)
        mesh.push_back(toml::table{{"position",array(vertex.position)},{"normal",array(vertex.normal)},{"color",array(vertex.color)}});
    for (const auto& grip:data.grips) grips.push_back(toml::table{{"position",array(grip.position)},{"rotation",array(grip.rotation)}});
    toml::table table{{"schema_version",2},{"id",data.id},{"prototype",data.prototype},{"name",data.name},{"acoustics",data.acoustics},
        {"width",data.width},{"height",data.height},{"tags",std::move(tags)},
        {"model",toml::table{{"mass_kg",double(data.model.massKg)},{"parts",std::move(parts)},{"mesh",std::move(mesh)}}},{"grips",std::move(grips)}};
    if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
    auto temporary=path;
    temporary += "."+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".tmp";
    try {
        {
            std::ofstream output(temporary,std::ios::binary|std::ios::trunc);
            output<<toml::toml_formatter{table}<<'\n'; output.flush();
            if (!output) throw std::runtime_error("Cannot write crafted item");
        }
        (void)loadCraftedItem(temporary);
#ifdef _WIN32
        if (!MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)) throw std::runtime_error("Cannot replace crafted item");
#else
        std::filesystem::rename(temporary,path);
#endif
    } catch (...) {
        std::error_code ignored; std::filesystem::remove(temporary,ignored); throw;
    }
}
Item loadCraftedItem(const std::filesystem::path& path) { return restoreCraftedItem(read(path)); }
std::string craftedItemPrototype(const std::filesystem::path& path) { return loadCraftedItem(path).prototype(); }
}
