#include "inventory_state.hpp"
#include "canopy.hpp"
#include "inventory/inventory_config.hpp"
#include <iostream>
#include <stdexcept>

namespace { void require(bool ok,const char* message) { if (!ok) throw std::runtime_error(message); } }
int main() {
    try {
        const auto catalog=viewer::loadPlayerInventory(MP_ROOT);
        auto world=std::make_shared<viewer::PhysicsWorld>();
        viewer::GroundItems source(world);
        source.add(*catalog.find("canopy"),0,{}, {0,2,0});
        source.add(*catalog.find("support_stake"),0,{}, {0,1,2});
        const auto original=source.poses();
        multiplayer::Snapshot snapshot; snapshot.tick=1;
        snapshot.groundItems=multiplayer::captureGround(source,{0,0,0});
        const auto bytes=multiplayer::encode(snapshot);
        const auto decoded=std::get<multiplayer::Snapshot>(multiplayer::decode(bytes.data(),bytes.size()));
        require(decoded.groundItems.size()==2 && decoded.groundItems[0].panels.size()==36,
                "Snapshot must carry the canopy's complete panel set and spare stake");
        viewer::GroundItems replica(std::make_shared<viewer::PhysicsWorld>(),true);
        multiplayer::applyGround(replica,catalog,decoded.groundItems);
        const auto poses=replica.poses();
        for (int p=0; p<36; ++p)
            require(glm::distance(poses[0].panels[p].center,original[0].panels[p].center)<1e-6f,
                    "Replica changed an authoritative canopy panel");
        const auto inventory=multiplayer::captureInventory(catalog);
        auto copy=catalog;
        require(multiplayer::applyInventory(copy,catalog,inventory) && copy.owns("canopy") && copy.owns("support_stake"),
                "General shelter equipment must survive inventory replication");
        auto invalid=snapshot; invalid.groundItems[0].panels.pop_back();
        bool rejected=false;
        try { multiplayer::encode(invalid); } catch (const std::exception&) { rejected=true; }
        require(rejected,"Partial canopy snapshots must be rejected");
        invalid=snapshot; invalid.groundItems[1].panels=invalid.groundItems[0].panels;
        rejected=false;
        try { multiplayer::applyGround(replica,catalog,invalid.groundItems); } catch (const std::exception&) { rejected=true; }
        require(rejected && replica.count()==2,"Panel data on a stake must be rejected without replacing replica state");
        multiplayer::applyGround(replica,catalog,{});
        require(replica.count()==0,"Replica removal must delete the whole canopy");
        std::cout<<"Multiplayer canopy checks passed\n";
    } catch (const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
