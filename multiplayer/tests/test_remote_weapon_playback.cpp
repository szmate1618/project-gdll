#include "client/remote_weapon_playback.hpp"
#include "weapons.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
using namespace multiplayer;
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
SoundEvent shot(std::uint64_t id, std::uint32_t player, std::uint8_t slot, std::uint64_t tick = 600) {
    return {id, tick, SoundKind::shot, player, slot, {12, 3, -7}};
}

void eyeMount() {
    RemoteWeaponPlayback playback;
    PlayerState player; player.id = 2; player.slot = 1;
    player.movement.feet = {10, 2, -4}; player.yaw = 0; player.pitch = 30;
    for (const auto stance : {viewer::PlayerStance::Standing, viewer::PlayerStance::Crouched, viewer::PlayerStance::Prone}) {
        player.movement.stance = stance;
        const float height = stance == viewer::PlayerStance::Standing ? 1.7f :
            stance == viewer::PlayerStance::Crouched ? 1.f : .5f;
        const auto pose = playback.sample(player, 0);
        require(bool(pose), "On-foot firearm has no remote weapon mount");
        const auto eye = player.movement.feet + glm::vec3(0, height, 0);
        require(glm::distance(glm::vec3(pose->eyeToWorld[3]), eye) < .00001f,
            "Remote gun used body center or standing eyes for another stance");
        const auto forwardPoint = glm::vec3(pose->eyeToWorld * glm::vec4(0, 0, -2, 1));
        require(glm::distance(forwardPoint, eye + glm::vec3(std::sqrt(3.f), 1, 0)) < .00001f,
            "Remote gun mount did not preserve the player's yaw/pitch and local forward axis");
        require(glm::distance(glm::vec3(pose->eyeToWorld[0]), {0, 0, 1}) < .00001f &&
                std::abs(glm::determinant(glm::mat3(pose->eyeToWorld)) - 1) < .00001f,
            "Remote gun mount reflected or scaled the shared local geometry");
    }
    player.vehicle = 0;
    require(!playback.sample(player, 0), "Seated player retained an on-foot weapon");
    player.vehicle = noVehicle; player.slot = 4;
    require(!playback.sample(player, 0), "Grenade was drawn as a remote firearm");
    player.slot = static_cast<std::uint8_t>(viewer::punchSlot);
    require(!playback.sample(player, 0), "Melee was drawn as a remote firearm");
    player.slot = 0; player.weaponSwitchRemaining = .5f;
    require(!playback.sample(player, 0), "Switching player retained an equipped weapon mesh");
    player.weaponSwitchRemaining = 0; player.reloadRemaining = 1;
    require(bool(playback.sample(player, 0)), "Reloading hid the equipped firearm");
}

void tickTimedFeedback() {
    RemoteWeaponPlayback playback;
    PlayerState player; player.id = 2; player.slot = 1; player.loaded = 30;
    const auto event = shot(1, player.id, player.slot);
    playback.receiveShots({event}, {player}, 10);
    const auto first = playback.sample(player, 10);
    require(first && first->motion.offset.z > .01f && first->flash.color.r > 0,
        "Remote shot event did not start recoil and muzzle light");
    const auto lights = playback.muzzleLights(10);
    require(lights.size() == 1 && lights[0].position == event.position && first->flash.position == event.position,
        "Remote muzzle light lost the authoritative event's world position");
    const auto decayed = playback.sample(player, 10.04);
    playback.receiveShots({event}, {player}, 10.04);
    const auto duplicate = playback.sample(player, 10.04);
    require(decayed && duplicate && decayed->motion.offset == duplicate->motion.offset &&
            decayed->flash.color == duplicate->flash.color && duplicate->flash.color.r < first->flash.color.r,
        "Repeated snapshot event retriggered remote recoil or flash");
    const auto recovered = playback.sample(player, 11);
    require(recovered && recovered->motion.offset == glm::vec3(0) && playback.muzzleLights(11).empty(),
        "Remote firearm effects did not recover on server time");
    require(player.loaded == 30 && player.shots == 0, "Presentation playback changed authoritative weapon state");

    RemoteWeaponPlayback late;
    late.receiveShots({event}, {player}, 10.05);
    const auto aged = late.sample(player, 10.05);
    require(aged && aged->flash.color.r > 0 && aged->flash.color.r < first->flash.color.r * .2f,
        "Late shot restarted its flash at receipt time instead of event time");
    require(late.muzzleLights(10.1).empty(), "Expired shot flash restarted after a late receipt");
}

void sourceAndWeaponIdentity() {
    RemoteWeaponPlayback playback;
    PlayerState player; player.id = 2; player.slot = 1;
    auto reload = shot(1, player.id, 1); reload.kind = SoundKind::reload;
    playback.receiveShots({reload, shot(2, 99, 1), shot(3, player.id, 4)}, {player}, 10);
    require(playback.muzzleLights(10).empty(), "Reload, unknown player or grenade created a muzzle light");
    playback.receiveShots({shot(4, player.id, 0)}, {player}, 10);
    const auto otherSlot = playback.sample(player, 10);
    require(otherSlot && otherSlot->motion.offset == glm::vec3(0) && otherSlot->flash.color == glm::vec3(0),
        "Shot from a previous equipment slot animated a different gun");
    player.slot = 0;
    require(playback.sample(player, 10)->motion.offset.z > 0, "Shot feedback lost its own weapon identity");
    player.vehicle = 0;
    playback.receiveShots({}, {player}, 10);
    require(playback.muzzleLights(10).empty(), "Seated player retained an on-foot muzzle light");
}

void membershipAndFutureEvents() {
    RemoteWeaponPlayback playback;
    std::vector<PlayerState> players(maxPlayers);
    std::vector<SoundEvent> events;
    for (std::size_t i = 0; i < players.size(); ++i) {
        players[i].id = static_cast<std::uint32_t>(i + 2);
        events.push_back(shot(i + 1, players[i].id, 0));
    }
    playback.receiveShots(events, players, 10);
    require(playback.muzzleLights(10).size() == maxPlayers, "Eight remote actors lost distinct muzzle lights");
    playback.receiveShots({}, {players.front()}, 10);
    require(playback.muzzleLights(10).size() == 1, "Departed players retained presentation state");
    playback.receiveShots({}, {}, 10);
    require(playback.muzzleLights(10).empty(), "Disconnected population retained muzzle lights");
    require(playback.sample(players.front(), 10)->motion.offset == glm::vec3(0),
        "Rejoining actor inherited an old recoil pose");
    const auto future = shot(100, players.front().id, 0, 1200);
    playback.receiveShots({future}, {players.front()}, 10);
    require(playback.muzzleLights(10).empty(), "Future event bypassed buffered shot playback");
    playback.receiveShots({future}, {players.front()}, 20);
    require(playback.muzzleLights(20).size() == 1, "Scheduled future event did not play when its tick arrived");
}
} // namespace

int main() {
    try {
        eyeMount(); tickTimedFeedback(); sourceAndWeaponIdentity(); membershipAndFutureEvents();
        std::cout << "Remote weapon mounts, shot clocks, lights and membership passed\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
