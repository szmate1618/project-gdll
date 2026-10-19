#include "weapons.hpp"
#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
void click(viewer::Weapons& weapons) { weapons.trigger(false); weapons.trigger(true); }
void firingAndReload() {
    viewer::Weapons weapons(VIEWER_ITEMS, VIEWER_INVENTORY);
    require(weapons.equipped().config.id == "pistol", "Inventory must select starting weapon");
    const int loaded = weapons.equipped().loaded, reserve = weapons.equipped().reserve;
    click(weapons);
    require(weapons.fire(0), "First press must fire immediately");
    require(!weapons.fire(10), "Holding semi-auto must not fire again");
    click(weapons);
    require(!weapons.fire(.1), "Cooldown must reject fast click");
    require(!weapons.fire(.4), "Rejected semi-auto click must not be queued");
    click(weapons);
    require(weapons.fire(.4), "New press after cooldown must fire");
    require(weapons.equipped().loaded == loaded - 2, "Shots must consume magazine rounds");
    require(weapons.equipped().reserve == reserve, "Shots must not consume reserve directly");
    require(weapons.reload() == 2 && weapons.equipped().loaded == loaded &&
            weapons.equipped().reserve == reserve - 2, "Reload must transfer only missing rounds");
    require(weapons.reload() == 0, "Full magazine must not waste ammo");
    require(!weapons.equip(4), "Invalid slot must not equip");

    weapons.equip(1); click(weapons);
    require(weapons.fire(1), "SMG must fire on press");
    require(!weapons.fire(1.01) && weapons.fire(1.09), "Held SMG must respect cooldown and repeat");
    weapons.trigger(false);
    require(!weapons.fire(2), "Released SMG must stop");
    click(weapons); weapons.cancelTrigger();
    require(!weapons.fire(2), "Cancelled input must stop automatic fire");
    click(weapons); weapons.equip(0); weapons.equip(1);
    require(!weapons.fire(2), "Switching weapons must clear held trigger");
    click(weapons);
    require(!weapons.fire(1.1), "Switching must preserve each weapon's cooldown");
    require(weapons.fire(10), "Held automatic must resume after cooldown");
    int shots = 1;
    for (int i = 0; i < 40; ++i) if (weapons.fire(11 + i)) ++shots;
    require(shots == 28 && weapons.equipped().loaded == 0, "Automatic fire must stop at empty magazine");
    require(!weapons.fire(100), "Empty gun must not fire");
    require(weapons.reload() == 30, "Empty magazine must refill from its own ammo");
    require(!weapons.fire(101), "Reload must require a fresh trigger press");
    weapons.equip(0);
    require(weapons.equipped().reserve == reserve - 2, "SMG reload must not touch pistol ammo");
}

void pelletPattern() {
    viewer::Weapons weapons(VIEWER_ITEMS, VIEWER_INVENTORY);
    weapons.equip(3); click(weapons);
    const int shells = weapons.equipped().loaded;
    require(weapons.fire(0) && weapons.equipped().loaded == shells - 1, "Shotgun must consume one shell, not eight rounds");
    for (const auto forward : {glm::vec3(0,0,-1), glm::vec3(0,1,0), glm::vec3(1,0,0)}) {
        const auto rays = viewer::weaponShotDirections(weapons.equipped().config, forward);
        require(rays.size() == 8 && glm::length(rays[0] - forward) < .0001f, "Shotgun must cast eight rays with centered pellet");
        for (std::size_t i = 1; i < rays.size(); ++i) {
            require(std::abs(glm::length(rays[i]) - 1) < .0001f, "Pellet directions must be normalized");
            const float angle = glm::degrees(std::acos(glm::clamp(glm::dot(rays[i], forward), -1.0f, 1.0f)));
            require(angle > 3.9f && angle < 4.1f, "Peripheral pellets must spread four degrees from aim");
            require(glm::length(rays[i] - rays[(i == 1) ? rays.size() - 1 : i - 1]) > .01f,
                    "Pellets must use distinct directions");
        }
    }
    weapons.equip(2);
    require(weapons.equipped().config.hasScope &&
            viewer::weaponShotDirections(weapons.equipped().config, {0,0,-1}).size() == 1,
            "Sniper must have scope and single ray");
}

void dryFire() {
    using Result = viewer::WeaponFireResult;
    viewer::Weapons weapons(VIEWER_ITEMS, VIEWER_INVENTORY);
    weapons.equip(1);
    click(weapons);
    const auto rounds = weapons.equipped().loaded;
    for (int i = 0; i < rounds; ++i)
        require(weapons.fireAttempt(i) == Result::fired, "Held SMG consumes its magazine normally");
    require(weapons.fireAttempt(rounds - 1 + .01) == Result::none, "Shot cooldown cannot produce an empty click");
    require(weapons.fireAttempt(rounds) == Result::empty, "Held automatic trigger reports empty after its last round");
    require(weapons.fireAttempt(rounds + 1) == Result::none && weapons.fireAttempt(rounds + 2) == Result::none,
            "Holding an empty gun cannot spam clicks every frame");
    click(weapons);
    require(weapons.fireAttempt(rounds + 3) == Result::empty, "A new trigger press allows one more empty click");
    const auto reserve = weapons.equipped().reserve;
    require(weapons.equipped().loaded == 0, "Dry fire cannot consume ammunition");
    require(weapons.reload() == rounds && weapons.equipped().reserve == reserve - rounds, "Reload still transfers rounds normally");
    require(weapons.fireAttempt(rounds + 4) == Result::none, "Reload clears held/empty input");
    click(weapons);
    require(weapons.fireAttempt(rounds + 5) == Result::fired, "A fresh trigger fires after reload");
}

