#pragma once

namespace pet {

class PetIdleEvents {
public:
    static PetIdleEvents &instance() noexcept;

    void init() noexcept;
    void tick(int current_age_ticks) noexcept;

private:
    PetIdleEvents() = default;

    int next_event_tick_ = 0;
    int age_at_last_event_ = 0;
};

} // namespace pet
