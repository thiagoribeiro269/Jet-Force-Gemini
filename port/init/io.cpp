// Read-only cart PI transport, serviced by the session controller.
#include <array>
#include <cstring>
#include <map>
#include "io.h"
#include "scheduler.h"

namespace {
constexpr uint32_t base = 0x80000000, end = 0x80800000;
struct Request { uint32_t source, destination, size, return_queue, device_address, priority; };
struct Io {
    uint8_t* ram = nullptr;
    uint32_t command_queue = 0;
    bool pumping = false;
    std::map<uint32_t, Request> requests;
    uint64_t submitted = 0, completed = 0, bytes = 0, dropped = 0, cancelled = 0, rejected = 0;
} io;
bool range(uint32_t address, uint32_t size, uint32_t align = 4) {
    return !(address % align) && address >= base && address < end && size <= end - address;
}
bool access(uint8_t* ram, bool owner = false) {
    return ram && ram == io.ram && jfg_threads_context(ram, owner);
}
uint32_t word(uint32_t address) { uint32_t value; std::memcpy(&value, io.ram + address - base, 4); return value; }
void word(uint32_t address, uint32_t value) { std::memcpy(io.ram + address - base, &value, 4); }
} // namespace

int jfg_io_begin(uint8_t* ram) {
    if (io.ram || !jfg_threads_context(ram, 1) || jfg_boot_rom_size() != 0x2000000) return -1;
    io = Io{};
    io.ram = ram;
    return 0;
}
int jfg_io_manager(uint8_t* ram, int32_t priority, uint32_t queue, uint32_t buffer, int32_t capacity) {
    if (!access(ram) || priority < 0 || priority > 255) return -1;
    if (io.command_queue) return 0; // Original libultra keeps the first PI manager.
    int32_t result;
    if (jfg_threads_queue(ram, 0, queue, buffer, capacity, &result)) return -1;
    io.command_queue = queue;
    return 0;
}
int jfg_io_submit(uint8_t* ram, uint32_t message, int32_t priority, int32_t direction,
                  uint32_t source, uint32_t destination, uint32_t size, uint32_t return_queue, int32_t* result) {
    if (!access(ram) || !result) return -1;
    uint32_t offset = source >= 0x10000000u && source < 0x12000000u ? source - 0x10000000u : source;
    if (!io.command_queue || direction != 0 || (priority != 0 && priority != 1) ||
        !range(message, 24) || !range(destination, size, 8) || !size || size > 0x1000000 ||
        (offset & 1) || (size & 1) || offset > jfg_boot_rom_size() || size > jfg_boot_rom_size() - offset ||
        !jfg_threads_queue_known(ram, return_queue) || io.requests.contains(message)) {
        io.rejected++;
        return -1;
    }
    // Mirror the original descriptor fields; preserve its status byte.
    word(message, (11u << 16) | (uint32_t(priority) << 8) | (word(message) & 0xFFu));
    word(message + 4, return_queue);
    word(message + 8, destination);
    word(message + 12, source);
    word(message + 16, size);
    word(message + 20, 0);
    // No guest receiver consumes this command queue: the owner pump does.
    int status = jfg_threads_queue(ram, priority ? 2 : 1, io.command_queue, message, 0, result);
    if (status) return -1;
    if (*result == 0) {
        io.requests.emplace(message, Request{offset, destination, size, return_queue, source, uint32_t(priority)});
        io.submitted++;
    } else {
        io.rejected++;
    }
    return 0;
}
int jfg_io_pump(uint8_t* ram, uint32_t budget) {
    if (!access(ram, true) || io.pumping || !budget || budget > 4096) return -1;
    if (!io.command_queue) return 0;
    io.pumping = true;
    struct Finish { ~Finish() { io.pumping = false; } } finish;
    uint32_t processed = 0;
    while (processed < budget) {
        // Read the public ring state to obtain the descriptor, then consume it
        // through the runtime API. No scratch guest memory is needed.
        if (!word(io.command_queue + 8)) break;
        uint32_t first = word(io.command_queue + 12), count = word(io.command_queue + 16), buffer = word(io.command_queue + 20);
        if (!count || first >= count || count > 0x200000 || !range(buffer, count * 4)) return -1;
        uint32_t message = word(buffer + first * 4);
        auto found = io.requests.find(message);
        if (found == io.requests.end()) return -1;
        Request request = found->second;
        if ((word(message) >> 8) != ((11u << 8) | request.priority) ||
            word(message + 4) != request.return_queue || word(message + 8) != request.destination ||
            word(message + 12) != request.device_address || word(message + 16) != request.size || word(message + 20)) return -1;
        int32_t result;
        if (jfg_threads_queue(ram, 3, io.command_queue, 0, 0, &result) || result) return -1;
        if (jfg_boot_rom_read(ram, request.source, request.destination, request.size)) return -1;
        io.requests.erase(found);
        io.completed++;
        io.bytes += request.size;
        processed++;
        // Erasing before notification allows romCopy to reuse its descriptor
        // for the next 0x5000-byte chunk as soon as the waiting thread resumes.
        if (jfg_threads_queue(ram, 1, request.return_queue, message, 0, &result)) return -1;
        if (result == -1) io.dropped++;
        else if (result) return -1;
    }
    return int(processed);
}
int jfg_io_end(uint8_t* ram) {
    if (!io.ram) return 0;
    if (!access(ram, true) || io.pumping) return -1;
    io.cancelled += io.requests.size();
    io.requests.clear();
    if (io.command_queue) {
        int32_t result;
        do {
            if (jfg_threads_queue(ram, 3, io.command_queue, 0, 0, &result)) return -1;
        } while (result == 0);
    }
    io.command_queue = 0;
    io.ram = nullptr;
    return 0;
}
int jfg_io_state(uint64_t* fields, size_t count) {
    if (!fields || count != 10 || (io.ram && !access(io.ram, true))) return -1;
    std::array<uint64_t, 10> values{io.ram != nullptr, io.command_queue, io.requests.size(), io.submitted,
        io.completed, io.bytes, io.dropped, io.cancelled, io.rejected, 0};
    std::memcpy(fields, values.data(), sizeof(values));
    return 0;
}
