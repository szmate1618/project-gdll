#include "weapons.hpp"
#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
void click(viewer::Weapons& weapons) { weapons.trigger(false); weapons.trigger(true); }
void firingAndReload() {
    viewer::Weapons weapons(VIEWER_WEAPON_CONFIGS, VIEWER_INVENTORY);
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
    require(!weapons.equip(5), "Invalid slot must not equip");

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
    viewer::Weapons weapons(VIEWER_WEAPON_CONFIGS, VIEWER_INVENTORY);
    weapons.equip(2); click(weapons);
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
    weapons.equip(3);
    require(weapons.equipped().config.hasScope &&
            viewer::weaponShotDirections(weapons.equipped().config, {0,0,-1}).size() == 1,
            "Sniper must have scope and single ray");
}

void dryFire() {
    using Result = viewer::WeaponFireResult;
    viewer::Weapons weapons(VIEWER_WEAPON_CONFIGS, VIEWER_INVENTORY);
    weapons.equip(1);
    click(weapons);
    const auto rounds = weapons.equipped().loaded;
    for (int i = 0; i < rounds; ++i)
        require(weapons.fireAttempt(i) == Result::fired, "Held SMG consumes its magazine normally");
    require(weapons.fireAttempt(rounds - 1 + .01) == Result::none, "Shot cooldown cannot produce an empty click");
    int callbacks = 0;
    require(weapons.fireAttempt(rounds, [&] { ++callbacks; return true; }) == Result::empty && callbacks == 0,
            "An empty trigger must report empty without starting the physical action");
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

void punchesNeedNoAmmo() {
    viewer::Weapons weapons(VIEWER_WEAPON_CONFIGS, VIEWER_INVENTORY, true);
    viewer::Weapons networkInventory(VIEWER_WEAPON_CONFIGS, VIEWER_INVENTORY);
    require(!networkInventory.equip(viewer::punchSlot), "Multiplayer inventory must not expose unreplicated melee");
    const auto rounds = weapons.equipped().loaded;
    require(weapons.equip(viewer::punchSlot), "Fists must always be owned");
    click(weapons);
    require(weapons.fire(0), "A click must punch without ammunition");
    require(!weapons.fire(1), "Holding must not repeat punches");
    click(weapons);
    require(!weapons.fire(.1) && !weapons.fire(1), "Cooldown clicks must not queue a punch");
    click(weapons);
    require(weapons.fire(1), "A fresh click must punch after cooldown");
    require(weapons.reload() == 0 && weapons.equipped().loaded == 0, "Fists must not reload or consume ammo");
    require(!weapons.discard() && weapons.equipped().owned, "Fists must not become a dropped equippable weapon");
    require(viewer::weaponShotDirections(weapons.equipped().config, {0,0,-1}).empty(), "Fists must never emit hitscan bullets");
    weapons.equip(0);
    require(weapons.equipped().loaded == rounds, "Punching must preserve firearm ammunition");
}

void physicalActionAcceptance() {
    using Result = viewer::WeaponFireResult;
    viewer::Weapons weapons(VIEWER_WEAPON_CONFIGS, VIEWER_INVENTORY, true);
    require(weapons.equip(viewer::punchSlot), "Acceptance fixture must equip fists");
    bool available = false;
    int attempts = 0;
    const auto accept = [&] { ++attempts; return available; };
    require(weapons.fireAttempt(1, accept) == Result::none && attempts == 0,
            "No trigger must not attempt a physical action");
    const auto readyAt = weapons.equipped().readyAt;
    click(weapons);
    require(weapons.fireAttempt(1, accept) == Result::none && attempts == 1 &&
            weapons.equipped().readyAt == readyAt,
            "A rejected physical punch must not spend its cooldown");
    available = true;
    require(weapons.fireAttempt(1, accept) == Result::none && attempts == 1,
            "Holding a rejected punch must not retry when the arm becomes available");
    click(weapons);
    require(weapons.fireAttempt(1, accept) == Result::fired && attempts == 2 &&
            weapons.equipped().readyAt == 1.5,
            "A fresh press after rejection must start immediately and commit one cooldown");
    click(weapons);
    require(weapons.fireAttempt(1.1, accept) == Result::none && attempts == 2 &&
            weapons.fireAttempt(1.5, accept) == Result::none && attempts == 2,
            "Cooldown clicks must neither start nor queue a physical action");

    weapons.equip(0);
    const auto loaded = weapons.equipped().loaded;
    const auto firearmReadyAt = weapons.equipped().readyAt;
    available = false;
    click(weapons);
    require(weapons.fireAttempt(2, accept) == Result::none && attempts == 3 &&
            weapons.equipped().loaded == loaded && weapons.equipped().readyAt == firearmReadyAt,
            "Rejected actions must not consume ammunition or cooldown");
    available = true;
    click(weapons);
    require(weapons.fireAttempt(2, accept) == Result::fired && attempts == 4 &&
            weapons.equipped().loaded == loaded - 1,
            "Accepted actions must still consume ammunition exactly once");
}

void fixtures(const std::filesystem::path& scratch) {
    std::filesystem::create_directories(scratch / "weapons");
    for (const auto* id : viewer::weaponIds) {
        const auto filename = std::string(id) + ".gameplay.toml";
        std::filesystem::copy_file(std::filesystem::path(VIEWER_WEAPON_CONFIGS) / filename, scratch / "weapons" / filename,
                                  std::filesystem::copy_options::overwrite_existing);
    }
    const auto inventory = scratch / "inventory.toml";
    const auto write = [&](const std::string& text) { std::ofstream(inventory) << text; };
    const std::string base = "equipped = 'pistol'\n[weapons]\npistol = 3\n[ammo]\n"
                             "pistol_rounds = 1\nsmg_rounds = 0\nsniper_rounds = 0\nshotgun_shells = 0\nhe_grenades = 0\n";
    write(base);
    viewer::Weapons partial(scratch / "weapons", inventory);
    require(!partial.equip(1), "Inventory omission must remove ownership");
    require(partial.reload() == 1 && partial.equipped().loaded == 4 && partial.equipped().reserve == 0,
            "Partial reserve must give partial reload");
    require(partial.reload() == 0, "Empty reserve must not create ammunition");
    // Configure a two-round shot; parsing must affect real firing and reload.
    const std::string customConfig =
        "name = 'Test pistol'\nammo_type = 'pistol_rounds'\ncaliber = 'high'\nshot_interval_seconds = 2\ndraw_time_seconds = 0.45\n"
        "automatic = false\nhas_scope = false\npellets = 1\nspread_degrees = 0\n"
        "magazine_capacity = 7\nammo_per_shot = 2\nmuzzle_position = [0.01, 0.02, -0.3]\n";
    std::ofstream(scratch / "weapons/pistol.gameplay.toml") << customConfig;
    viewer::Weapons doubleShot(scratch / "weapons", inventory);
    require(doubleShot.equipped().config.caliber == viewer::ShotCaliber::high,
            "TOML caliber must configure the shot damage category");
    require(doubleShot.equipped().config.drawTimeSeconds == .45, "Draw time must come from gameplay TOML");
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
        try { viewer::Weapons bad(scratch / "weapons", inventory); }
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
             {"draw_time_seconds = 0.45\n", ""},
             {"draw_time_seconds = 0.45", "draw_time_seconds = -1"},
             {"draw_time_seconds = 0.45", "draw_time_seconds = nan"},
             {"draw_time_seconds = 0.45", "draw_time_seconds = 'fast'"},
             {"pellets = 1", "pellets = 65"},
             {"spread_degrees = 0", "spread_degrees = 90"},
             {"magazine_capacity = 7", "magazine_capacity = 1"},
             {"ammo_per_shot = 2", "ammo_per_shot = 0"},
             {"automatic = false", "automatic = 'false'"},
             {"caliber = 'high'\n", ""},
             {"caliber = 'high'", "caliber = 'medium'"},
             {"caliber = 'high'", "caliber = 2"},
             {"muzzle_position = [0.01, 0.02, -0.3]\n", ""},
             {"muzzle_position = [0.01, 0.02, -0.3]", "muzzle_position = [0, 0]"},
             {"muzzle_position = [0.01, 0.02, -0.3]", "muzzle_position = [0, 0, 0, 0]"},
             {"muzzle_position = [0.01, 0.02, -0.3]", "muzzle_position = [0, nan, 0]"},
             {"muzzle_position = [0.01, 0.02, -0.3]", "muzzle_position = [0, 1e99, 0]"},
             {"muzzle_position = [0.01, 0.02, -0.3]", "muzzle_position = [0, 'zero', 0]"},
             {"has_scope = false\n", ""}}) {
        auto badConfig = customConfig;
        badConfig.replace(badConfig.find(change.first), change.first.size(), change.second);
        std::ofstream(scratch / "weapons/pistol.gameplay.toml") << badConfig;
        invalid();
    }
}
} // namespace
int main() {
    try {
        firingAndReload(); pelletPattern(); dryFire(); punchesNeedNoAmmo(); physicalActionAcceptance();
        viewer::Weapons grenades(VIEWER_WEAPON_CONFIGS, VIEWER_INVENTORY);
        require(grenades.equip(4) && grenades.equipped().config.grenade.has_value(), "Key 5 must equip HE grenades");
        const auto visibleAt = [&](double seconds) {
            const auto& held = grenades.equipped();
            return viewer::weaponViewVisible(held.config, held.loaded, seconds < held.readyAt);
        };
        require(visibleAt(0), "A loaded, ready grenade must appear in the hand");
        const auto rounds = grenades.equipped().loaded;
        click(grenades);
        require(grenades.fire(0) && grenades.equipped().loaded == rounds - 1, "Throw consumes exactly one grenade");
        const auto readyAt = grenades.equipped().readyAt;
        require(!visibleAt(0) && !visibleAt(readyAt - .001), "The held grenade must remain hidden throughout cooldown");
        grenades.equip(0);
        require(viewer::weaponViewVisible(grenades.equipped().config, 0, true), "Firearm models remain visible while cooling down or empty");
        grenades.equip(4); grenades.reload();
        require(!visibleAt(.1) && visibleAt(readyAt), "Switching/reloading cannot bypass cooldown; its exact end restores the grenade");
        require(!grenades.fire(10) && viewer::weaponShotDirections(grenades.equipped().config, {0, 0, -1}).empty(),
                "Holding throw cannot repeat or create hitscan bullets");
        for (int i = 1; i < rounds; ++i) {
            click(grenades);
            require(grenades.fire(10 + i), "A new click after cooldown throws another grenade");
        }
        require(!visibleAt(100), "After the last grenade, the hand must stay empty beyond cooldown");
        fixtures(std::filesystem::current_path() / "weapon-test-fixtures");
        std::cout << "Weapon configuration, inventory, firing, reload, and pellet tests passed\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
