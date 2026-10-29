#pragma once
#include "scene.hpp"
#include <functional>
#include <optional>
struct GLFWwindow;
namespace multiplayer {
// Called on the window thread during loading/connection. False cancels startup.
using SessionFeedback = std::function<bool(const std::string&)>;
int runClientSession(const Options& options, GLFWwindow* window = nullptr,
                     const SessionFeedback& feedback = {}, const std::function<void()>& checkServer = {},
                     const std::function<std::optional<double>()>& serverSimulationMilliseconds = {});
}
