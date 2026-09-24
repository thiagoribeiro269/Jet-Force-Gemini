// Session-owned event/timer loop. The controller pumps it while guest threads
// are quiescent; no detached timer thread can outlive the caller's RAM.
#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <limits>
#include <map>
#include <thread>
#include "events.h"
#include "scheduler.h"

namespace {
constexpr uint64_t ticks_per_second = 46875000;
constexpr uint32_t ram_base = 0x80000000u, ram_end = 0x80800000u;
using Clock = std::chrono::steady_clock;
struct Route { uint32_t queue = 0, message = 0; };
struct Timer { uint64_t due, interval, order; Route route; };
struct Events {
    uint8_t* ram = nullptr;
    uint32_t table = 0;
    bool deterministic = false, manager = false, black = false, pumping = false;
    uint64_t ticks = 0, time_offset = 0, last_ticks = 0;
    Clock::time_point start;
    std::array<Route, 15> routes{};
    std::map<uint32_t, Timer> timers;
    uint64_t timer_order = 0;
    Route vi;
    uint32_t vi_mode = 0, retrace_divisor = 1, retraces_left = 1, int_mask = 0x003FFF01u;
    uint32_t vi_control = 0, vi_features = 0;
    uint64_t vi_period = ticks_per_second / 60, vi_due = 0;
    uint64_t timer_firings = 0, vi_fields = 0, delivered = 0, dropped = 0, posts = 0;
} events;

bool range(uint32_t p, uint32_t bytes, uint32_t alignment = 4) {
    return !(p % alignment) && p >= ram_base && p < ram_end && bytes <= ram_end - p;
}
bool access(uint8_t* ram, bool owner = false) {
    return ram && ram == events.ram && jfg_threads_context(ram, owner);
}
void word(uint32_t address, uint32_t value) { std::memcpy(events.ram + address - ram_base, &value, 4); }
void doubleword(uint32_t address, uint64_t value) { word(address, uint32_t(value >> 32)); word(address + 4, uint32_t(value)); }
uint64_t now() {
    if (events.deterministic) return events.ticks;
    uint64_t ns = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - events.start).count();
    return (ns / 1000000000) * ticks_per_second + ((ns % 1000000000) * ticks_per_second) / 1000000000;
}
bool queue_ok(uint8_t* ram, uint32_t queue) { return !queue || jfg_threads_queue_known(ram, queue); }
int send(Route route) {
    if (!route.queue) return 0;
    int32_t result;
    if (jfg_threads_queue(events.ram, 1, route.queue, route.message, 0, &result)) return -1;
    if (result == 0) events.delivered++;
    else if (result == -1) events.dropped++;
    else return -1;
    return 0;
}
uint64_t next_due() {
    uint64_t due = events.manager && events.vi_mode ? events.vi_due : std::numeric_limits<uint64_t>::max();
    for (const auto& [address, timer] : events.timers) if (timer.due < due) due = timer.due;
    return due;
}
int pump() {
    if (events.pumping) return -1;
    events.pumping = true;
    struct Finish { ~Finish() { events.pumping = false; } } finish;
    // A guest callback can modify timer registration after delivery resumes it.
    // Copy the route and update/remove the timer before any guest can run.
    for (uint32_t budget = 0; budget < 4096; budget++) {
        if (events.timers.empty() && !(events.manager && events.vi_mode)) return 0;
        uint64_t current = now(), due = next_due();
        events.last_ticks = current;
        if (due > current) return 0;
        Route route;
        if (events.manager && events.vi_mode && events.vi_due == due) {
            if (current > UINT64_MAX - events.vi_period) return -1;
            events.vi_due = current + events.vi_period;
            events.vi_fields++;
            if (--events.retraces_left == 0) {
                events.retraces_left = events.retrace_divisor;
                route = events.vi;
            }
        } else {
            auto found = events.timers.end();
            for (auto it = events.timers.begin(); it != events.timers.end(); ++it)
                if (it->second.due == due && (found == events.timers.end() || it->second.order > found->second.order)) found = it;
            if (found == events.timers.end()) return -1;
            Timer timer = found->second;
            route = timer.route;
            events.timer_firings++;
            if (timer.interval) {
                if (current > UINT64_MAX - timer.interval) return -1;
                found->second.due = current + timer.interval;
                found->second.order = ++events.timer_order;
                doubleword(found->first + 16, found->second.due);
            } else {
                events.timers.erase(found);
            }
        }
        if (send(route)) return -1;
    }
    return -5; // Caller must service remaining work; never silently discard it.
}
} // namespace

int jfg_events_begin(uint8_t* ram, uint32_t table, int deterministic) {
    if (events.ram || !jfg_threads_context(ram, 1) || !range(table, 15 * 8) ||
        (deterministic != 0 && deterministic != 1)) return -1;
    events = Events{};
    events.ram = ram;
    events.table = table;
    events.deterministic = deterministic != 0;
    events.start = Clock::now();
    std::memset(ram + table - ram_base, 0, 15 * 8);
    return 0;
}

int jfg_events_end(uint8_t* ram) {
    if (!events.ram) return 0;
    if (!access(ram, true) || events.pumping) return -1;
    events.last_ticks = now();
    events.timers.clear();
    events.routes = {};
    events.vi = {};
    events.manager = false;
    events.ram = nullptr;
    return 0;
}

int jfg_events_bind(uint8_t* ram, uint32_t event, uint32_t queue, uint32_t message) {
    if (!access(ram) || event >= events.routes.size() || !queue_ok(ram, queue)) return -1;
    events.routes[event] = {queue, message};
    word(events.table + event * 8, queue);
    word(events.table + event * 8 + 4, message);
    return 0;
}

