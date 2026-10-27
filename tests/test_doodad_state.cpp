#include "doodad_state.hpp"
#include <json.hpp>
#include <chrono>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using Json=nlohmann::json;
void require(bool value,const char* message) {if(!value) throw std::runtime_error(message);}
template<class Function> void rejects(Function action,const char* message) {
    bool rejected=false;
    try {action();} catch(const std::exception&) {rejected=true;}
    require(rejected,message);
}
struct Files {
    std::filesystem::path directory=std::filesystem::path("build")/("doodad-state-test-"+
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::path save=directory/"state.json";
    Files() {std::filesystem::create_directories(directory);}
    ~Files() {std::error_code ignored;std::filesystem::remove_all(directory,ignored);}
    void write(const Json& document) {std::ofstream(save)<<document.dump(2)<<'\n';}
    Json read() {std::ifstream input(save);return Json::parse(input);}
};
void relocation() {
    Files files;
    viewer::DoodadState state("map:forest");
    state.load(files.save); // Missing save is a fresh world.
    viewer::DoodadPlacement moved{0xfedcba9876543210ull,viewer::DoodadKind::BeerBottle,{2,0,3},{1,0,0,0},1};
    state.set(moved);
    const auto revision=state.revision();state.set(moved);
    require(state.revision()==revision,"Unchanged pose dirtied save state");
    moved.position={80,3,-65}; // Outside both original cell and woodland.
    state.set(moved);
    require(state.contains(moved.id),"Moved candidate must suppress its original spawn globally");
    require(state.inCell({0,0}).empty(),"Old destination index retained moved doodad");
    require(state.inCell({5,-5}).size()==1,"Destination cell did not receive moved doodad");
    state.save(files.save);
    viewer::DoodadState loaded("map:forest");loaded.load(files.save);
    const auto loadedRevision=loaded.revision();loaded.load(files.save);
    require(loaded.revision()==loadedRevision,"Identical reload dirtied save state");
    require(loaded.changes().size()==1&&loaded.contains(moved.id),"64-bit ID did not roundtrip exactly");
    const auto restored=loaded.inCell({5,-5});
    require(restored.size()==1&&restored[0].position==moved.position&&restored[0].kind==moved.kind,
            "Moved pose did not survive save/reload");
    moved.position={-1,4,-1};state.set(moved);state.save(files.save);
    loaded.load(files.save);
    require(loaded.inCell({5,-5}).empty()&&loaded.inCell({-1,-1}).size()==1,"Save replacement did not update destination");
}
void validation() {
    Files files;
    viewer::DoodadState state("map:forest");
    viewer::DoodadPlacement p{5,viewer::DoodadKind::Branch,{1,0,1},{1,0,0,0},1};
    state.set(p);state.save(files.save);
    const auto good=files.read();
    auto malformed=good;
    malformed["changes"][0]["rotation_xyzw"]={0,0,0,0};files.write(malformed);
    rejects([&]{state.load(files.save);},"Invalid rotation accepted");
    require(state.contains(5)&&state.changes().size()==1,"Failed load cleared last good state");
    malformed=good;malformed["changes"].push_back(malformed["changes"][0]);files.write(malformed);
    rejects([&]{state.load(files.save);},"Duplicate IDs accepted");
    malformed=good;malformed["identity"]="other map";files.write(malformed);
    rejects([&]{state.load(files.save);},"Mismatched map identity accepted");
    malformed=good;malformed["generation_version"]=2;files.write(malformed);
    rejects([&]{state.load(files.save);},"Mismatched generation version accepted");
    malformed=good;malformed["changes"][0]["id"]="18446744073709551615";files.write(malformed);
    rejects([&]{state.load(files.save);},"Noncanonical ID accepted");
    malformed=good;malformed["changes"][0]["kind"]=std::uint64_t(1)<<32;files.write(malformed);
    rejects([&]{state.load(files.save);},"Overflowing kind accepted");
    p.position.x=std::numeric_limits<float>::infinity();
    rejects([&]{state.set(p);},"Nonfinite position accepted");
    state.save(files.save);
    viewer::DoodadState restored("map:forest");restored.load(files.save);
    require(restored.contains(5),"Failed load damaged later atomic saves");
    require(!std::filesystem::exists(files.save.u8string()+".tmp"),"Temporary save left after publication");
}
}
int main() {
    try {relocation();validation();}
    catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
    std::cout<<"Doodad state checks passed\n";
}