void fixtures(const std::filesystem::path& scratch) {
    std::filesystem::create_directories(scratch / "items");
    for (const auto& entry : std::filesystem::directory_iterator(VIEWER_ITEMS))
        if (entry.path().extension() == ".toml")
            std::filesystem::copy_file(entry.path(), scratch / "items" / entry.path().filename(),
                                       std::filesystem::copy_options::overwrite_existing);
    const auto inventory = scratch / "inventory.toml";
    const auto write = [&](const std::string& text) { std::ofstream(inventory) << text; };
    const std::string base = "equipped = 'pistol'\n[weapons]\npistol = 3\n[ammo]\n"
                             "pistol_rounds = 1\nsmg_rounds = 0\nsniper_rounds = 0\nshotgun_shells = 0\n";
    write(base);
    viewer::Weapons partial(scratch / "items", inventory);
    require(!partial.equip(1), "Inventory omission must remove ownership");
    require(partial.reload() == 1 && partial.equipped().loaded == 4 && partial.equipped().reserve == 0,
            "Partial reserve must give partial reload");
    require(partial.reload() == 0, "Empty reserve must not create ammunition");
    // Configure a two-round shot; parsing must affect real firing and reload.
    const std::string customConfig =
        "name = 'Test pistol'\nammo_type = 'pistol_rounds'\nshot_interval_seconds = 2\n"
        "automatic = false\nhas_scope = false\npellets = 1\nspread_degrees = 0\n"
        "magazine_capacity = 7\nammo_per_shot = 2\nmuzzle_position = [0.01, 0.02, -0.3]\n";
    std::ofstream(scratch / "items/pistol.toml") << customConfig;
    viewer::Weapons doubleShot(scratch / "items", inventory);
    require(glm::distance(doubleShot.equipped().config.muzzlePosition, glm::vec3(.01f, .02f, -.3f)) < 1e-6f,
            "TOML muzzle coordinates must preserve all three model-local axes");
    click(doubleShot);
    require(doubleShot.fire(0) && doubleShot.equipped().loaded == 1, "TOML ammo_per_shot must control consumption");
    click(doubleShot);
    require(!doubleShot.fire(1), "TOML cooldown must control repeat timing");
    click(doubleShot);
    require(!doubleShot.fire(2), "Insufficient magazine ammo must not fire a partial shot");
    require(doubleShot.reload() == 1, "Reload must allow a partially filled magazine");
    click(doubleShot);
    require(doubleShot.fire(2), "Enough ammo after reload must permit firing");
    const auto invalid = [&] {
        try { viewer::Weapons bad(scratch / "items", inventory); }
        catch (const std::exception&) { return; }
        throw std::runtime_error("Invalid inventory was accepted");
    };
    write("equipped = 'missing'\n[weapons]\n[ammo]\n"); invalid();
    write("equipped = 'pistol'\n[weapons]\npistol = 99\n[ammo]\npistol_rounds = 1\n"); invalid();
    auto negative = base; negative.replace(negative.find("pistol_rounds = 1"), 17, "pistol_rounds = -1");
    write(negative); invalid();
    write("this is not valid TOML"); invalid();
    write(base);
    for (const auto& change : std::vector<std::pair<std::string, std::string>>{
             {"shot_interval_seconds = 2", "shot_interval_seconds = nan"},
             {"shot_interval_seconds = 2", "shot_interval_seconds = 0"},
             {"pellets = 1", "pellets = 65"},
             {"spread_degrees = 0", "spread_degrees = 90"},
             {"magazine_capacity = 7", "magazine_capacity = 1"},
             {"ammo_per_shot = 2", "ammo_per_shot = 0"},
             {"automatic = false", "automatic = 'false'"},
             {"muzzle_position = [0.01, 0.02, -0.3]\n", ""},
             {"muzzle_position = [0.01, 0.02, -0.3]", "muzzle_position = [0, 0]"},
             {"muzzle_position = [0.01, 0.02, -0.3]", "muzzle_position = [0, 0, 0, 0]"},
             {"muzzle_position = [0.01, 0.02, -0.3]", "muzzle_position = [0, nan, 0]"},
             {"muzzle_position = [0.01, 0.02, -0.3]", "muzzle_position = [0, 1e99, 0]"},
             {"muzzle_position = [0.01, 0.02, -0.3]", "muzzle_position = [0, 'zero', 0]"},
             {"has_scope = false\n", ""}}) {
        auto badConfig = customConfig;
        badConfig.replace(badConfig.find(change.first), change.first.size(), change.second);
        std::ofstream(scratch / "items/pistol.toml") << badConfig;
        invalid();
    }
}
} // namespace
int main() {
    try {
        firingAndReload(); pelletPattern(); dryFire();
        fixtures(std::filesystem::current_path() / "weapon-test-fixtures");
        std::cout << "Weapon configuration, inventory, firing, reload, and pellet tests passed\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
