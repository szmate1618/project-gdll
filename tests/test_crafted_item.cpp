#include "inventory/crafted_item.hpp"
#include "inventory/item_config.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void require(bool value,const char* message) { if (!value) throw std::runtime_error(message); }
template<class Action> void rejects(Action action,const char* message) {
    try { action(); } catch (const std::exception&) { return; }
    throw std::runtime_error(message);
}
}
int main() {
    const auto directory=std::filesystem::temp_directory_path()/
        ("gd-crafted-item-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        const std::filesystem::path root(VIEWER_ITEM_ROOT);
        const auto base=viewer::loadItem(root/"storage/items","broom");
        auto model=base.inventory().model;
        model.parts[0].rotation=glm::angleAxis(.4f,glm::normalize(glm::vec3(1,2,0)));
        const std::vector<viewer::ItemGrip> grips{{{.025f,.35f,-.03f},glm::angleAxis(.7f,glm::vec3(0,1,0))},
            {{0,.7f,.01f},glm::quat(1,0,0,0)}};
        const auto item=base.withCrafting(model,grips).renamed("crafted_broom_1");
        require(item.crafted() && !item.inventory().stackable && item.prototype()=="broom" && item.grips().size()==2,
            "Crafting must retain provenance and both local grip frames as one nonstackable item");
        require(item.grips()[0].position==grips[0].position && std::abs(glm::dot(item.inventory().model.parts[0].rotation,model.parts[0].rotation))>.999999f,
            "Crafting must retain the exact authored grip and rotated part");
        require(item.renamed("transferred").grips().size()==2,"Renaming during transfer discarded grip metadata");
        const auto path=directory/"crafted_broom_1.toml";
        viewer::saveCraftedItem(path,item);
        const auto loaded=viewer::loadCraftedItem(path);
        require(loaded.id()==item.id() && loaded.prototype()==item.prototype() && loaded.grips().size()==2 &&
                glm::distance(loaded.grips()[0].position,grips[0].position)<1e-7f &&
                std::abs(glm::dot(loaded.grips()[0].rotation,grips[0].rotation))>.999999f &&
                std::abs(glm::dot(loaded.inventory().model.parts[0].rotation,model.parts[0].rotation))>.999999f,
            "Durable save/reload discarded the grip or rotated model part");
        require(viewer::craftedItemPrototype(path)=="broom","Saved provenance could not be inspected");
        std::ifstream input(path,std::ios::binary);
        const std::string bytes{std::istreambuf_iterator<char>(input),{}};
        require(bytes.find('\r')==std::string::npos,"Crafted save must use LF on every platform");
        auto updated=grips; updated[0].position.x=.1f;
        viewer::saveCraftedItem(path,base.withCrafting(model,updated).renamed(item.id()));
        require(viewer::loadCraftedItem(path).grips()[0].position.x==.1f,"Atomic replacement retained an obsolete grip");
        for (const auto& entry:std::filesystem::directory_iterator(directory))
            require(entry.path()==path,"Saving left a temporary definition behind");
        auto invalid=viewer::captureCraftedItem(item);
        invalid.grips[0].rotation={0,0,0,0};
        rejects([&] { viewer::restoreCraftedItem(invalid); },"A zero grip orientation was accepted");
        invalid=viewer::captureCraftedItem(item); invalid.grips.resize(9);
        rejects([&] { viewer::restoreCraftedItem(invalid); },"An unbounded number of grips was accepted");
        invalid=viewer::captureCraftedItem(item); invalid.model.parts[0].center.x=std::numeric_limits<float>::quiet_NaN();
        rejects([&] { viewer::restoreCraftedItem(invalid); },"A nonfinite part was accepted");
        invalid=viewer::captureCraftedItem(item); invalid.model.parts[0].size.x=5;
        rejects([&] { viewer::restoreCraftedItem(invalid); },"An oversized model was accepted");
        const auto nail=viewer::loadItem(root/"storage/items","nail").withCrafting(model,grips);
        const auto tape=viewer::loadItem(root/"storage/items","duct_tape").withCrafting(model,grips);
        require(!nail.hasTag("nail") && !tape.hasTag("bind") && tape.hasTag("crafting") && tape.hasTag("melee"),
            "A completed custom assembly must not retain special nail or binding tool behavior");
        viewer::ItemInventoryConfig generic; generic.name="Crate assembly"; generic.type="equipment"; generic.width=2; generic.height=2;
        const auto worldItem=viewer::Item("world_crate",generic,{}, {"wood"}).withCrafting(model,grips).renamed("crafted_crate");
        viewer::saveCraftedItem(directory/"crate.toml",worldItem);
        require(viewer::loadCraftedItem(directory/"crate.toml").prototype()=="world_crate",
            "A generated world object must reload without a catalog definition");
        std::ofstream(directory/"invalid.toml")<<"schema_version=2\n";
        rejects([&] { viewer::loadCraftedItem(directory/"invalid.toml"); },"An incompatible crafted save schema was accepted");
        std::filesystem::remove_all(directory);
        std::cout<<"Crafted item persistence checks passed\n";
    } catch (const std::exception& error) {
        std::error_code ignored; std::filesystem::remove_all(directory,ignored);
        std::cerr<<error.what()<<'\n'; return 1;
    }
}
