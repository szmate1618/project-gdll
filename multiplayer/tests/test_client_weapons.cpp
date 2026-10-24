// Run the real graphical client loop against scripted authoritative snapshots.
#define main clientMain
#include "../client/main.cpp"
#undef main
#include <fstream>
#include "player_body.hpp"

namespace {
std::filesystem::path fixture;
multiplayer::PlayerState authoritative;
std::optional<multiplayer::VehicleState> car;
std::vector<std::vector<unsigned char>> frames;
int frame = 0;
bool departed = false;
viewer::PlayerBodyPose punchPose;
constexpr HSteamNetConnection testConnection = 1;
constexpr int frameWidth = 1280, frameHeight = 720;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
std::size_t differences(std::size_t a, std::size_t b) {
    std::size_t count = 0;
    for (std::size_t i = 0; i < frames.at(a).size(); i += 3)
        if (std::abs(int(frames[a][i]) - int(frames[b][i])) > 8 ||
            std::abs(int(frames[a][i+1]) - int(frames[b][i+1])) > 8 ||
            std::abs(int(frames[a][i+2]) - int(frames[b][i+2])) > 8) ++count;
    return count;
}
void capture(const char* name) {
    std::ofstream file(fixture / (std::string(name) + ".ppm"), std::ios::binary);
    file << "P6\n" << frameWidth << ' ' << frameHeight << "\n255\n";
    const auto& pixels = frames.back();
    for (int y = frameHeight - 1; y >= 0; --y)
        file.write(reinterpret_cast<const char*>(pixels.data() + y * frameWidth * 3), frameWidth * 3);
    require(bool(file), "Cannot save multiplayer weapon capture");
}
} // namespace

