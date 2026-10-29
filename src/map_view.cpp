#include "map_view.hpp"
#include <algorithm>
#include <cmath>

namespace viewer {
namespace {
bool contains(glm::dvec2 p, glm::dvec2 offset, glm::dvec2 size) {
    return p.x >= offset.x && p.y >= offset.y && p.x <= offset.x + size.x && p.y <= offset.y + size.y;
}
}
void MapView::setSource(const std::optional<MinimapSource>& source) {
    clearMeasurement(); source_ = source; zoom_ = 1; center_ = glm::dvec2(.5);
    if (source_) worldFromUv_ = glm::inverse(source_->uvFromWorld);
}
void MapView::setViewport(glm::dvec2 offset, glm::dvec2 size, double density) {
    if (offset != offset_ || size != size_) {
        // A resize must not turn an in-progress pointer gesture into a click.
        if (editing_) points_[*editing_] = originalPoint_;
        editing_.reset(); pressed_ = dragging_ = false;
    }
    offset_ = offset; size_ = glm::max(size, glm::dvec2(1)); density_ = std::max(.1, density);
    clampCenter();
}
glm::dvec2 MapView::imageSize() const {
    if (!source_) return glm::dvec2(1);
    const glm::dvec2 pixels(source_->imageSize);
    return pixels * (std::min(size_.x / pixels.x, size_.y / pixels.y) * zoom_);
}
glm::dvec2 MapView::imageOffset() const { return offset_ + size_ * .5 - center_ * imageSize(); }
void MapView::clampCenter() {
    const auto dimensions = imageSize();
    for (int i = 0; i < 2; ++i) {
        const double half = std::min(.5, size_[i] / (2 * dimensions[i]));
        center_[i] = std::clamp(center_[i], half, 1 - half);
    }
}
std::optional<MapView::ImageRegion> MapView::imageRegion() const {
    if (!source_) return {};
    const auto origin = imageOffset(), dimensions = imageSize();
    const auto low = glm::max(offset_, origin), high = glm::min(offset_ + size_, origin + dimensions);
    return ImageRegion{low, high - low, (low - origin) / dimensions, (high - low) / dimensions};
}
std::optional<glm::dvec2> MapView::worldAt(glm::dvec2 screen) const {
    if (!source_ || !contains(screen, offset_, size_)) return {};
    const auto uv = (screen - imageOffset()) / imageSize();
    if (!contains(uv, glm::dvec2(0), glm::dvec2(1))) return {};
    return glm::dvec2(worldFromUv_ * glm::dvec3(uv, 1));
}
glm::dvec2 MapView::screenAt(glm::dvec2 world) const {
    return imageOffset() + glm::dvec2(source_->uvFromWorld * glm::dvec3(world, 1)) * imageSize();
}
std::optional<MapView::Marker> MapView::marker(glm::dvec2 world, double yaw) const {
    const auto region = imageRegion();
    if (!region || !std::isfinite(world.x) || !std::isfinite(world.y) || !std::isfinite(yaw)) return {};
    const auto margin = glm::min(glm::dvec2(13 * density_), region->size * .25);
    const auto low = region->offset + margin, high = region->offset + region->size - margin;
    auto position = screenAt(world);
    const double angle = glm::radians(yaw);
    auto direction = screenAt(world + glm::dvec2(std::cos(angle), std::sin(angle))) - position;
    const bool edge = !contains(position, low, high - low);
    if (edge) {
        const auto middle = (low + high) * .5;
        direction = position - middle;
        double t = 1;
        for (int i = 0; i < 2; ++i)
            if (std::abs(direction[i]) > 1e-12) t = std::min(t, (direction[i] > 0 ? high[i] - middle[i] : low[i] - middle[i]) / direction[i]);
        position = middle + direction * t;
    }
    const double magnitude = glm::length(direction);
    if (magnitude < 1e-12) return {};
    return Marker{position, direction / magnitude, edge};
}
void MapView::zoomAt(glm::dvec2 screen, double notches) {
    if (!worldAt(screen) || !std::isfinite(notches)) return;
    const auto uv = (screen - imageOffset()) / imageSize();
    zoom_ = std::clamp(zoom_ * std::pow(1.25, std::clamp(notches, -32., 32.)), 1., 32.);
    center_ = uv - (screen - offset_ - size_ * .5) / imageSize();
    clampCenter();
    // A wheel turn during a press cannot become an extra measurement point.
    if (pressed_) dragging_ = true;
}
void MapView::centerOn(glm::dvec2 world) {
    if (!source_) return;
    center_ = glm::dvec2(source_->uvFromWorld * glm::dvec3(world, 1)); clampCenter();
}
void MapView::beginMeasurement(std::optional<glm::dvec2> start) {
    cancel(); points_.clear(); measuring_ = bool(source_);
    if (measuring_ && start) {
        const auto uv = glm::dvec2(source_->uvFromWorld * glm::dvec3(*start, 1));
        if (contains(uv, glm::dvec2(0), glm::dvec2(1))) points_.push_back(*start);
    }
}
void MapView::clearMeasurement() { cancel(); points_.clear(); measuring_ = false; }
void MapView::move(glm::dvec2 screen) {
    pointer_ = screen;
    if (!pressed_) return;
    if (glm::length(screen - press_) >= 4 * density_) dragging_ = true;
    if (dragging_) {
        if (editing_) {
            if (const auto world = worldAt(screen)) points_[*editing_] = *world;
        } else { center_ -= (screen - previous_) / imageSize(); clampCenter(); }
    }
    previous_ = screen;
}
void MapView::press() {
    if (!worldAt(pointer_)) return;
    pressed_ = true; dragging_ = false; press_ = previous_ = pointer_;
    if (!measuring_) for (std::size_t i = 0; i < points_.size(); ++i)
        if (glm::length(screenAt(points_[i]) - pointer_) <= 12 * density_) {
            editing_ = i; originalPoint_ = points_[i]; break;
        }
}
bool MapView::finishing() const {
    return measuring_ && points_.size() >= 2 && worldAt(pointer_) &&
        glm::length(screenAt(points_.back()) - pointer_) <= 12 * density_;
}
void MapView::release() {
    if (pressed_ && !dragging_ && measuring_) {
        if (finishing()) measuring_ = false;
        else if (const auto world = worldAt(pointer_)) {
            if (points_.empty() || glm::length(screenAt(points_.back()) - pointer_) > 2 * density_)
                points_.push_back(*world);
        }
    }
    editing_.reset(); pressed_ = dragging_ = false;
}
void MapView::undo() {
    if (editing_) points_[*editing_] = originalPoint_;
    editing_.reset(); pressed_ = dragging_ = false;
    if (!measuring_) return;
    if (!points_.empty()) points_.pop_back();
    if (points_.empty()) measuring_ = false;
}
void MapView::cancel() {
    if (editing_) points_[*editing_] = originalPoint_;
    editing_.reset(); pressed_ = dragging_ = false;
    if (measuring_) points_.clear();
    measuring_ = false;
}
std::optional<glm::dvec2> MapView::preview() const {
    if (!measuring_ || points_.empty() || pressed_ || finishing()) return {};
    return worldAt(pointer_);
}
double MapView::distance() const {
    double result = 0;
    for (std::size_t i = 1; i < points_.size(); ++i) result += glm::length(points_[i] - points_[i-1]);
    return result;
}
} // namespace viewer
