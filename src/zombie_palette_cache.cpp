#include "zombie_palette_cache.hpp"

#include <algorithm>
#include <stdexcept>

namespace viewer {
static_assert(sizeof(glm::mat4) == 4 * sizeof(glm::vec4), "Palette texture requires packed matrix columns");

ZombiePaletteCache::ZombiePaletteCache(std::size_t instances, std::size_t bones, std::size_t maximumTexels)
    : bones_(bones), maximumSlots_(bones ? std::min({instances, maximumTexels / 4 / bones,
                                                   (std::size_t(1) << 24) / bones}) : 0),
      instanceSlots_(instances, noSlot) {}

ZombiePaletteCache::~ZombiePaletteCache() {
    glDeleteTextures(1, &texture_);
    glDeleteBuffers(1, &buffer_);
}

void ZombiePaletteCache::release(std::size_t instance) {
    auto& slot = instanceSlots_.at(instance);
    if (slot == noSlot) return;
    slots_[slot] = {}; // Reuse must overwrite even if the new revision is equal.
    freeSlots_.push_back(slot);
    slot = noSlot;
}

void ZombiePaletteCache::beginFrame() {
    if (!dirtySlots_.empty()) throw std::logic_error("Unflushed zombie palette frame");
    stats_ = {};
}

float ZombiePaletteCache::request(std::size_t instance, std::uint64_t revision,
                                 const std::vector<glm::mat4>& palette) {
    if (!bones_ || palette.size() != bones_) throw std::invalid_argument("Invalid zombie palette size");
    auto& index = instanceSlots_.at(instance);
    if (index == noSlot) {
        if (freeSlots_.empty()) {
            if (slots_.size() == maximumSlots_) throw std::runtime_error("Zombie palettes exceed GPU buffer capacity");
            index = slots_.size();
            slots_.push_back({});
        } else {
            index = freeSlots_.back();
            freeSlots_.pop_back();
        }
    }
    auto& slot = slots_[index];
    ++stats_.needed;
    if (!slot.uploaded || slot.revision != revision) {
        if (!slot.pending) dirtySlots_.push_back(index);
        slot.pending = &palette;
        slot.pendingRevision = revision;
    }
    return static_cast<float>(index * bones_);
}

void ZombiePaletteCache::grow(std::size_t required) {
    if (required <= capacity_) return;
    const auto capacity = std::min(maximumSlots_, std::max(required, std::max(std::size_t(8), capacity_ * 2)));
    GLuint replacement = 0;
    glGenBuffers(1, &replacement);
    glBindBuffer(GL_COPY_WRITE_BUFFER, replacement);
    glBufferData(GL_COPY_WRITE_BUFFER, static_cast<GLsizeiptr>(capacity * bones_ * sizeof(glm::mat4)),
                 nullptr, GL_DYNAMIC_DRAW);
    if (buffer_) {
        // Preserve hidden/settled slots on the GPU; growing does not force their
        // CPU palettes to be uploaded again. GL orders the copy before draws.
        glBindBuffer(GL_COPY_READ_BUFFER, buffer_);
        glCopyBufferSubData(GL_COPY_READ_BUFFER, GL_COPY_WRITE_BUFFER, 0, 0,
                            static_cast<GLsizeiptr>(capacity_ * bones_ * sizeof(glm::mat4)));
    }
    if (!texture_) glGenTextures(1, &texture_);
    glBindTexture(GL_TEXTURE_BUFFER, texture_);
    glTexBuffer(GL_TEXTURE_BUFFER, GL_RGBA32F, replacement);
    glDeleteBuffers(1, &buffer_);
    buffer_ = replacement;
    capacity_ = capacity;
    glBindBuffer(GL_COPY_READ_BUFFER, 0);
    glBindBuffer(GL_COPY_WRITE_BUFFER, 0);
    glBindTexture(GL_TEXTURE_BUFFER, 0);
}

void ZombiePaletteCache::flush() {
    if (!dirtySlots_.empty()) {
        grow(slots_.size());
        glBindBuffer(GL_TEXTURE_BUFFER, buffer_);
        std::sort(dirtySlots_.begin(), dirtySlots_.end());
        for (std::size_t begin = 0; begin < dirtySlots_.size();) {
            auto end = begin + 1;
            while (end < dirtySlots_.size() && dirtySlots_[end] == dirtySlots_[end - 1] + 1) ++end;
            scratch_.clear();
            scratch_.reserve((end - begin) * bones_);
            for (auto i = begin; i < end; ++i) {
                const auto& slot = slots_[dirtySlots_[i]];
                scratch_.insert(scratch_.end(), slot.pending->begin(), slot.pending->end());
            }
            const auto bytes = scratch_.size() * sizeof(glm::mat4);
            glBufferSubData(GL_TEXTURE_BUFFER,
                            static_cast<GLintptr>(dirtySlots_[begin] * bones_ * sizeof(glm::mat4)),
                            static_cast<GLsizeiptr>(bytes), scratch_.data());
            ++stats_.uploadCalls;
            stats_.uploadBytes += bytes;
            begin = end;
        }
        stats_.uploaded = dirtySlots_.size();
        for (const auto index : dirtySlots_) {
            auto& slot = slots_[index];
            slot.uploaded = true;
            slot.revision = slot.pendingRevision;
            slot.pending = nullptr;
        }
        dirtySlots_.clear();
        glBindBuffer(GL_TEXTURE_BUFFER, 0);
    }
    stats_.resident = slots_.size() - freeSlots_.size();
    stats_.storageBytes = capacity_ * bones_ * sizeof(glm::mat4);
}

} // namespace viewer