namespace multiplayer {
const std::filesystem::path& resourceRoot() { return fixture; }
std::filesystem::path weaponModelDirectory() { return VIEWER_WEAPON_ASSETS; }
std::filesystem::path audioDirectory() { return VIEWER_AUDIO_DIR; }
Network::Network(float, float) {}
Network::~Network() = default;
HSteamNetConnection Network::connect(const std::string&) { return testConnection; }
bool Network::send(HSteamNetConnection, const Message& message, bool) {
    if (std::holds_alternative<Goodbye>(message)) departed = true;
    return true;
}
std::vector<std::pair<HSteamNetConnection, Message>> Network::receive() {
    events.clear();
    if (departed) {
        events.push_back({testConnection, k_ESteamNetworkingConnectionState_ClosedByPeer, {}});
        return {};
    }
    if (frame == 0) { ++frame; return {}; } // A connecting client has no weapon yet.
    frames.emplace_back(frameWidth * frameHeight * 3);
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, frameWidth, frameHeight, GL_RGB, GL_UNSIGNED_BYTE, frames.back().data());
    auto* window = glfwGetCurrentContext();
    auto& in = input(window);
    switch (frame) {
        case 2:
            require(differences(0, 1) > 500, "Confirmed pistol did not appear after spawn");
            capture("pistol");
            key(window, GLFW_KEY_2, 0, GLFW_PRESS, 0); // Not confirmed by the server yet.
            break;
        case 3:
            require(differences(1, 2) < 100, "Requested slot replaced the server-confirmed pistol");
            authoritative.slot = 1;
            break;
        case 4: capture("submachine_gun"); authoritative.slot = 2; break;
        case 5: capture("shotgun"); authoritative.slot = 3; break;
        case 6: capture("sniper"); in.interaction = true; break;
        case 7:
            require(differences(0, 6) < 100, "Weapon remained visible in Interaction mode");
            in.interaction = false;
            break;
        case 8:
            require(differences(5, 7) < 100, "Returning to Combat did not restore the equipped sniper");
            authoritative.slot = 3;
            break;
        case 9:
            require(in.canScope, "Confirmed sniper did not enable scope");
            in.capture = true;
            button(window, GLFW_MOUSE_BUTTON_RIGHT, GLFW_PRESS, 0);
            require(in.camera.viewMode() == viewer::Camera::ViewMode::zoom10, "Sniper scope did not activate");
            {
                // The sent aim and the rendered camera must share the sway;
                // the mouse orientation must retain its original values.
                const auto aim = in.weaponMotion.aimCamera(in.camera, in.equippedSlot, 1, true);
                const auto outgoing = command(in, window, false, 1);
                require(outgoing.pitch == aim.pitchDegrees() && outgoing.movement.yawDegrees == aim.yawDegrees(),
                        "Multiplayer sent a different aim direction from the scoped view");
                require(aim.view() != in.camera.view(), "Multiplayer scope has no sway");
            }
            in.interaction = true; // Capture scoped world/reticle without the weapon.
            break;
        case 10: in.interaction = false; break;
        case 11:
            require(differences(9, 10) < 100, "Weapon appeared over the scope view");
            authoritative.slot = 0;
            break;
        case 12:
            require(differences(1, 11) < 100, "Confirmed weapon change did not restore normal pistol view");
            require(!in.canScope, "Unscoped pistol enabled scope");
            in.capture = true;
            button(window, GLFW_MOUSE_BUTTON_RIGHT, GLFW_PRESS, 0);
            require(in.camera.viewMode() == viewer::Camera::ViewMode::original, "Pistol entered scope view");
            authoritative.slot = 4; authoritative.loaded = 2;
            break;
        case 13:
            require(differences(0, 12) > 500, "Ready grenade did not appear in the hand");
            capture("he_grenade");
            authoritative.loaded = 1; authoritative.weaponCoolingDown = true; ++authoritative.shots;
            break;
        case 14:
            require(differences(0, 13) < 100, "Thrown grenade remained in the hand during cooldown");
            capture("grenade-cooldown");
            authoritative.slot = 0;
            break;
        case 15:
            require(differences(1, 14) < 100, "Grenade cooldown hid the pistol after switching weapons");
            authoritative.slot = 4;
            break;
        case 16:
            require(differences(0, 15) < 100, "Re-equipping a cooling grenade restored its held model too soon");
            authoritative.weaponCoolingDown = false;
            break;
        case 17:
            require(differences(12, 16) < 100, "Grenade did not return when the server confirmed cooldown expiry");
            authoritative.loaded = 0;
            break;
        case 18:
            require(differences(0, 17) < 100, "Empty grenade inventory must leave the hand empty");
            authoritative.slot = 1;
            break;
        case 19:
            capture("smg-before-shot");
            ++authoritative.shots;
            break;
        case 20:
            require(differences(18,19) > 500, "Confirmed shot did not animate the held weapon");
            capture("smg-recoil");
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            break;
        case 22:
            require(differences(18,21) < 100, "Unchanged shot snapshots retriggered recoil or prevented recovery");
            capture("smg-recovered");
            break;
        case 23: {
            car.emplace();
            car->chassis.center = authoritative.movement.feet + glm::vec3(0, .8f, 0);
            for (std::size_t w = 0; w < 4; ++w) {
                car->wheels[w].center = car->chassis.center + glm::vec3(w % 2 ? .83f : -.83f, -.32f, w < 2 ? -1.15f : 1.15f);
                car->wheels[w].rotation = glm::angleAxis(-glm::half_pi<float>(), glm::vec3(0, 0, 1));
            }
            authoritative.vehicle = 0; authoritative.seat = 0;
            break;
        }
        case 24: {
            require(in.driver.driving() && !in.driver.thirdPerson() && !in.canScope,
                    "Confirmed driving must enter first person and disable weapon scope");
            in.weaponMotion.fire(in.equippedSlot,0.0);
            const auto seatedAim=command(in,window,false,0.0);
            require(seatedAim.movement.yawDegrees==in.camera.yawDegrees() && seatedAim.pitch==in.camera.pitchDegrees(),
                    "Held-weapon recoil changed seated look controls");
            capture("driving_first_person");
            key(window, GLFW_KEY_LEFT_ALT, 0, GLFW_PRESS, 0);
            break;
        }
        case 25:
            require(in.driver.thirdPerson() && glm::distance(in.camera.position(), car->chassis.center) > 5,
                    "Alt must select the driving chase camera");
            capture("driving_third_person");
            require(differences(23,24) > 500, "Driving camera modes produced the same view");
            key(window, GLFW_KEY_LEFT_ALT, 0, GLFW_PRESS, 0);
            break;
        case 26: {
            require(!in.driver.thirdPerson(), "Alt must return to the driver seat");
            in.capture = true;
            key(window, GLFW_KEY_F12, 0, GLFW_PRESS, 0);
            require(!in.capture && !in.driver.thirdPerson(), "F12 must release the mouse without changing camera mode");
            key(window, GLFW_KEY_F12, 0, GLFW_PRESS, 0);
            key(window, GLFW_KEY_ENTER, 0, GLFW_PRESS, 0);
            require(command(in, window, false, 0.0).enterVehicle && !command(in, window, false, 0.0).enterVehicle,
                    "Enter must queue exactly one seat-transition command");
            authoritative.vehicle = multiplayer::noVehicle; authoritative.seat = multiplayer::noVehicleSeat;
            break;
        }
        case 27:
            require(!in.driver.driving() && !in.driver.thirdPerson(), "Confirmed exit must restore walking camera");
            key(window, GLFW_KEY_LEFT_ALT, 0, GLFW_PRESS, 0);
            require(!in.toggleDrivingCamera, "Alt must have no camera effect on foot");
            key(window, GLFW_KEY_LEFT_ALT, 0, GLFW_PRESS, 0); // Restore master's mouse capture toggle.
            authoritative.vehicle = 0; authoritative.seat = 1;
            break;
        case 28: {
            require(in.driver.occupied() && !in.driver.driving() && in.driver.seat() == 1 && !in.canScope,
                    "Confirmed passenger seat enabled driver controls or scope");
            const auto oldSlot = in.slot;
            key(window, GLFW_KEY_LEFT_ALT, 0, GLFW_PRESS, 0);
            key(window, GLFW_KEY_2, 0, GLFW_PRESS, 0);
            key(window, GLFW_KEY_GRAVE_ACCENT, 0, GLFW_PRESS, 0);
            require(!in.toggleDrivingCamera && in.slot == oldSlot, "Passenger selected chase camera or weapon");
            in.firstMouse = false; in.lastX = in.lastY = 100;
            mouse(window, 160, 80);
            require(std::abs(in.camera.pitchDegrees()) > 0, "Passenger mouse look was blocked");
            capture("passenger");
            break;
        }
        case 29:
            require(in.driver.occupied() && !in.driver.thirdPerson(), "Passenger look changed occupancy or camera mode");
            key(window, GLFW_KEY_ENTER, 0, GLFW_PRESS, 0);
            require(command(in, window, false, 0.0).enterVehicle && !command(in, window, false, 0.0).enterVehicle,
                    "Passenger Enter must request one exit");
            authoritative.vehicle = multiplayer::noVehicle; authoritative.seat = multiplayer::noVehicleSeat;
            break;
        case 30: {
            require(!in.driver.occupied(), "Passenger exit did not restore walking controls");
            const auto oldSlot=in.slot;
            key(window,GLFW_KEY_0,0,GLFW_PRESS,0);
            require(in.slot==oldSlot,"US number-row 0 unexpectedly equipped fists");
            key(window,GLFW_KEY_GRAVE_ACCENT,0,GLFW_PRESS,0);
            require(in.slot==viewer::punchSlot && command(in,window,false,0).slot==viewer::punchSlot,
                    "Physical key left of 1 did not request multiplayer fists");
            in.camera.setOrientation(-90,0); authoritative.yaw=-90; authoritative.pitch=0;
            auto physics=std::make_shared<viewer::PhysicsWorld>(); viewer::PlayerBody body(physics,1.7f);
            viewer::FPSController controller; controller.restoreMovement(authoritative.movement);
            body.follow(controller.eyePosition(),{0,0,-1},true); body.punch({0,0,-1});
            const auto start=body.fistPosition(true);
            for (int n=0;n<20 && glm::distance(start,body.fistPosition(true))<.3f;++n)
                physics->step(1.f/60,[&](float step){body.beforeStep(step);},[&](float){body.afterStep();});
            punchPose=body.pose();
            authoritative.slot=static_cast<std::uint8_t>(viewer::punchSlot);
            authoritative.loaded=authoritative.reserve=0; authoritative.melee=punchPose; ++authoritative.shots;
            break;
        }
        case 31:
            require(!in.canScope,"Confirmed fists enabled scope"); capture("fists-punch");
            authoritative.melee={};
            break;
        case 32:
            require(differences(30,31)>500,"Confirmed physical hands did not render in first person");
            authoritative.melee=punchPose; ++authoritative.shots;
            break;
        case 33:
            require(differences(30,32)<100,"Confirmed punches created firearm recoil or muzzle lighting");
            in.interaction=true;
            break;
        case 34:
            require(differences(31,33)<100,"Interaction mode retained the local physical hands");
            in.interaction=false; key(window,GLFW_KEY_1,0,GLFW_PRESS,0);
            authoritative.slot=0; authoritative.melee={};
            break;
        case 35:
            capture("pistol-after-fists");
            require(differences(31,34)>500,"Equipping a firearm after fists lost its model");
            glfwSetWindowShouldClose(window,true);
            break;
    }
    Snapshot snapshot; snapshot.tick = static_cast<std::uint64_t>(frame++);
    snapshot.players.push_back(authoritative);
    if (car) snapshot.vehicles.push_back(*car);
    if (frame == 2) return {{testConnection, Welcome{1}}, {testConnection, snapshot}};
    return {{testConnection, snapshot}};
}
} // namespace multiplayer

