#include "sleep_drawer.hpp"
#include "hud.hpp"
#include "presentation_viewport.hpp"
#include <RmlUi/Core.h>
#include <algorithm>
#include <array>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <cmath>

namespace viewer {
namespace {
constexpr std::array<const char*, 12> controls{{"sleep-tab", "sleep-close", "sleep-less", "sleep-more",
    "sleep-suggest", "sleep-accept", "sleep-guard", "sleep-cancel", "sleep-notice-accept",
    "sleep-notice-guard", "sleep-notice-another", "sleep-roster"}};
std::string dayLabel(std::uint64_t day) { return "DAY " + std::to_string(day); }
}
SleepDrawer::SleepDrawer(Rml::ElementDocument& document) : document_(document) {
    for (const char* id : {"sleep-drawer", "sleep-panel", "sleep-tab", "sleep-close", "sleep-day", "sleep-clock",
        "sleep-duration", "sleep-less", "sleep-more", "sleep-wake", "sleep-suggest", "sleep-proposal", "sleep-roster",
        "sleep-accept", "sleep-guard", "sleep-cancel", "sleep-notice", "sleep-notice-text", "sleep-notice-wake",
        "sleep-notice-accept", "sleep-notice-guard", "sleep-notice-another", "sleep-rest-clock", "sleep-rest-day",
        "sleep-rest-remaining", "sleep-rest-rate", "sleep-rest-wake", "sleep-wake-reason", "sleep-wake-status"}) {
        auto* element = document.GetElementById(id);
        if (!element) throw std::runtime_error(std::string("Missing sleep control: ") + id);
        elements_.emplace(id, element);
    }
}
bool SleepDrawer::contains(const char* id) const {
    auto* element = elements_.at(id);
    const auto origin = element->GetAbsoluteOffset(Rml::BoxArea::Border);
    const auto size = element->GetBox().GetSize(Rml::BoxArea::Border);
    return pointer_.x >= origin.x && pointer_.x < origin.x + size.x &&
        pointer_.y >= origin.y && pointer_.y < origin.y + size.y;
}
void SleepDrawer::text(const char* id, const std::string& value) {
    if (text_[id] == value) return;
    text_[id] = value;
    elements_.at(id)->SetInnerRML(Rml::StringUtilities::EncodeRml(value));
}
void SleepDrawer::close() {
    open_ = false; pressed_.clear(); pointer_ = {-1,-1};
    document_.SetClass("sleep-expanded", false);
    elements_.at("sleep-drawer")->SetClass("expanded", false);
}
void SleepDrawer::update(const HudState& state) {
    const auto& plan = state.sleepPlan;
    const bool asleep = plan.asleep(state.localPlayer);
    document_.SetClass("sleeping", asleep);
    available_ = !asleep && state.localPlayer != 0 && state.screen == HudScreen::Gameplay && !state.commandWheelOpen;
    if (plan.revision != revision_) {
        revision_ = plan.revision;
        if (plan.proposer) minutes_ = plan.minutes;
        pressed_.clear();
    }
    if ((!available_ && !asleep) || state.mouseCaptured) close();
    if (asleep) open_ = false;
    notice_ = available_ && plan.proposer && plan.proposer != state.localPlayer &&
        plan.response(state.localPlayer) == SleepResponse::Pending && !open_;
    document_.SetClass("sleep-expanded", open_);
    elements_.at("sleep-drawer")->SetClass("available", available_);
    elements_.at("sleep-drawer")->SetClass("expanded", open_);
    elements_.at("sleep-tab")->SetClass("pending", notice_);
    elements_.at("sleep-notice")->SetClass("visible", notice_);
    const auto now = gameCalendarTime(state.dayNight, state.gameSeconds);
    text("sleep-rest-clock", now.clock); text("sleep-rest-day", dayLabel(now.day));
    const double remaining = state.dayNight.dayLengthSeconds > 0 ? std::max(0., plan.endsAt - state.gameSeconds) * 1440 / state.dayNight.dayLengthSeconds : 0;
    text("sleep-rest-remaining", sleepDurationLabel(static_cast<std::uint16_t>(std::ceil(remaining))) + " remaining");
    text("sleep-rest-rate", plan.rate() > 1 ? "Time passing · 30×" : "Your group is standing guard · normal time");
    const char* wakeMessage = "";
    if (!asleep && plan.endedAt >= 0 && state.gameSeconds >= plan.endedAt && state.gameSeconds < plan.endedAt + 8) {
        switch (plan.wakeReason) {
        case SleepWakeReason::Rested: wakeMessage = "For a little while, the world let you rest. That time is yours now."; break;
        case SleepWakeReason::Threat: wakeMessage = "Restless feet drag across the earth. Their shuffling leaves no room for dreams."; break;
        case SleepWakeReason::Damage: wakeMessage = "Pain tears you from your dreams. The world demands your attention."; break;
        case SleepWakeReason::Cancelled: wakeMessage = "The watch is over. It is time to open your eyes."; break;
        case SleepWakeReason::PlayerJoined: wakeMessage = "Radio: Attention, all survivors. Another pair of boots has hit the ground in the area of operations."; break;
        case SleepWakeReason::PlayerLeft: wakeMessage = "Radio: Attention, all survivors. One of your own has left the area of operations."; break;
        default: break;
        }
    }
    text("sleep-wake-reason", wakeMessage);
    text("sleep-wake-status", *wakeMessage ? "Sleep ended" : "");
    const auto wake = plan.sleeping ? gameCalendarTime(state.dayNight, plan.endsAt) : gameCalendarTime(state.dayNight, state.gameSeconds, minutes_);
    text("sleep-day", dayLabel(now.day));
    text("sleep-clock", now.clock);
    text("sleep-duration", sleepDurationLabel(minutes_));
    text("sleep-wake", "Until " + wake.clock + (wake.day != now.day ? " · " + dayLabel(wake.day) : "") + " · 24-hour time");
    text("sleep-suggest", plan.proposer ? "Suggest another duration" : "Suggest sleep");
    elements_.at("sleep-suggest")->SetClass("disabled", plan.sleeping || state.dayNight.dayLengthSeconds == 0 || (plan.proposer && minutes_ == plan.minutes));
    elements_.at("sleep-less")->SetClass("disabled", plan.sleeping || minutes_ == minimumSleepMinutes);
    elements_.at("sleep-more")->SetClass("disabled", plan.sleeping || minutes_ == maximumSleepMinutes);
    const auto ownResponse = plan.response(state.localPlayer);
    for (const auto* id : {"sleep-accept", "sleep-guard", "sleep-cancel"})
        elements_.at(id)->SetClass("disabled", !plan.proposer);
    elements_.at("sleep-accept")->SetClass("selected", plan.proposer && ownResponse == SleepResponse::Accept);
    elements_.at("sleep-guard")->SetClass("selected", plan.proposer && ownResponse == SleepResponse::Guard);
    text("sleep-accept", plan.sleeping ? "Join sleep" : ownResponse == SleepResponse::Accept && plan.proposer ? "Accepted" : "Accept");
    text("sleep-guard", ownResponse == SleepResponse::Guard && plan.proposer ? "On guard" : "Stand guard");
    text("sleep-cancel", plan.sleeping ? "Wake everyone" : plan.proposer == state.localPlayer ? "Cancel proposal" : "Withdraw response");
    const bool allGuarding = plan.proposer && std::all_of(plan.participants.begin(), plan.participants.end(),
        [](const auto& member) { return member.response == SleepResponse::Guard; });
    text("sleep-proposal", state.dayNight.dayLengthSeconds == 0 ? "The game clock is paused" : plan.sleeping ? "Keeping watch · " + sleepDurationLabel(plan.minutes) : !plan.proposer ? "Plan some rest together" : allGuarding ? "Everyone chose to stand guard" :
        (plan.ready() ? "Plan agreed · " : "Proposed · ") + sleepDurationLabel(plan.minutes));
    std::ostringstream roster;
    if (!plan.proposer) roster << "<div class=\"sleep-member\">Choose 30 minutes to 12 hours.</div>"
        "<div class=\"sleep-member\">Your group can accept or stand guard.</div>";
    else for (const auto& member : plan.participants) {
        const auto css = member.response == SleepResponse::Accept ? "accept" : member.response == SleepResponse::Guard ? "guard" : "pending";
        roster << "<div class=\"sleep-member " << css << "\">" << (member.player == state.localPlayer ? "You" : "Player " + std::to_string(member.player))
            << (member.player == plan.proposer ? " · proposer" : "") << "<span>"
            << (member.response == SleepResponse::Accept ? "Accepted" : member.response == SleepResponse::Guard ? "On guard" : "Awaiting reply")
            << "</span></div>";
    }
    if (text_["sleep-roster"] != roster.str()) {
        text_["sleep-roster"] = roster.str(); elements_.at("sleep-roster")->SetInnerRML(roster.str());
    }
    const auto proposalWake = gameCalendarTime(state.dayNight, state.gameSeconds, plan.minutes);
    text("sleep-notice-text", "Player " + std::to_string(plan.proposer) + " suggests " + sleepDurationLabel(plan.minutes));
    text("sleep-notice-wake", "Rest until " + proposalWake.clock + " · " + dayLabel(proposalWake.day));
}
bool SleepDrawer::activate(const std::string& id, const HudState& state) {
    if (id == "sleep-tab") { open_ = !open_; return true; }
    if (id == "sleep-close") { open_ = false; return true; }
    if (id == "sleep-less") { if (!state.sleepPlan.sleeping) minutes_ = std::max<int>(minimumSleepMinutes, minutes_ - 30); return true; }
    if (id == "sleep-more") { if (!state.sleepPlan.sleeping) minutes_ = std::min<int>(maximumSleepMinutes, minutes_ + 30); return true; }
    if (id == "sleep-notice-another") { open_ = true; return true; }
    SleepActionKind kind;
    if (id == "sleep-suggest") {
        if (state.sleepPlan.sleeping || state.dayNight.dayLengthSeconds == 0) return true;
        if (state.sleepPlan.proposer && minutes_ == state.sleepPlan.minutes) return true;
        kind = SleepActionKind::Suggest;
    }
    else if (id == "sleep-accept" || id == "sleep-notice-accept") kind = SleepActionKind::Accept;
    else if (id == "sleep-guard" || id == "sleep-notice-guard") kind = SleepActionKind::Guard;
    else if (id == "sleep-cancel") kind = SleepActionKind::Cancel;
    else return false;
    if (kind != SleepActionKind::Suggest && !state.sleepPlan.proposer) return true;
    action_ = SleepAction{kind, state.sleepPlan.revision, minutes_};
    return true;
}
std::vector<HudPointerEvent> SleepDrawer::process(const HudState& state, const std::vector<HudPointerEvent>& events,
    int width, int height, const PresentationViewport& presentation) {
    // These clicks were queued over the previous frame. If a counterproposal
    // arrived meanwhile, display it before accepting any new consent.
    const bool proposalChanged = state.sleepPlan.revision != revision_;
    update(state);
    std::vector<HudPointerEvent> remaining;
    for (const auto& event : events) {
        if (event.action == HudPointerAction::Cancel) { close(); remaining.push_back(event); continue; }
        if (state.sleepPlan.asleep(state.localPlayer)) {
            if (event.action == HudPointerAction::Move) pointer_ = presentation.scenePointer(event.x,event.y,width,height);
            if (!state.mouseCaptured && event.action == HudPointerAction::Press && contains("sleep-rest-wake")) pressed_ = "wake";
            if (event.action == HudPointerAction::Release && std::exchange(pressed_, {}) == "wake" && contains("sleep-rest-wake"))
                action_ = SleepAction{SleepActionKind::Cancel, state.sleepPlan.revision, minutes_};
            continue;
        }
        if (!available_ || state.mouseCaptured) { remaining.push_back(event); continue; }
        if (event.action == HudPointerAction::Move) {
            pointer_ = presentation.scenePointer(event.x, event.y, width, height);
            remaining.push_back(event);
            continue;
        }
        const bool inside = contains("sleep-tab") || (open_ && contains("sleep-panel")) || (notice_ && contains("sleep-notice"));
        if (event.action == HudPointerAction::Press && inside) {
            pressed_ = "panel";
            for (const char* id : controls) {
                const bool notificationControl = std::string(id).find("sleep-notice-") == 0;
                if (!proposalChanged && (notificationControl ? notice_ : (open_ || std::string(id) == "sleep-tab")) && contains(id)) {
                    pressed_ = id; break;
                }
            }
            continue;
        }
        if (event.action == HudPointerAction::Release && !pressed_.empty()) {
            const auto pressed = std::exchange(pressed_, {});
            if (pressed != "panel" && contains(pressed.c_str())) activate(pressed, state);
            update(state);
            continue;
        }
        if (event.action == HudPointerAction::Wheel && open_ && contains("sleep-roster")) {
            auto* roster = elements_.at("sleep-roster");
            roster->SetScrollTop(roster->GetScrollTop() - event.x * 24 * document_.GetContext()->GetDensityIndependentPixelRatio());
            continue;
        }
        if (inside) continue; // Never leak a sleep click or wheel into equipment.
        if (event.action == HudPointerAction::Press && open_) { open_ = false; update(state); }
        remaining.push_back(event);
    }
    for (const char* id : controls) {
        const bool notificationControl = std::string(id).find("sleep-notice-") == 0;
        elements_.at(id)->SetClass("hovered", available_ && !state.mouseCaptured &&
            (notificationControl ? notice_ : (open_ || std::string(id) == "sleep-tab")) && contains(id));
    }
    return remaining;
}
std::optional<SleepAction> SleepDrawer::takeAction() { return std::exchange(action_, {}); }
} // namespace viewer
