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
        require(!base.custom(),"A catalog definition must not require per-instance transfer data");
        auto model=base.inventory().model;
        model.parts[0].rotation=glm::angleAxis(.4f,glm::normalize(glm::vec3(1,2,0)));
        const std::vector<viewer::ItemGrip> grips{{{.025f,.35f,-.03f},glm::angleAxis(.7f,glm::vec3(0,1,0))},
            {{0,.7f,.01f},glm::quat(1,0,0,0)}};
        const auto item=base.withCrafting(model,grips).renamed("crafted_broom_1");
        require(item.custom() && item.crafted() && !item.inventory().stackable && item.prototype()=="broom" && item.grips().size()==2,
            "Crafting must retain provenance and both local grip frames as one nonstackable item");
        require(item.grips()[0].position==grips[0].position && std::abs(glm::dot(item.inventory().model.parts[0].rotation,model.parts[0].rotation))>.999999f,
            "Crafting must retain the exact authored grip and rotated part");
        require(item.renamed("transferred").grips().size()==2,"Renaming during transfer discarded grip metadata");
        const auto resized=item.withFootprint(3,4);
        require(resized.inventory().width==3 && resized.inventory().height==4 && item.inventory().width==base.inventory().width &&
                resized.id()==item.id() && resized.prototype()==item.prototype() && resized.tags()==item.tags() && resized.custom() &&
                resized.grips().size()==2 && resized.grips()[0].position==item.grips()[0].position &&
                resized.inventory().model.parts[0].center==item.inventory().model.parts[0].center,
            "Changing an immutable footprint must retain the item's model, provenance, tags and grips");
        rejects([&] { item.withFootprint(17,1); },"Footprints exceeding the inventory format limit must be rejected");
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
        auto legacy=bytes;
        const auto schemaPosition=legacy.find("schema_version = 2");
        require(schemaPosition!=std::string::npos,"New custom saves must explicitly declare schema 2");
        legacy.replace(schemaPosition,std::string("schema_version = 2").size(),"schema_version = 1");
        std::ofstream(directory/"legacy.toml")<<legacy;
        require(viewer::loadCraftedItem(directory/"legacy.toml").grips().size()==2,
            "Existing schema 1 grip definitions must remain readable");
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
        model.mesh={{{-.05f,0,.05f},{0,0,1},{.2f,.3f,.4f}},{{.05f,0,.05f},{0,0,1},{.4f,.5f,.6f}},{{0,.1f,.05f},{0,0,1},{.6f,.7f,.8f}}};
        const auto ungripped=viewer::Item("world_box",generic,{}, {"wood","crafting"}).withModel(model).renamed("imported_box");
        require(ungripped.custom() && !ungripped.crafted() && ungripped.grips().empty() && ungripped.inventory().width==2 &&
                ungripped.inventory().height==2 && ungripped.tags()==std::vector<std::string>{"wood","crafting"} &&
                ungripped.renamed("transferred_box").custom(),
            "An imported model must preserve its footprint and tags without inventing a combat grip");
        const auto ungrippedPath=directory/"ungripped.toml";
        viewer::saveCraftedItem(ungrippedPath,ungripped);
        const auto imported=viewer::loadCraftedItem(ungrippedPath);
        require(imported.custom() && !imported.crafted() && imported.prototype()=="world_box" && imported.grips().empty() &&
                imported.tags()==ungripped.tags() && imported.inventory().model.mesh.size()==3 &&
                imported.inventory().model.mesh[2].position==model.mesh[2].position && imported.inventory().model.mesh[1].color==model.mesh[1].color,
            "Saving an ungripped item must preserve exact visual triangles, provenance, materials and physical parts");
        require(item.withModel(model).grips().size()==2,"Replacing a complete model must retain its existing grips");
        const auto flattened=viewer::Item("world_canopy",generic,{}, {"canopy","wood"}).withModel(model);
        require(!flattened.hasTag("canopy") && flattened.hasTag("wood"),"A flattened custom model must not redeploy its original canopy structure");
        rejects([&] { base.withCrafting(model,{}); },"Authored combat items must retain at least one grip");
        rejects([&] { viewer::captureCraftedItem(base); },"A plain catalog definition must not masquerade as a custom item");
        invalid=viewer::captureCraftedItem(ungripped); invalid.model.mesh.pop_back();
        rejects([&] { viewer::restoreCraftedItem(invalid); },"An incomplete visual triangle was accepted");
        invalid=viewer::captureCraftedItem(ungripped); invalid.model.mesh.resize(4096);
        rejects([&] { viewer::restoreCraftedItem(invalid); },"An unbounded visual mesh was accepted");
        invalid=viewer::captureCraftedItem(ungripped); invalid.model.mesh[0].normal={0,0,0};
        rejects([&] { viewer::restoreCraftedItem(invalid); },"A zero visual normal was accepted");
        invalid=viewer::captureCraftedItem(ungripped); invalid.model.mesh[1].position=invalid.model.mesh[0].position;
        rejects([&] { viewer::restoreCraftedItem(invalid); },"A degenerate visual triangle was accepted");
        std::ofstream(directory/"invalid.toml")<<"schema_version=3\n";
        rejects([&] { viewer::loadCraftedItem(directory/"invalid.toml"); },"An incompatible crafted save schema was accepted");
        std::filesystem::remove_all(directory);
        std::cout<<"Crafted item persistence checks passed\n";
    } catch (const std::exception& error) {
        std::error_code ignored; std::filesystem::remove_all(directory,ignored);
        std::cerr<<error.what()<<'\n'; return 1;
    }
}