int main(int argc, char** argv) {
    if (!glfwInit()) return 77;
    glfwTerminate();
    try {
        require(argc == 3, "Expected test map and fixture directory");
        fixture = argv[2];
        std::filesystem::create_directories(fixture / "items");
        const std::filesystem::path source(MP_ROOT);
        const auto copy = std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing;
        std::filesystem::copy(source / "items", fixture / "items", copy);
        std::filesystem::copy(source / "vehicles", fixture / "vehicles", copy);
        std::filesystem::copy(source / "shaders", fixture / "shaders", copy);
        std::filesystem::create_directories(fixture / "multiplayer/client");
        for (const auto* shader : {"players.vert", "players.frag"})
            std::filesystem::copy_file(source / "multiplayer/client" / shader, fixture / "multiplayer/client" / shader,
                                       std::filesystem::copy_options::overwrite_existing);
        std::ofstream(fixture / "inventory.toml") <<
            "equipped='pistol'\n[weapons]\npistol=0\nsubmachine_gun=0\nsniper=0\nshotgun=0\nhe_grenade=2\n"
            "[ammo]\npistol_rounds=0\nsmg_rounds=0\nsniper_rounds=0\nshotgun_shells=0\nhe_grenades=0\n";
        multiplayer::Options options; options.map = argv[1]; options.dayNight.dayLengthSeconds = 0;
        options.trees = options.forests = false; options.zombieCount = 0;
        multiplayer::Scene scene(options);
        viewer::FPSController controller;
        require(controller.spawn(scene.spawnEye, *scene.collision), "Cannot spawn weapon test player");
        authoritative.id = 1; authoritative.movement = controller.movementState();
        char program[] = "client", noTrees[] = "--no-trees", count[] = "--zombie-count", zero[] = "0";
        char hidden[] = "--hidden", seconds[] = "--seconds", duration[] = "5";
        char dayLength[] = "--day-length";
        char* args[] = {program, argv[1], noTrees, count, zero, hidden, seconds, duration, dayLength, zero};
        require(clientMain(10, args) == 0 && frame == 36, "Graphical client did not finish all weapon states");
        const std::array<std::size_t, 5> weaponFrames{1, 3, 4, 5, 12};
        for (std::size_t a = 0; a < weaponFrames.size(); ++a)
            for (std::size_t b = a+1; b < weaponFrames.size(); ++b)
                require(differences(weaponFrames[a], weaponFrames[b]) > 500,
                        "Server-confirmed weapon models must produce distinct frames");
        std::cout << "Multiplayer weapons, melee, driving cameras, Enter, Alt and F12 checks passed\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
