#include "lighting/event_queue.h"

namespace lighting {

void event_queue::push(const flash_event& ev) {
    std::lock_guard<std::mutex> lk(mu_);
    events_.push_back(ev);
}

void event_queue::drain(float delta_ms, std::vector<flash_event>& out) {
    std::lock_guard<std::mutex> lk(mu_);
    for (auto& ev : events_) { ev.elapsed_ms += delta_ms; }
    // Partition: keep live events in events_, move them to out as well.
    // Live events stay in the internal buffer for next-frame aging;
    // out receives a snapshot for the current SSBO build.
    std::vector<flash_event> survivors;
    survivors.reserve(events_.size());
    for (const auto& ev : events_) {
        if (ev.elapsed_ms < ev.duration_ms) {
            survivors.push_back(ev);
            out.push_back(ev);
        }
    }
    events_ = std::move(survivors);
}

} // namespace lighting