int jfg_events_post(uint8_t* ram, uint32_t event) {
    if (!access(ram, true) || event >= events.routes.size()) return -1;
    events.posts++;
    return send(events.routes[event]);
}

int jfg_events_vi_manager(uint8_t* ram, int32_t priority) {
    if (!access(ram) || priority < 0 || priority > 255) return -1;
    events.manager = true;
    return 0;
}
int jfg_events_vi_mode(uint8_t* ram, uint32_t mode) {
    if (!access(ram) || !events.manager || !range(mode, 80)) return -1;
    uint8_t type = ram[(mode - ram_base) ^ 3];
    if (type >= 42) return -2;
    uint64_t period = ticks_per_second / (type >= 14 && type < 28 ? 50 : 60);
    if (now() > UINT64_MAX - period) return -1;
    events.vi_mode = mode;
    std::memcpy(&events.vi_control, ram + mode - ram_base + 4, 4);
    events.vi_features = 0;
    events.vi_period = period;
    events.vi_due = now() + period;
    return 0;
}
int jfg_events_vi_black(uint8_t* ram, uint32_t black) {
    if (!access(ram) || !events.manager || black > 1) return -1;
    events.black = black != 0;
    return 0;
}
int jfg_events_vi_features(uint8_t* ram, uint32_t features) {
    if (!access(ram) || !events.manager || !events.vi_mode) return -1;
    uint32_t control = events.vi_control;
    if (features & 1) control |= 8;
    if (features & 2) control &= ~8u;
    if (features & 4) control |= 4;
    if (features & 8) control &= ~4u;
    if (features & 16) control |= 16;
    if (features & 32) control &= ~16u;
    if (features & 64) { control |= 0x10000; control &= ~0x300u; }
    if (features & 128) {
        uint32_t original;
        std::memcpy(&original, ram + events.vi_mode - ram_base + 4, 4);
        control = (control & ~0x10000u) | (original & 0x300u);
    }
    events.vi_control = control;
    events.vi_features = features & 0xFFu;
    return 0;
}
int jfg_events_vi_state(uint32_t* fields, size_t count) {
    if (!fields || count != 4 || (events.ram && !access(events.ram, true))) return -1;
    std::array<uint32_t, 4> values{events.vi_mode, events.vi_control, events.vi_features, events.black};
    std::memcpy(fields, values.data(), sizeof(values));
    return 0;
}
int jfg_events_vi_bind(uint8_t* ram, uint32_t queue, uint32_t message, uint32_t retraces) {
    if (!access(ram) || !events.manager || !queue_ok(ram, queue) || !retraces || retraces > 255) return -1;
    events.vi = {queue, message};
    events.retrace_divisor = events.retraces_left = retraces;
    return 0;
}

int jfg_events_clock(uint8_t* ram, int operation, uint64_t value, uint64_t* result) {
    if (!access(ram) || !result || operation < 0 || operation > 2) return -1;
    uint64_t ticks = now();
    if (operation == 2) events.time_offset = value - ticks;
    *result = operation == 0 ? ticks : ticks + events.time_offset;
    return 0;
}

int jfg_events_timer(uint8_t* ram, uint32_t timer, uint64_t countdown, uint64_t interval,
                      uint32_t queue, uint32_t message) {
    if (!access(ram) || !range(timer, 32, 8) || !queue_ok(ram, queue) ||
        events.timers.size() >= 256 || events.timers.contains(timer)) return -1;
    uint64_t ticks = now(), delay = countdown ? countdown : interval;
    if (ticks > UINT64_MAX - delay) return -1;
    events.timers.emplace(timer, Timer{ticks + delay, interval, ++events.timer_order, {queue, message}});
    word(timer, 0); word(timer + 4, 0);
    doubleword(timer + 8, interval); doubleword(timer + 16, ticks + delay);
    word(timer + 24, queue); word(timer + 28, message);
    return 0;
}
int jfg_events_cancel_timer(uint8_t* ram, uint32_t timer) {
    if (!access(ram, true) || !range(timer, 32, 8)) return -1;
    return int(events.timers.erase(timer));
}

int jfg_events_advance(uint8_t* ram, uint64_t ticks) {
    if (!access(ram, true) || !events.deterministic || events.ticks > UINT64_MAX - ticks) return -1;
    events.ticks += ticks;
    return pump();
}
int jfg_events_wait(uint8_t* ram, uint32_t max_ms) {
    if (!access(ram, true) || events.deterministic || max_ms > 1000) return -1;
    uint64_t current = now(), due = next_due();
    if (due > current) {
        uint64_t wait_ticks = std::min(due - current, uint64_t(max_ms) * ticks_per_second / 1000);
        uint64_t micros = (wait_ticks * 1000000 + ticks_per_second - 1) / ticks_per_second;
        std::this_thread::sleep_for(std::chrono::microseconds(micros));
    }
    return pump();
}
int jfg_events_mask(uint8_t* ram, uint32_t value, uint32_t* previous) {
    if (!access(ram) || !previous) return -1;
    *previous = events.int_mask;
    events.int_mask = value;
    return 0;
}

int jfg_events_state(uint64_t* fields, size_t count) {
    if (!fields || count != 16) return -1;
    if (events.ram && !jfg_threads_context(events.ram, 1)) return -1;
    std::array<uint64_t, 16> result{events.ram != nullptr, events.timers.size(), events.timer_firings,
        events.vi_fields, events.delivered, events.dropped, events.posts, events.manager,
        events.vi_mode, events.black, events.retrace_divisor, events.int_mask,
        events.ram ? now() : events.last_ticks, events.deterministic, events.vi_period, 0};
    std::memcpy(fields, result.data(), sizeof(result));
    return 0;
}
