/**
 * @file core/service_slot.h
 * @brief Lightweight injectable runtime service holder with explicit reset.
 *
 * Replaces immortal function-local singletons so plugin shutdown can destroy
 * services in deterministic order and tests can substitute isolated instances
 * with RAII cleanup.
 */
#pragma once

#include <memory>
#include <utility>

namespace CanvasCore {

template <typename T>
class ServiceSlot {
  public:
    class ScopedOverride {
      public:
        ScopedOverride(ServiceSlot& slot, T* replacement)
            : slot_(&slot), previous_(std::exchange(slot.override_, replacement)) {}

        ScopedOverride(const ScopedOverride&) = delete;
        ScopedOverride& operator=(const ScopedOverride&) = delete;

        ScopedOverride(ScopedOverride&& other) noexcept
            : slot_(std::exchange(other.slot_, nullptr)),
              previous_(std::exchange(other.previous_, nullptr)) {}

        ScopedOverride& operator=(ScopedOverride&& other) noexcept {
            if (this == &other)
                return *this;
            restore();
            slot_ = std::exchange(other.slot_, nullptr);
            previous_ = std::exchange(other.previous_, nullptr);
            return *this;
        }

        ~ScopedOverride() {
            restore();
        }

      private:
        void restore() {
            if (slot_) {
                slot_->override_ = previous_;
                slot_ = nullptr;
            }
        }

        ServiceSlot* slot_ = nullptr;
        T*           previous_ = nullptr;
    };

    T& get() {
        if (override_)
            return *override_;
        if (!owned_)
            owned_ = std::make_unique<T>();
        return *owned_;
    }

    bool hasOwnedInstance() const {
        return static_cast<bool>(owned_);
    }

    void reset() {
        owned_.reset();
    }

    [[nodiscard]] ScopedOverride overrideWith(T& replacement) {
        return ScopedOverride(*this, &replacement);
    }

  private:
    std::unique_ptr<T> owned_;
    T*                 override_ = nullptr;
};

} // namespace CanvasCore
