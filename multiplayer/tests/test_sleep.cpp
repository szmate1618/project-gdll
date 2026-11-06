#include "server/world.hpp"
#include "client/snapshots.hpp"
#include "sleep_plan.hpp"
#include "simulation_clock.hpp"
#include <glm/gtc/matrix_transform.hpp>
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
    require(plan.begin(10, {12,1200}) && plan.state().asleep(1) && !plan.state().asleep(2) && plan.state().rate() == 1,
        "A guard must stay awake at normal simulation speed");
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
    require(!plan.state().sleeping && plan.state().wakeReason == SleepWakeReason::MembershipChanged, "Joining a session did not end acceleration");
    FPSController player; auto movement = player.movementState();
    movement.feet = {4,0,8}; movement.exertion.strain = 1; movement.exertion.stamina = .1f;
    player.restoreMovement(movement);
    player.restSleeping(400,8);
    require(player.feet() == movement.feet && player.exertion().strain == 0 && player.exertion().stamina == 1,
        "Sleep recovery must restore exertion without moving or healing the player");
}
void wire() {
    using namespace multiplayer;
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
    require(world.snapshot(first).sleepPlan.sleeping && world.simulationRate() == 1, "Guarded sleep must start at normal speed");
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
}
int main(int argc, char** argv) {
    try {
        calendar(); planning(); lifecycle(); wire();
        require(argc == 2, "Expected test map"); server(argv[1]);
        std::cout << "Sleep planning, calendar, protocol and server checks passed\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
