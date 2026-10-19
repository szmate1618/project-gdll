// Run the real graphical client loop against scripted authoritative snapshots.
#define main clientMain
#include "../client/main.cpp"
#undef main
#include <fstream>

namespace {
std::filesystem::path fixture;
multiplayer::PlayerState authoritative;
std::vector<std::vector<unsigned char>> frames;
int frame = 0;
bool departed = false;
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
            glfwSetWindowShouldClose(window, true);
            break;
    }
    Snapshot snapshot; snapshot.tick = static_cast<std::uint64_t>(frame++);
    snapshot.players.push_back(authoritative);
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
        std::filesystem::copy(source / "shaders", fixture / "shaders", copy);
        std::filesystem::create_directories(fixture / "multiplayer/client");
        for (const auto* shader : {"players.vert", "players.frag"})
            std::filesystem::copy_file(source / "multiplayer/client" / shader, fixture / "multiplayer/client" / shader,
                                       std::filesystem::copy_options::overwrite_existing);
        std::ofstream(fixture / "inventory.toml") <<
            "equipped='pistol'\n[weapons]\npistol=0\nsubmachine_gun=0\nsniper=0\nshotgun=0\nhe_grenade=2\n"
            "[ammo]\npistol_rounds=0\nsmg_rounds=0\nsniper_rounds=0\nshotgun_shells=0\nhe_grenades=0\n";
        multiplayer::Options options; options.map = argv[1];
        options.trees = options.forests = false; options.zombieCount = 0;
        multiplayer::Scene scene(options);
        viewer::FPSController controller;
        require(controller.spawn(scene.spawnEye, *scene.collision), "Cannot spawn weapon test player");
        authoritative.id = 1; authoritative.movement = controller.movementState();
        char program[] = "client", noTrees[] = "--no-trees", count[] = "--zombie-count", zero[] = "0";
        char hidden[] = "--hidden", seconds[] = "--seconds", duration[] = "5";
        char* args[] = {program, argv[1], noTrees, count, zero, hidden, seconds, duration};
        require(clientMain(8, args) == 0 && frame == 19, "Graphical client did not finish all weapon states");
        const std::array<std::size_t, 5> weaponFrames{1, 3, 4, 5, 12};
        for (std::size_t a = 0; a < weaponFrames.size(); ++a)
            for (std::size_t b = a+1; b < weaponFrames.size(); ++b)
                require(differences(weaponFrames[a], weaponFrames[b]) > 500,
                        "Server-confirmed weapon models must produce distinct frames");
        std::cout << "Multiplayer weapon selection, confirmation, scope and Interaction rendering passed\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
