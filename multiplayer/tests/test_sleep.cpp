#include "server/world.hpp"
#include "client/snapshots.hpp"
#include "sleep_plan.hpp"
#include "simulation_clock.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
template<class F> void rejects(F action) {
    bool rejected = false;
    try { action(); } catch (const std::exception&) { rejected = true; }
    require(rejected, "Invalid sleep message accepted");
}
void calendar() {
    using namespace viewer;
    require(sleepDurationLabel(30) == "00:30" && sleepDurationLabel(720) == "12:00", "Duration formatting");
    for (const int minutes : {0, 1, 29, 31, 719, 721, 1440})
        require(!validSleepDuration(minutes), "Invalid sleep duration allowed");
    const DayNightCycle cycle{23.5, 1200};
    const auto start = gameCalendarTime(cycle, 0), midnight = gameCalendarTime(cycle, 25), next = gameCalendarTime(cycle, 0, 60);
    require(start.day == 1 && start.clock == "23:30", "Calendar start does not match world time");
    require(midnight.day == 2 && midnight.clock == "00:00", "Calendar midnight rollover");
    require(next.day == 2 && next.clock == "00:30", "Wake preview rollover");
    const auto later = gameCalendarTime(cycle, 2425);
    require(later.day == 4 && later.clock == "00:00", "Calendar lost complete days");
    require(gameCalendarTime({17, 0}, 36000).clock == "17:00", "Frozen clock advanced");
}
void planning() {
    using namespace viewer;
    SleepPlan plan; plan.join(1); plan.join(2); plan.join(2);
    require(plan.state().participants.size() == 2, "Duplicate participant");
    require(!plan.apply(3, {SleepActionKind::Suggest, 0, 30}), "Unknown player proposed sleep");
    require(!plan.apply(1, {SleepActionKind::Suggest, 0, 45}), "Non-half-hour suggestion");
    require(plan.apply(1, {SleepActionKind::Suggest, 0, 480}), "Cannot suggest sleep");
    require(!plan.state().ready() && plan.state().response(1) == SleepResponse::Accept, "Proposer must accept their suggestion");
    require(plan.apply(2, {SleepActionKind::Guard, 1, 30}) && plan.state().ready(), "Guard did not count as an answer");
    require(plan.apply(2, {SleepActionKind::Suggest, 1, 720}), "Cannot counterpropose");
    require(plan.state().response(1) == SleepResponse::Pending && !plan.state().ready(), "Counterproposal retained consent");
    require(!plan.apply(1, {SleepActionKind::Accept, 1, 480}), "Stale accept approved a new duration");
    require(!plan.apply(1, {SleepActionKind::Suggest, 1, 30}), "Stale suggestion overwrote latest plan");
    plan.apply(1, {SleepActionKind::Accept, 2, 30});
    plan.join(3); require(!plan.state().ready(), "Joining player silently accepted");
    plan.leave(3); require(plan.state().ready(), "Departed player blocked agreement");
    plan.apply(2, {SleepActionKind::Guard, 2, 30});
    plan.apply(1, {SleepActionKind::Guard, 2, 30});
    require(!plan.state().ready(), "All guards produced a sleep agreement");
    plan.leave(2); require(!plan.state().proposer && plan.state().revision == 3, "Departing proposer left an orphan proposal");
}
void lifecycle() {
    using namespace viewer;
    SleepPlan plan; plan.join(1); plan.join(2);
    plan.apply(1, {SleepActionKind::Suggest, 0, 480});
    require(!plan.begin(10, {12,1200}), "Sleep began before the group answered");
    plan.apply(2, {SleepActionKind::Guard, 1, 30});
    require(plan.begin(10, {12,1200}) && plan.state().asleep(1) && plan.state().guarding(2) &&
        plan.state().inSleepSession(2) && plan.state().rate() == 30,
        "Automatic watch must suspend guard controls while allowing accelerated sleep");
    require(plan.state().endsAt == 410, "Eight game hours must equal 400 simulation seconds on the default clock");
    plan.apply(2, {SleepActionKind::Accept,1,30},20);
    require(plan.state().rate() == 30, "The last guard joining sleep must permit acceleration");
    plan.update(409.9); require(plan.state().sleeping, "Sleep ended before the deadline");
    plan.update(410); require(!plan.state().sleeping && plan.state().rate() == 1 && plan.state().wakeReason == SleepWakeReason::Rested,
        "Completed sleep did not restore normal time");
    require(!plan.begin(411,{12,1200}), "Completed sleep automatically restarted");
    plan.apply(1,{SleepActionKind::Suggest,plan.state().revision,30});
    plan.apply(2,{SleepActionKind::Accept,plan.state().revision,30});
    require(!plan.begin(500,{12,0}), "Frozen calendar entered an endless sleep");
    plan.begin(500,{12,1200}); plan.join(3,501);
    require(!plan.state().sleeping && plan.state().rate() == 1 && plan.state().wakeReason == SleepWakeReason::PlayerJoined,
        "Joining a session did not end acceleration with an arrival reason");
    plan.apply(1,{SleepActionKind::Suggest,plan.state().revision,30});
    plan.apply(2,{SleepActionKind::Accept,plan.state().revision,30});
    plan.apply(3,{SleepActionKind::Accept,plan.state().revision,30});
    require(plan.begin(502,{12,1200}), "Cannot restart sleep after arrival");
    plan.leave(3,503);
    require(!plan.state().sleeping && plan.state().rate() == 1 && plan.state().wakeReason == SleepWakeReason::PlayerLeft,
        "Leaving a session did not end acceleration with a departure reason");
    FPSController player; auto movement = player.movementState();
    movement.feet = {4,0,8}; movement.exertion.strain = 1; movement.exertion.stamina = .1f;
    player.restoreMovement(movement);
    player.restSleeping(400,8);
    require(player.feet() == movement.feet && player.exertion().strain == 0 && player.exertion().stamina == 1,
        "Sleep recovery must restore exertion without moving or healing the player");
    player.restoreMovement(movement); player.standGuard(400);
    require(player.feet() == movement.feet && player.exertion().strain == 1,
        "Automatic watch must not move the guard or award sleep strain recovery");
    plan.apply(1,{SleepActionKind::Suggest,plan.state().revision,30});
    plan.apply(2,{SleepActionKind::Guard,plan.state().revision,30});
    require(plan.begin(504,{12,1200}), "Cannot start another guarded sleep");
    plan.guardSpotted({1,"Sleeper",1,{0,1.7f,-75}},505);
    require(plan.state().sleeping, "A sleeping player spoofed a guard sighting");
    plan.guardSpotted({2,"Bandi",1,{0,1.7f,-75}},505);
    require(!plan.state().sleeping && plan.state().rate() == 1 &&
        plan.state().wakeReason == SleepWakeReason::GuardSpotted && plan.state().guardAlert.name == "Bandi",
        "Guard sighting must wake the entire group and preserve the named alert");
}
void wire() {
    using namespace multiplayer;
    const Hello hello{std::string(64,'a'), "Bandi"};
    auto handshake = encode(hello);
    require(std::get<Hello>(decode(handshake.data(),handshake.size())).characterName == "Bandi", "Character name missing from handshake");
    rejects([&] { encode(Hello{hello.fingerprint,std::string(33,'a')}); });
    rejects([&] { encode(Hello{hello.fingerprint,"Bandi\nspoof"}); });
    Command command; command.sequence = 1;
    command.sleepAction = viewer::SleepAction{viewer::SleepActionKind::Suggest, 42, 720};
    auto bytes = encode(Commands{{command}});
    const auto copy = std::get<Commands>(decode(bytes.data(), bytes.size())).commands.front().sleepAction;
    require(copy && copy->revision == 42 && copy->minutes == 720 && copy->kind == viewer::SleepActionKind::Suggest,
        "Sleep action roundtrip");
    command.sleepAction->minutes = 15; rejects([&] { encode(Commands{{command}}); });
    command.sleepAction->minutes = 30; command.sleepAction->kind = static_cast<viewer::SleepActionKind>(9);
    rejects([&] { encode(Commands{{command}}); });
    Snapshot snapshot; snapshot.tick = 100;
    snapshot.sleepPlan = {43, 1, 480, {{1, viewer::SleepResponse::Accept}, {2, viewer::SleepResponse::Guard}}};
    bytes = encode(snapshot);
    const auto restored = std::get<Snapshot>(decode(bytes.data(), bytes.size()));
    require(restored.sleepPlan.ready() && restored.sleepPlan.response(2) == viewer::SleepResponse::Guard,
        "Sleep snapshot roundtrip");
    for (const auto reason : {viewer::SleepWakeReason::PlayerJoined, viewer::SleepWakeReason::PlayerLeft}) {
        snapshot.sleepPlan.wakeReason = reason;
        bytes = encode(snapshot);
        require(std::get<Snapshot>(decode(bytes.data(), bytes.size())).sleepPlan.wakeReason == reason,
            "Arrival/departure wake reason lost in transit");
    }
    snapshot.sleepPlan.wakeReason = viewer::SleepWakeReason::GuardSpotted;
    snapshot.sleepPlan.guardAlert = {2,"Bandi",1,{28,1.7f,-70}};
    bytes = encode(snapshot);
    const auto alert = std::get<Snapshot>(decode(bytes.data(),bytes.size())).sleepPlan.guardAlert;
    require(alert.guard == 2 && alert.name == "Bandi" && alert.bearing == 1 && alert.target == glm::vec3(28,1.7f,-70),
        "Named directional guard alert lost in transit");
    snapshot.sleepPlan.guardAlert.bearing = 16; rejects([&] { encode(snapshot); });
    snapshot.sleepPlan.guardAlert.bearing = 1; snapshot.sleepPlan.guardAlert.guard = 0;
    rejects([&] { encode(snapshot); });
    snapshot.sleepPlan.wakeReason = static_cast<viewer::SleepWakeReason>(8);
    rejects([&] { encode(snapshot); });
    Snapshots snapshots; snapshots.push(restored, 0);
    snapshot.tick = 101; snapshot.sleepPlan = {44, 2, 30, {{1, viewer::SleepResponse::Pending}, {2, viewer::SleepResponse::Accept}}};
    snapshots.push(snapshot, 1);
    require(!snapshots.push(restored, 2) && snapshots.latest().sleepPlan.minutes == 30, "Reordered packet restored superseded sleep plan");
    snapshot.sleepPlan.participants.push_back({2, viewer::SleepResponse::Accept}); rejects([&] { encode(snapshot); });
    snapshot.sleepPlan.participants.pop_back(); snapshot.sleepPlan.proposer = 3; rejects([&] { encode(snapshot); });
}
void server(const char* map) {
    using namespace multiplayer;
    Options options; options.map = map; options.trees = options.forests = options.cars = false; options.zombieCount = 0;
    Scene scene(options); World world(scene);
    const auto first = world.join(), second = world.join();
    Command command; command.sequence = 1; command.sleepAction = viewer::SleepAction{viewer::SleepActionKind::Suggest, 0, 240};
    for (const auto& [item, definition] : scene.initialInventory.items()) {
        (void)definition;
        command.inventoryActions.push_back({viewer::InventoryDragActionKind::PickUp,item,""});
    }
    for (int retry = 0; retry < 3; ++retry) world.submit(first, Commands{{command}});
    for (int i = 0; i < 10; ++i) world.step();
    const auto proposal = world.snapshot(second).sleepPlan;
    require(world.players().at(first).inventory.held().has_value(), "Sleep fixture did not start a reversible inventory drag");
    require(proposal.proposer == first && proposal.minutes == 240 && proposal.revision == 1, "Suggestion missing from other player's snapshot");
    command.sleepAction = viewer::SleepAction{viewer::SleepActionKind::Guard, proposal.revision, 30};
    command.inventoryActions.clear();
    world.submit(second, Commands{{command}});
    for (int i = 0; i < 10; ++i) world.step();
    require(world.snapshot(first).sleepPlan.ready(), "Remote guard response did not reach proposer");
    require(world.snapshot(first).sleepPlan.sleeping && world.simulationRate() == 30, "Guarded sleep must permit acceleration");
    require(!world.players().at(first).inventory.held(), "Sleep left an unfinished inventory transaction attached to the player");
    command.sequence = 2; command.sleepAction = viewer::SleepAction{viewer::SleepActionKind::Accept, proposal.revision, 30};
    world.submit(second,Commands{{command}});
    for (int i=0;i<10;++i) world.step();
    require(world.simulationRate() == 30, "Unanimous sleep did not enable acceleration");
    const auto feet = world.players().at(first).controller.feet();
    command.sequence = 2; command.sleepAction.reset(); command.movement.forward = 1;
    world.submit(first,Commands{{command}});
    for (int i=0;i<60;++i) world.step();
    require(world.players().at(first).controller.feet() == feet, "Sleeping player moved on queued or held commands");
    auto sleeping = world.snapshot(first); sleeping.tick = 6000;
    Snapshots buffer; buffer.push(sleeping,0); buffer.apply(scene,0);
    auto guarded = sleeping; guarded.tick = 6003; guarded.sleepPlan.participants.front().response = viewer::SleepResponse::Guard;
    buffer.push(guarded,.05); buffer.apply(scene,.05);
    require(std::abs(buffer.animationSeconds() - 6003./60) < .001, "Returning to guard duty retained an accelerated presentation clock");
    auto awake = guarded; awake.tick = 6006; awake.sleepPlan.sleeping = false;
    buffer.push(awake,.1); buffer.apply(scene,.1);
    require(std::abs(buffer.animationSeconds() - 6006./60) < .001, "Waking retained a stale presentation clock");
    world.leave(first);
    require(!world.snapshot(second).sleepPlan.proposer, "Disconnected proposer notification persisted");
    command.sequence = 3; command.movement = {}; command.sleepAction = viewer::SleepAction{viewer::SleepActionKind::Suggest,world.snapshot(second).sleepPlan.revision,30};
    world.submit(second,Commands{{command}});
    for(int i=0;i<10;++i) world.step();
    require(world.simulationRate() == 30, "A sole survivor could not start sleep");
    viewer::ZombieInstance threat; threat.restRoot = threat.transform = threat.restTransform =
        glm::translate(glm::mat4(1),world.players().at(second).controller.feet()+glm::vec3(59,0,0));
    if (scene.zombies.assets.empty()) scene.zombies.assets.emplace_back();
    scene.zombies.instances.push_back(threat); scene.zombies.rebuildSpatialIndex();
    world.step();
    require(!world.snapshot(second).sleepPlan.sleeping && world.snapshot(second).sleepPlan.wakeReason == viewer::SleepWakeReason::Threat,
        "A living zombie inside the 60-metre boundary did not wake the sleeper");
}
void guardedWake(const char* map) {
    using namespace multiplayer;
    Options options; options.map = map; options.trees = options.forests = options.cars = false; options.zombieCount = 0;
    Scene scene(options); scene.dayNight = {12,1200}; scene.zombies.setPerceptionEnabled(false);
    World world(scene);
    const auto sleeper = world.join("Almos"), guard = world.join("Bandi"), otherGuard = world.join("Juli");
    Command command; command.sequence = 1;
    command.sleepAction = viewer::SleepAction{viewer::SleepActionKind::Suggest,0,240};
    world.submit(sleeper,Commands{{command}});
    for (int i = 0; i < 10 && !world.snapshot(sleeper).sleepPlan.proposer; ++i) world.step();
    command.sleepAction = viewer::SleepAction{viewer::SleepActionKind::Guard,world.snapshot(sleeper).sleepPlan.revision,30};
    world.submit(guard,Commands{{command}}); world.submit(otherGuard,Commands{{command}}); world.step();
    require(world.simulationRate() == 30, "Guards prevented shared fast-forward");
    const auto feet = world.players().at(guard).controller.feet();
    const auto eyes = world.players().at(guard).controller.eyePosition();
    glm::vec3 target{}; bool clear = false;
    for (int sector = 1; sector <= 16 && !clear; ++sector) {
        const float angle = glm::radians(float(sector) * 22.5f);
        target = eyes + 75.f * glm::vec3(std::sin(angle),0,-std::cos(angle));
        clear = scene.collision->lineOfSight(eyes,target);
    }
    require(clear,"Guard fixture needs a clear 75-metre sight line");
    viewer::AnimatedModel asset; viewer::SkeletonBone head; head.name = "head";
    head.referenceWorld = glm::translate(glm::mat4(1),glm::vec3(0,eyes.y-feet.y,0));
    asset.skeleton.push_back(head); asset.ragdollBones[std::size_t(viewer::RagdollPart::head)] = 0;
    viewer::ZombieInstance threat; threat.asset = scene.zombies.assets.size();
    scene.zombies.assets.push_back(std::move(asset));
    const auto zombieFeet = target - glm::vec3(0,eyes.y-feet.y,0);
    threat.restRoot = threat.restTransform = threat.transform = glm::translate(glm::mat4(1),zombieFeet);
    threat.locomotionVelocity = glm::normalize(feet-zombieFeet);
    scene.zombies.instances.push_back(threat); scene.zombies.rebuildSpatialIndex();
    command.sequence = 2; command.sleepAction.reset(); command.movement.forward = 1; command.movement.jump = true; command.left = true;
    world.submit(guard,Commands{{command}});
    for (int i = 0; i < 290; ++i) world.step();
    require(world.snapshot(sleeper).sleepPlan.sleeping && world.players().at(guard).controller.feet() == feet,
        "Guard moved during automatic watch or scanned before the five-second deadline");
    for (int i = 0; i < 15 && world.snapshot(sleeper).sleepPlan.sleeping; ++i) world.step();
    const auto plan = world.snapshot(sleeper).sleepPlan;
    require(!plan.sleeping && world.simulationRate() == 1 && plan.wakeReason == viewer::SleepWakeReason::GuardSpotted &&
        plan.guardAlert.guard == guard && plan.guardAlert.name == "Bandi" &&
        plan.guardAlert.bearing == viewer::sleepGuardBearing(target-eyes),
        "A guard's visible incoming threat did not wake the whole group with a named directional alert");
    require(world.snapshot(otherGuard).sleepPlan.guardAlert.name == "Bandi", "Other guards missed the shared wake alert");
    const auto angles = viewer::sleepGuardLookAngles(eyes,target);
    const auto state = world.snapshot(guard);
    const auto player = std::find_if(state.players.begin(),state.players.end(),[&](const auto& p) { return p.id == guard; });
    require(player != state.players.end() && std::abs(player->yaw-angles.x) < .001f && std::abs(player->pitch-angles.y) < .001f,
        "Spotting guard did not face the detected head on waking");
}
}
int main(int argc, char** argv) {
    try {
        calendar(); planning(); lifecycle(); wire();
        require(argc == 2, "Expected test map"); server(argv[1]); guardedWake(argv[1]);
        std::cout << "Sleep planning, calendar, protocol and server checks passed\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
