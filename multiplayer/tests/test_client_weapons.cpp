// Run the real graphical client loop against scripted authoritative snapshots.
#include "../client/session.cpp"
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
viewer::FPSState onFootAfterExit;
glm::vec3 punchEye{0};
constexpr HSteamNetConnection testConnection = 1;
constexpr int frameWidth = 1280;
int frameHeight = 720;
bool scaledDisplay = false;
std::chrono::steady_clock::time_point sessionOrigin;
std::uint64_t latestCommand = 0, localFireCommand = 0, shotsBeforeLocalFire = 0;
double localFireSeconds = 0;
std::uint8_t localFireSlot = 1;
bool holdFireAcknowledgement = false;

double sessionSeconds() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - sessionOrigin).count();
}

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
std::size_t differences(std::size_t a, std::size_t b) {
    std::size_t count = 0;
    // Compare the world/held-weapon area. The HUD intentionally differs between
    // connection, equipment, cooldown, interaction and vehicle states.
    for (int y = 0; y < frameHeight; ++y)
        for (int x = 0; x < frameWidth; ++x) {
            const bool hud = (x > 410 && x < 870 && y > 630) || (x > 1000 && y > 550) ||
                (x < 310 && y < 145) || (x > 425 && x < 910 && y < 95) || (x > 1030 && y < 115) ||
                (x > 310 && x < 970 && y > 95 && y < 135);
            if (hud) continue;
            const auto i = static_cast<std::size_t>(y * frameWidth + x) * 3;
            if (std::abs(int(frames[a][i]) - int(frames[b][i])) > 8 ||
                std::abs(int(frames[a][i+1]) - int(frames[b][i+1])) > 8 ||
                std::abs(int(frames[a][i+2]) - int(frames[b][i+2])) > 8) ++count;
        }
    return count;
}
std::size_t brightenedWorldPixels(std::size_t before, std::size_t after) {
    std::size_t count = 0;
    // The fixture's nearby pillar, above the held weapon and away from the HUD.
    // Recoil does not move the hip camera; illumination here proves that the
    // muzzle light reached scene rendering, independently of weapon motion.
    for (int y = frameHeight / 2; y < frameHeight * 13 / 20; ++y)
        for (int x = frameWidth * 13 / 20; x < frameWidth * 3 / 4; ++x) {
            const auto i = static_cast<std::size_t>(y * frameWidth + x) * 3;
            if (int(frames[after][i]) - int(frames[before][i]) > 12 &&
                int(frames[after][i+1]) - int(frames[before][i+1]) > 6) ++count;
        }
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
// The frame script begins after discovery; transport startup has its own integration test.
std::optional<Options> discoverSession(const Options& local, const SessionFeedback&, const std::function<void()>&) { return local; }
Network::~Network() = default;
HSteamNetConnection Network::connect(const std::string&) {
    sessionOrigin = std::chrono::steady_clock::now();
    return testConnection;
}
double Network::estimatedOneWaySeconds(HSteamNetConnection) const { return 0; }
bool Network::send(HSteamNetConnection, const Message& message, bool) {
    if (std::holds_alternative<Goodbye>(message)) departed = true;
    if (const auto* commands = std::get_if<Commands>(&message)) {
        for (const auto& command : commands->commands) {
            latestCommand = std::max(latestCommand, command.sequence);
            if (holdFireAcknowledgement && !localFireCommand && command.left && command.slot == localFireSlot) {
                localFireCommand = command.sequence;
                localFireSeconds = sessionSeconds();
            }
        }
        if (scaledDisplay && commands->view) require(std::abs(commands->view->aspect - 16.f/9) < .0001f,
                "Replication used the letterboxed window aspect instead of the rendered camera aspect");
    }
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
    const auto clickTab = [&](float designX) {
        int width, height, windowWidth, windowHeight;
        glfwGetFramebufferSize(window, &width, &height);
        glfwGetWindowSize(window, &windowWidth, &windowHeight);
        const float scale = std::min(width / 1280.f, height / 720.f);
        // Hidden windows cannot warp the OS cursor. Use the real move callback
        // and enqueue button transitions at the HUD boundary instead.
        mouse(window, ((width - 1280 * scale) / 2 + designX * scale) * windowWidth / width,
                      ((height - 720 * scale) / 2 + 53 * scale) * windowHeight / height);
        in.hud->pointerButton(0, true);
        in.hud->pointerButton(0, false);
    };
    switch (frame) {
        case 2:
            require(differences(0, 1) > 500, "Confirmed pistol did not appear after spawn");
            capture("pistol");
            key(window, GLFW_KEY_1, 0, GLFW_PRESS, 0); // Preview hotbar 1 (SMG).
            break;
        case 3:
            require(in.equippedSlot == 0 && in.slot == 0 && in.actionSelection.selected() == 1,
                    "Preview replaced the server-confirmed pistol");
            button(window, GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS, 0);
            require(in.slot == 1 && !command(in, window, false, 0).left,
                    "Selection confirmation must request SMG and consume the click");
            button(window, GLFW_MOUSE_BUTTON_LEFT, GLFW_RELEASE, 0);
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
            authoritative.slot = 4; authoritative.loaded = 1;
            break;
        case 13:
            require(differences(0, 12) > 500, "Ready grenade did not appear in the hand");
            capture("he_grenade");
            authoritative.loaded = 0; authoritative.weaponCoolingDown = true; ++authoritative.shots;
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
            authoritative.weaponCoolingDown = false; authoritative.loaded = 1;
            break;
        case 17:
            require(differences(12, 16) < 100, "Grenade did not return when the server confirmed cooldown expiry");
            authoritative.loaded = 0;
            break;
        case 18:
            require(differences(0, 17) < 100, "Empty grenade inventory must leave the hand empty");
            authoritative.slot = 1; authoritative.loaded = 2;
            // The following loop must accrue a fixed step after its local press.
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            break;
        case 19:
            capture("smg-before-shot");
            shotsBeforeLocalFire = authoritative.shots;
            holdFireAcknowledgement = true;
            in.capture = true;
            button(window, GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS, 0);
            button(window, GLFW_MOUSE_BUTTON_LEFT, GLFW_RELEASE, 0);
            break;
        case 20:
            require(localFireCommand && authoritative.shots == shotsBeforeLocalFire,
                    "Local firearm feedback did not precede the server shot acknowledgement");
            require(in.weaponMotion.pose(1, localFireSeconds).offset.z > 0 && differences(18,19) > 500,
                    "Local firing input did not immediately animate the held weapon");
            require(brightenedWorldPixels(18,19) > 500,
                    "Local firing input did not immediately illuminate the scene with muzzle flash");
            capture("smg-recoil");
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            break;
        case 21:
            require(in.weaponMotion.pose(1, sessionSeconds()).offset.z == 0,
                    "Local recoil did not recover while its acknowledgement was delayed");
            ++authoritative.shots; --authoritative.loaded;
            holdFireAcknowledgement = false;
            break;
        case 22:
            require(authoritative.resolved >= localFireCommand && differences(18,21) < 100,
                    "Delayed shot acknowledgement replayed local recoil or muzzle flash");
            capture("smg-recovered");
            ++authoritative.shots; --authoritative.loaded;
            break;
        case 23: {
            require(differences(18,22) < 100,
                    "An externally scripted shot counter triggered local recoil or muzzle flash");
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
            key(window, GLFW_KEY_F1, 0, GLFW_PRESS, 0);
            require(in.hudScreen == viewer::HudScreen::Inventory, "F1 must open inventory while seated");
            require(glfwGetInputMode(window, GLFW_CURSOR) == GLFW_CURSOR_NORMAL, "Inventory must release the cursor while seated");
            key(window, GLFW_KEY_F2, 0, GLFW_PRESS, 0);
            key(window, GLFW_KEY_F2, 0, GLFW_REPEAT, 0);
            require(in.hudScreen == viewer::HudScreen::Skills, "F2 must select skills without repeat toggling");
            key(window, GLFW_KEY_F3, 0, GLFW_PRESS, 0);
            require(in.hudScreen == viewer::HudScreen::Health, "F3 must select health while seated");
            key(window, GLFW_KEY_F4, 0, GLFW_PRESS, 0);
            key(window, GLFW_KEY_F4, 0, GLFW_REPEAT, 0);
            require(in.hudScreen == viewer::HudScreen::Map, "F4 must select map without repeat toggling");
            key(window, GLFW_KEY_ENTER, 0, GLFW_PRESS, 0);
            button(window, GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS, 0);
            const auto previewCommand = command(in, window, false, 0.0);
            require(!previewCommand.enterVehicle && !previewCommand.left && !previewCommand.reload &&
                    previewCommand.movement.forward == 0, "Prototype screen passed gameplay input through");
            key(window, GLFW_KEY_ESCAPE, 0, GLFW_PRESS, 0);
            require(in.hudScreen == viewer::HudScreen::Gameplay && !in.leave, "Escape must close skills before leaving session");
            require(glfwGetInputMode(window, GLFW_CURSOR) == GLFW_CURSOR_DISABLED, "Closing inventory screens must restore cursor capture");
            in.capture = true;
            key(window, GLFW_KEY_F12, 0, GLFW_PRESS, 0);
            require(!in.capture && !in.driver.thirdPerson(), "F12 must release the mouse without changing camera mode");
            key(window, GLFW_KEY_F12, 0, GLFW_PRESS, 0);
            key(window, GLFW_KEY_ENTER, 0, GLFW_PRESS, 0);
            require(command(in, window, false, 0.0).enterVehicle && !command(in, window, false, 0.0).enterVehicle,
                    "Enter must queue exactly one seat-transition command");
            authoritative.vehicle = multiplayer::noVehicle; authoritative.seat = multiplayer::noVehicleSeat;
            authoritative.movement = onFootAfterExit;
            break;
        }
        case 27:
            require(!in.driver.driving() && !in.driver.thirdPerson(), "Confirmed exit must restore walking camera");
            key(window, GLFW_KEY_LEFT_ALT, 0, GLFW_PRESS, 0);
            require(!in.toggleDrivingCamera && in.capture, "Alt must leave camera and mouse capture unchanged on foot");
            key(window, GLFW_KEY_RIGHT_ALT, 0, GLFW_PRESS, 0);
            require(in.capture, "Right Alt must leave mouse capture unchanged on foot");
            key(window, GLFW_KEY_C, 0, GLFW_PRESS, 0);
            key(window, GLFW_KEY_C, 0, GLFW_REPEAT, 0);
            key(window, GLFW_KEY_C, 0, GLFW_RELEASE, 0);
            require(command(in, window, false, 0).movement.toggleProne &&
                    !command(in, window, false, 0).movement.toggleProne,
                    "C must queue one prone toggle and ignore repeat/release");
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
            authoritative.movement = onFootAfterExit;
            break;
        case 30: {
            require(!in.driver.occupied(), "Passenger exit did not restore walking controls");
            const auto oldSlot=in.slot;
            key(window, GLFW_KEY_1, 0, GLFW_PRESS, 0);
            scroll(window, 0, 1);
            require(in.actionSelection.selected() == 2, "Scrolling up must preview the next action");
            scroll(window, 0, -1);
            require(in.actionSelection.selected() == 1 && in.slot == oldSlot,
                    "Scrolling down must preview the previous action without equipping");
            in.actionSelection.close();
            const auto flashlight = in.flashlight;
            key(window, GLFW_KEY_F, 0, GLFW_PRESS, 0);
            require(in.flashlight == flashlight, "F must not toggle the flashlight");
            key(window, GLFW_KEY_L, 0, GLFW_PRESS, 0);
            require(in.flashlight != flashlight, "L must toggle the flashlight");
            key(window, GLFW_KEY_L, 0, GLFW_PRESS, 0);
            key(window,GLFW_KEY_0,0,GLFW_PRESS,0);
            require(in.slot==oldSlot,"US number-row 0 unexpectedly equipped fists");
            key(window,GLFW_KEY_GRAVE_ACCENT,0,GLFW_PRESS,0);
            require(in.slot==oldSlot && in.actionSelection.selected()==0,
                    "Physical key left of 1 must preview slot zero without equipping");
            key(window,GLFW_KEY_5,0,GLFW_PRESS,0);
            button(window,GLFW_MOUSE_BUTTON_LEFT,GLFW_PRESS,0);
            button(window,GLFW_MOUSE_BUTTON_LEFT,GLFW_RELEASE,0);
            require(in.slot==viewer::punchSlot && command(in,window,false,0).slot==viewer::punchSlot,
                    "Confirmed Punch shortcut did not request multiplayer fists");
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
            punchEye = in.camera.position();
            authoritative.melee={};
            break;
        case 32:
            require(differences(30,31)>500,"Confirmed physical hands did not render in first person");
            authoritative.melee=punchPose; ++authoritative.shots;
            break;
        case 33:
            capture("fists-punch-confirmed-again");
            require(glm::distance(in.camera.position(), punchEye) < .001f,
                    "Scripted on-foot state moved the camera between identical punch poses");
            require(differences(30,32)<100,"Confirmed punches created firearm recoil or muzzle lighting");
            in.interaction=true;
            break;
        case 34:
            require(differences(31,33)<100,"Interaction mode retained the local physical hands");
            in.interaction=false; key(window,GLFW_KEY_GRAVE_ACCENT,0,GLFW_PRESS,0);
            button(window,GLFW_MOUSE_BUTTON_LEFT,GLFW_PRESS,0);
            button(window,GLFW_MOUSE_BUTTON_LEFT,GLFW_RELEASE,0);
            authoritative.slot=0; authoritative.melee={};
            break;
        case 35:
            capture("pistol-after-fists");
            require(differences(31,34)>500,"Equipping a firearm after fists lost its model");
            button(window,GLFW_MOUSE_BUTTON_LEFT,GLFW_PRESS,0);
            require(command(in,window,false,0).left,"A fresh physical press must produce firing input");
            authoritative.slot=4; authoritative.loaded=0;
            authoritative.reloadRemaining=.5f; authoritative.reloadDuration=1;
            break;
        case 36:
            require(in.weaponReloading && in.leftHeld && in.suppressLeftUntilRelease && !command(in,window,false,0).left,
                    "Confirmed grenade autoload must latch suppression for the physically held throw button");
            authoritative.reloadRemaining=authoritative.reloadDuration=0; authoritative.loaded=1;
            break;
        case 37:
            require(!in.weaponReloading && in.leftHeld && in.suppressLeftUntilRelease && !command(in,window,false,0).left,
                    "Autoload completion must not turn a held throw button into a fresh press");
            button(window,GLFW_MOUSE_BUTTON_LEFT,GLFW_RELEASE,0);
            button(window,GLFW_MOUSE_BUTTON_LEFT,GLFW_PRESS,0);
            require(command(in,window,false,0).left,"Physical release and repress must rearm firing after autoload");
            key(window,GLFW_KEY_R,0,GLFW_PRESS,0);
            {
                const auto reloading=command(in,window,false,0);
                require(reloading.reload && !reloading.left && in.suppressLeftUntilRelease,
                        "Requesting reload while physically holding fire must require a release");
            }
            button(window,GLFW_MOUSE_BUTTON_LEFT,GLFW_RELEASE,0);
            button(window,GLFW_MOUSE_BUTTON_LEFT,GLFW_PRESS,0);
            authoritative.weaponSwitchRemaining=.5f; authoritative.weaponSwitchDuration=1;
            break;
        case 38:
            require(in.weaponSwitching && in.leftHeld && in.suppressLeftUntilRelease && !command(in,window,false,0).left,
                    "Confirmed switching must retain the physical-release requirement");
            authoritative.weaponSwitchRemaining=authoritative.weaponSwitchDuration=0;
            break;
        case 39:
            require(!in.weaponSwitching && in.leftHeld && !command(in,window,false,0).left,
                    "Switch completion must not rearm a physically held click");
            button(window,GLFW_MOUSE_BUTTON_LEFT,GLFW_RELEASE,0);
            button(window,GLFW_MOUSE_BUTTON_LEFT,GLFW_PRESS,0);
            require(command(in,window,false,0).left,"A fresh click after switch completion must fire");
            button(window,GLFW_MOUSE_BUTTON_LEFT,GLFW_RELEASE,0);
            button(window, GLFW_MOUSE_BUTTON_MIDDLE, GLFW_PRESS, 0);
            require(in.wheelOpen && !in.actionSelection.open(), "Middle mouse did not open the command wheel");
            break;
        case 40: {
            capture("command-wheel-center");
            const auto yaw = in.camera.yawDegrees(), pitch = in.camera.pitchDegrees();
            in.firstMouse = false; in.lastX = in.lastY = 100;
            mouse(window, 100, 0);
            require(in.camera.yawDegrees() == yaw && in.camera.pitchDegrees() == pitch &&
                    multiplayer::signalWheelSelection(in.wheelOffset) == multiplayer::SignalKind::Danger,
                    "Command wheel moved the camera or selected the wrong sector");
            button(window, GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS, 0);
            button(window, GLFW_MOUSE_BUTTON_RIGHT, GLFW_PRESS, 0);
            key(window, GLFW_KEY_R, 0, GLFW_PRESS, 0);
            scroll(window, 0, -1);
            const auto suppressed = command(in, window, false, 0);
            require(!suppressed.left && !suppressed.right && !suppressed.reload && !in.actionSelection.open(),
                    "Wheel passed fire, scope, reload or hotbar input through");
            break;
        }
        case 41: {
            capture("command-wheel-danger");
            button(window, GLFW_MOUSE_BUTTON_MIDDLE, GLFW_RELEASE, 0);
            const auto sent = command(in, window, false, 0);
            require(!in.wheelOpen && sent.signal == multiplayer::SignalKind::Danger && !sent.left &&
                    command(in, window, false, 0).signal == multiplayer::SignalKind::None,
                    "Release must send one command and keep held firing suppressed");
            button(window, GLFW_MOUSE_BUTTON_LEFT, GLFW_RELEASE, 0);
            button(window, GLFW_MOUSE_BUTTON_RIGHT, GLFW_RELEASE, 0);
            command(in, window, false, 0);
            button(window, GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS, 0);
            require(command(in, window, false, 0).left, "Fresh click did not rearm after the wheel closed");
            button(window, GLFW_MOUSE_BUTTON_LEFT, GLFW_RELEASE, 0);
            button(window, GLFW_MOUSE_BUTTON_MIDDLE, GLFW_PRESS, 0);
            button(window, GLFW_MOUSE_BUTTON_MIDDLE, GLFW_RELEASE, 0);
            require(command(in, window, false, 0).signal == multiplayer::SignalKind::None, "Center release sent a command");
            button(window, GLFW_MOUSE_BUTTON_MIDDLE, GLFW_PRESS, 0);
            in.wheelOffset = {100, 60};
            break;
        }
        case 42:
            capture("command-wheel-move");
            key(window, GLFW_KEY_ESCAPE, 0, GLFW_PRESS, 0);
            button(window, GLFW_MOUSE_BUTTON_MIDDLE, GLFW_RELEASE, 0);
            require(!in.wheelOpen && !in.leave && command(in, window, false, 0).signal == multiplayer::SignalKind::None,
                    "Escape must cancel the wheel before leaving the session");
            button(window, GLFW_MOUSE_BUTTON_MIDDLE, GLFW_PRESS, 0);
            key(window, GLFW_KEY_F1, 0, GLFW_PRESS, 0);
            button(window, GLFW_MOUSE_BUTTON_MIDDLE, GLFW_RELEASE, 0);
            require(!in.wheelOpen && in.pendingSignal == multiplayer::SignalKind::None,
                    "Opening inventory did not cancel the wheel");
            key(window, GLFW_KEY_F1, 0, GLFW_PRESS, 0);
            button(window, GLFW_MOUSE_BUTTON_MIDDLE, GLFW_PRESS, 0);
            focus(window, GLFW_FALSE);
            button(window, GLFW_MOUSE_BUTTON_MIDDLE, GLFW_RELEASE, 0);
            require(!in.wheelOpen && in.pendingSignal == multiplayer::SignalKind::None,
                    "Focus loss submitted a command");
            key(window, GLFW_KEY_F12, 0, GLFW_PRESS, 0);
            authoritative.slot = 3; authoritative.loaded = 2;
            authoritative.yaw = -90; authoritative.pitch = 0;
            in.camera.setOrientation(-90, 0);
            break;
        case 43:
            in.capture = true;
            button(window, GLFW_MOUSE_BUTTON_RIGHT, GLFW_PRESS, 0);
            require(in.camera.viewMode() == viewer::Camera::ViewMode::zoom10,
                    "Scoped local firing fixture did not enter sniper scope");
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            break;
        case 44:
            capture("sniper-before-local-shot");
            localFireCommand = 0; localFireSlot = 3;
            shotsBeforeLocalFire = authoritative.shots;
            holdFireAcknowledgement = true;
            button(window, GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS, 0);
            button(window, GLFW_MOUSE_BUTTON_LEFT, GLFW_RELEASE, 0);
            break;
        case 45: {
            require(localFireCommand && authoritative.shots == shotsBeforeLocalFire &&
                    in.weaponMotion.pose(3, localFireSeconds).offset.z > 0,
                    "Scoped recoil waited for server acknowledgement");
            require(in.camera.viewMode() == viewer::Camera::ViewMode::zoom10,
                    "Local firing left the sniper scope");
            const viewer::WeaponMotion resting;
            const auto baseline = resting.aimCamera(in.camera, 3, localFireSeconds, true);
            const auto recoiling = in.weaponMotion.aimCamera(in.camera, 3, localFireSeconds, true);
            const auto outgoing = command(in, window, false, localFireSeconds);
            require(glm::distance(baseline.direction(), recoiling.direction()) > .0001f,
                    "Immediate local recoil did not move the scoped aim beyond the same-time sway");
            require(outgoing.pitch == recoiling.pitchDegrees() && outgoing.movement.yawDegrees == recoiling.yawDegrees() &&
                    in.camera.yawDegrees() == authoritative.yaw && in.camera.pitchDegrees() == authoritative.pitch,
                    "Scoped local recoil disagreed with sent aim or accumulated into mouse orientation");
            capture("sniper-local-recoil");
            std::this_thread::sleep_for(std::chrono::milliseconds(750));
            break;
        }
        case 46:
            require(in.weaponMotion.pose(3, sessionSeconds()).offset.z == 0,
                    "Scoped recoil did not recover before its delayed acknowledgement");
            ++authoritative.shots; --authoritative.loaded;
            holdFireAcknowledgement = false;
            break;
        case 47: {
            require(authoritative.resolved >= localFireCommand && in.weaponMotion.pose(3, sessionSeconds()).offset.z == 0,
                    "Delayed sniper acknowledgement replayed local recoil");
            const double now = sessionSeconds();
            const viewer::WeaponMotion resting;
            const auto baseline = resting.aimCamera(in.camera, 3, now, true);
            const auto acknowledged = in.weaponMotion.aimCamera(in.camera, 3, now, true);
            require(glm::distance(baseline.direction(), acknowledged.direction()) < .000001f,
                    "Delayed sniper acknowledgement applied another kick to scoped aim");
            capture("sniper-local-acknowledged");
            key(window, GLFW_KEY_F1, 0, GLFW_PRESS, 0);
            break;
        }
        case 48: clickTab(300); break;
        case 49: require(in.hudScreen == viewer::HudScreen::Skills, "Inventory tab click did not reach the client's Skills input state"); break;
        case 50: clickTab(475); break;
        case 51: require(in.hudScreen == viewer::HudScreen::Health, "Skills page must forward tab clicks to Health"); break;
        case 52: clickTab(680); break;
        case 53: require(in.hudScreen == viewer::HudScreen::Map, "Health page must forward tab clicks to Map"); break;
        case 54: clickTab(80); break;
        case 55: require(in.hudScreen == viewer::HudScreen::Inventory, "Map tab click did not return to Inventory"); break;
        case 56: clickTab(80); break;
        case 57:
            require(in.hudScreen == viewer::HudScreen::Inventory, "Clicking the active inventory tab closed the menu");
            key(window, GLFW_KEY_ESCAPE, 0, GLFW_PRESS, 0);
            require(in.hudScreen == viewer::HudScreen::Gameplay && !in.leave, "Escape after tab navigation must close the menu");
            glfwSetWindowShouldClose(window, true);
            break;
    }
    // This fixture also scripts external authoritative equipment changes.
    // Their matching requests stand in for completed user selections.
    if (!in.actionSelection.open()) in.slot = authoritative.slot;
    if (!holdFireAcknowledgement) authoritative.resolved = latestCommand;
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
        require(argc == 3 || (argc == 4 && std::string(argv[3]) == "--scaled"), "Expected test map and fixture directory, optionally --scaled");
        scaledDisplay = argc == 4;
        if (scaledDisplay) frameHeight = 800;
        fixture = argv[2];
        std::filesystem::create_directories(fixture);
        const std::filesystem::path source(MP_ROOT);
        const auto copy = std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing;
        std::filesystem::copy(source / "storage", fixture / "storage", copy);
        std::filesystem::copy(source / "vehicles", fixture / "vehicles", copy);
        std::filesystem::copy(source / "skills", fixture / "skills", copy);
        std::filesystem::copy_file(source / "skills.toml", fixture / "skills.toml", std::filesystem::copy_options::overwrite_existing);
        std::filesystem::copy(source / "shaders", fixture / "shaders", copy);
        std::filesystem::copy(source / "ui", fixture / "ui", copy);
        const auto icons = source / "data/assets/game-icons/hud";
        if (std::filesystem::is_directory(icons)) {
            std::filesystem::create_directories(fixture / "data/assets/game-icons");
            std::filesystem::copy(icons, fixture / "data/assets/game-icons/hud", copy);
        }
        std::filesystem::create_directories(fixture / "multiplayer/client");
        for (const auto* shader : {"players.vert", "players.frag"})
            std::filesystem::copy_file(source / "multiplayer/client" / shader, fixture / "multiplayer/client" / shader,
                                       std::filesystem::copy_options::overwrite_existing);
        std::ofstream(fixture / "inventory.toml") <<
            "equipped='pistol'\n[weapons]\npistol=0\nsubmachine_gun=0\nsniper=0\nshotgun=0\nhe_grenade=1\n"
            "[ammo]\n"
            "[hotbar]\n0='pistol'\n1='submachine_gun'\n2='shotgun'\n3='sniper'\n4='he_grenade'\n5='skill:punch'\n"
            "[placements.pistol]\nstorage='body/hip_holster/holster'\nx=0\ny=0\n"
            "[placements.he_grenade]\nstorage='body/tactical_vest/lower_right'\nx=0\ny=0\n"
            "[placements.submachine_gun]\nstorage='backpack/tactical_backpack/main'\nx=0\ny=0\n"
            "[placements.shotgun]\nstorage='backpack/tactical_backpack/main'\nx=3\ny=0\n"
            "[placements.sniper]\nstorage='backpack/tactical_backpack/main'\nx=0\ny=2\n";
        multiplayer::Options options; options.map = argv[1]; options.dayNight.dayLengthSeconds = 0;
        options.trees = options.forests = false; options.zombieCount = 0;
        multiplayer::Scene scene(options);
        viewer::FPSController controller;
        require(controller.spawn(scene.spawnEye, *scene.collision), "Cannot spawn weapon test player");
        authoritative.id = 1; authoritative.movement = controller.movementState();
        // Script the same safe exit that the authoritative server chooses.
        // Restoring the original spawn would put walking feet inside this car;
        // normal command ACKs would then repeatedly trigger local depenetration.
        scene.vehicles->add(authoritative.movement.feet + glm::vec3(0, .8f, 0));
        const auto exits = viewer::vehicleExits(*scene.vehicles, 0, *scene.collision, controller.config(), 1);
        require(!exits.empty(), "Weapon fixture car has no supported passenger exit");
        viewer::FPSController exiting;
        require(exiting.spawn(exits.front() + glm::vec3(0, exiting.eyeHeight(), 0), *scene.collision),
                "Cannot place scripted player outside the fixture car");
        onFootAfterExit = exiting.movementState();
        char program[] = "client", noTrees[] = "--no-trees", count[] = "--zombie-count", zero[] = "0";
        char hidden[] = "--hidden", seconds[] = "--seconds", duration[] = "8", noAudio[] = "--no-audio";
        char dayLength[] = "--day-length";
        // The scaled variant repeats local firing checks through --no-audio.
        char* args[] = {program, argv[1], noTrees, count, zero, hidden, seconds, duration, dayLength, zero, noAudio};
        auto clientOptions = multiplayer::parseOptions(scaledDisplay ? 11 : 10, args, false);
        require(clientOptions.audio != scaledDisplay, "Client audio option did not match its test variant");
        const auto releaseWindow = [](GLFWwindow* window) { if (window) { glfwDestroyWindow(window); glfwTerminate(); } };
        std::unique_ptr<GLFWwindow, decltype(releaseWindow)> sharedWindow(nullptr, releaseWindow);
        if (scaledDisplay) {
            clientOptions.display.fullscreen = true;
            clientOptions.display.resolution = glm::ivec2(960, 540);
            sharedWindow.reset(multiplayer::createClientWindow(clientOptions.display, true));
            require(glfwGetWindowMonitor(sharedWindow.get()) == nullptr, "Hidden fullscreen capture occupied the monitor");
            glfwSetWindowSize(sharedWindow.get(), frameWidth, frameHeight);
            glfwPollEvents();
            clientOptions.screenshot = fixture / "scaled-window.ppm";
        }
        require(multiplayer::runClientSession(clientOptions, sharedWindow.get()) == 0 && frame == 58, "Graphical client did not finish all weapon, command wheel and page-tab states");
        if (scaledDisplay) {
            std::ifstream screenshot(clientOptions.screenshot);
            std::string magic; int width = 0, height = 0;
            screenshot >> magic >> width >> height;
            require(magic == "P6" && width == frameWidth && height == frameHeight,
                    "Upscaled screenshot did not preserve native GUI resolution");
            const auto& pixels = frames.front();
            require(pixels[0] == 0 && pixels[1] == 0 && pixels[2] == 0,
                    "Different scene/window aspects did not leave an undistorted fitted scene");
        }
        const std::array<std::size_t, 5> weaponFrames{1, 3, 4, 5, 12};
        for (std::size_t a = 0; a < weaponFrames.size(); ++a)
            for (std::size_t b = a+1; b < weaponFrames.size(); ++b)
                require(differences(weaponFrames[a], weaponFrames[b]) > 500,
                        "Server-confirmed weapon models must produce distinct frames");
        std::cout << "Multiplayer weapons, melee, driving cameras, Enter, Alt and F12 checks passed\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
