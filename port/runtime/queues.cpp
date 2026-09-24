// A bounded, non-suspending bridge to the pinned N64ModernRuntime queues.
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <stdexcept>

#include "ultramodern/ultramodern.hpp"

#ifdef _WIN32
#define JFG_EXPORT extern "C" __declspec(dllexport)
#else
#define JFG_EXPORT extern "C" __attribute__((visibility("default")))
#endif

// Link requirements of the unmodified thread implementation. This adapter
// never creates a thread or starts its cleanup service.
std::atomic_bool exited{false};
void run_thread_function(uint8_t*, uint64_t, uint64_t, uint64_t) {
    throw std::runtime_error("Game thread entrypoints are not integrated yet");
}

namespace {
constexpr uint32_t ram_base = 0x80000000u;
constexpr uint32_t ram_end = 0x80800000u;
std::mutex execution_mutex;

bool range(uint32_t address, uint32_t length) {
    return !(address & 3) && address >= ram_base && address < ram_end && length <= ram_end - address;
}

bool overlaps(uint32_t a, uint32_t a_size, uint32_t b, uint32_t b_size) {
    return a < b + b_size && b < a + a_size;
}
} // namespace

// Operations: 0=create (value=buffer, argument=capacity), 1=send, 2=jam,
// 3=receive (value=destination or null; argument=OS_MESG_*).
// Status is separate from the guest result: -1 means invalid memory/state,
// -2 unsupported flags/op, -3 requires thread suspension or wakeup,
// -4 a host exception. On failure the guest result is untouched.
JFG_EXPORT int jfg_queue_call(uint32_t operation, uint8_t* rdram, size_t ram_size,
                              uint32_t queue, uint32_t value, int32_t argument,
                              int32_t* guest_result) {
    if (!rdram || !guest_result || ram_size != 0x800000u ||
        !range(queue, sizeof(OSMesgQueue))) return -1;
    std::lock_guard lock{execution_mutex};
    auto* mq = reinterpret_cast<OSMesgQueue*>(rdram + queue - ram_base);
    if (operation == 0) {
        if (argument <= 0 || argument > 0x200000 || !range(value, uint32_t(argument) * 4) ||
            overlaps(queue, sizeof(OSMesgQueue), value, uint32_t(argument) * 4)) return -1;
    } else {
        if (operation > 3 || (argument != OS_MESG_NOBLOCK && argument != OS_MESG_BLOCK)) return -2;
        if (mq->msgCount <= 0 || mq->msgCount > 0x200000 || mq->validCount < 0 ||
            mq->validCount > mq->msgCount || mq->first < 0 || mq->first >= mq->msgCount ||
            !range(uint32_t(mq->msg), uint32_t(mq->msgCount) * 4) ||
            overlaps(queue, sizeof(OSMesgQueue), uint32_t(mq->msg), uint32_t(mq->msgCount) * 4)) return -1;
        if (operation == 3 && value && (!range(value, 4) ||
            overlaps(value, 4, queue, sizeof(OSMesgQueue)) ||
            overlaps(value, 4, uint32_t(mq->msg), uint32_t(mq->msgCount) * 4))) return -1;
        // Do not enter runtime paths that suspend or resume unintegrated threads.
        if (mq->blocked_on_recv || mq->blocked_on_send ||
            (argument == OS_MESG_BLOCK && (operation == 3 ? mq->validCount == 0 : mq->validCount == mq->msgCount))) return -3;
    }
    try {
        ultramodern::set_entrypoint_thread();
        int32_t result = 0;
        switch (operation) {
            case 0: osCreateMesgQueue(rdram, int32_t(queue), int32_t(value), argument); break;
            case 1: result = osSendMesg(rdram, int32_t(queue), int32_t(value), argument); break;
            case 2: result = osJamMesg(rdram, int32_t(queue), int32_t(value), argument); break;
            case 3: result = osRecvMesg(rdram, int32_t(queue), int32_t(value), argument); break;
            default: return -2;
        }
        *guest_result = result;
        return 0;
    } catch (const std::exception&) {
        return -4;
    }
}
