#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <memory>
#include <thread>
#include <unordered_set>
#include <vector>
#include "scheduler.h"
#include "ultramodern/ultramodern.hpp"
#ifdef JFG_EVENT_PROFILE
#include "events.h"
#endif
#ifdef JFG_INIT_PROFILE
#include "io.h"
#endif
#ifdef JFG_AUDIO_MANAGER_PROFILE
#include "ai.h"
#endif
#ifdef JFG_CONTROLLER_PROFILE
#include "controller.h"
#endif

int jfg_kernel_attach(uint8_t*, int32_t);
int jfg_kernel_join_retired(uint32_t);
uint32_t jfg_kernel_retired_count();
int jfg_kernel_detach(uint8_t*);

namespace {
constexpr uint32_t ram_base = 0x80000000u, ram_end = 0x80800000u;
enum State { created = 0, active = 1, completed = 2, failed = 3, cancelled = 4 };
struct Worker {
    uint32_t slot, entry;
    std::array<uint64_t, 4> arguments{};
    std::array<uint64_t, 32> result{};
    std::atomic<int32_t> state{created};
    std::atomic<bool> entered{false};
    int32_t status = 0;
    uint32_t error_target = 0, error_site = 0;
};
uint8_t* session_ram;
uint32_t host_slot;
std::thread::id owner;
std::vector<std::unique_ptr<Worker>> workers;
std::unordered_set<uint32_t> message_queues;

bool range(uint32_t address, uint32_t size, uint32_t alignment = 4) {
    return !(address % alignment) && address >= ram_base && address < ram_end && size <= ram_end - address;
}
bool overlap(uint32_t a, uint32_t as, uint32_t b, uint32_t bs) {
    return a < b + bs && b < a + as;
}
Worker* find_worker(uint32_t slot) {
    for (auto& worker : workers) if (worker->slot == slot) return worker.get();
    return nullptr;
}
bool access(uint8_t* ram) {
    if (!session_ram || ram != session_ram) return false;
    auto self = uint32_t(ultramodern::this_thread());
    return self == host_slot ? std::this_thread::get_id() == owner : find_worker(self) != nullptr;
}
bool host_access() {
    return session_ram && std::this_thread::get_id() == owner && uint32_t(ultramodern::this_thread()) == host_slot;
}
uint64_t sign_extend(uint32_t value) { return uint64_t(int64_t(int32_t(value))); }
} // namespace

int jfg_threads_context(uint8_t* ram, int owner_only) {
    return owner_only ? (host_access() && ram == session_ram) : access(ram);
}

int jfg_threads_queue_known(uint8_t* ram, uint32_t queue) {
    return access(ram) && message_queues.contains(queue);
}

// Called by the original runtime on the native thread's own stack. Each
// recompiled invocation retains its context while queue operations suspend it.
void run_thread_function(uint8_t* rdram, uint64_t entry, uint64_t stack, uint64_t) {
    Worker* worker = find_worker(uint32_t(ultramodern::this_thread()));
    if (!worker || uint32_t(entry) != worker->entry) throw std::runtime_error("Unregistered game thread");
    worker->entered.store(true, std::memory_order_release);
    worker->state.store(active, std::memory_order_release);
    std::array<uint64_t, 32> input{};
    std::copy(worker->arguments.begin(), worker->arguments.end(), input.begin() + 4);
    input[29] = sign_extend(uint32_t(stack));
    input[31] = sign_extend(0x807FF000);
    try {
        worker->status = jfg_poc_run(worker->entry, rdram, 0x800000, input.data(), worker->result.data());
        worker->error_target = jfg_poc_error_target();
        worker->error_site = jfg_poc_error_site();
        worker->state.store(worker->status ? failed : completed, std::memory_order_release);
    } catch (ultramodern::thread_terminated&) {
        worker->status = -100;
        worker->state.store(cancelled, std::memory_order_release);
        throw;
    } catch (const std::exception&) {
        worker->status = -20;
        worker->state.store(failed, std::memory_order_release);
    }
}

int jfg_threads_begin(uint8_t* ram, size_t size, uint32_t slot) {
    if (session_ram || !ram || size != 0x800000 || !range(slot, sizeof(OSThread), 8)) return -1;
    if (jfg_kernel_attach(ram, int32_t(slot))) return -1;
    session_ram = ram;
    host_slot = slot;
    owner = std::this_thread::get_id();
    workers.clear();
    message_queues.clear();
    return 0;
}

int jfg_threads_create(uint8_t* ram, uint32_t slot, int32_t id, uint32_t entry,
                       uint32_t stack, int32_t priority, const uint64_t* arguments) {
    if (!access(ram) || !arguments || workers.size() >= 32 || find_worker(slot) ||
        !range(slot, sizeof(OSThread), 8) || !range(stack - 0x100, 0x100, 8) ||
        overlap(slot, sizeof(OSThread), host_slot, sizeof(OSThread)) ||
        priority < 0 || priority > 255) return -1;
    if (!jfg_poc_is_callable(entry)) return -2;
    for (const auto& worker : workers)
        if (overlap(slot, sizeof(OSThread), worker->slot, sizeof(OSThread))) return -1;
    auto worker = std::make_unique<Worker>();
    worker->slot = slot;
    worker->entry = entry;
    std::copy(arguments, arguments + 4, worker->arguments.begin());
    workers.push_back(std::move(worker));
    osCreateThread(ram, int32_t(slot), id, int32_t(entry), int32_t(arguments[0]), int32_t(stack), priority);
    return 0;
}

int jfg_threads_start(uint8_t* ram, uint32_t slot) {
    if (!access(ram)) return -1;
    auto* worker = find_worker(slot);
    if (!worker || worker->state.load(std::memory_order_acquire) != created) return -1;
    // A lower-priority child may be queued without entering its function yet.
    // Mark it live before starting so a second start cannot queue it twice.
    worker->state.store(active, std::memory_order_release);
    osStartThread(ram, int32_t(slot));
    return 0;
}

int jfg_threads_result(uint32_t slot, int32_t* state, int32_t* status, uint64_t* registers) {
    if (!host_access() || !state || !status || !registers) return -1;
    auto* worker = find_worker(slot);
    if (!worker) return -1;
    *state = worker->state.load(std::memory_order_acquire);
    *status = (*state >= completed) ? worker->status : 0;
    std::copy(worker->result.begin(), worker->result.end(), registers);
    return 0;
}

int jfg_threads_error(uint32_t slot, uint32_t* target, uint32_t* site) {
    if (!host_access() || !target || !site) return -1;
    auto* worker = find_worker(slot);
    if (!worker || worker->state.load(std::memory_order_acquire) < completed) return -1;
    *target = worker->error_target;
    *site = worker->error_site;
    return 0;
}

int jfg_threads_queue(uint8_t* ram, uint32_t operation, uint32_t queue,
                      uint32_t value, int32_t argument, int32_t* result) {
    if (!access(ram) || !result || !range(queue, sizeof(OSMesgQueue))) return -1;
    auto* mq = reinterpret_cast<OSMesgQueue*>(ram + queue - ram_base);
    if (operation == 0) {
        if (argument <= 0 || argument > 0x200000 || !range(value, uint32_t(argument) * 4) ||
            overlap(queue, sizeof(*mq), value, uint32_t(argument) * 4)) return -1;
        // A new queue may be in uninitialized guest stack memory. Only an
        // already registered queue can contain live native wait lists.
        if (message_queues.contains(queue) && (mq->blocked_on_recv || mq->blocked_on_send)) return -1;
    } else {
        if (!message_queues.contains(queue)) {
            // JFG polls its zeroed reset queue before its later initialization.
            // Original libultra returns -1 before touching the buffer in this
            // exact nonblocking, empty case; preserve that observed behavior.
            const OSMesgQueue zero{};
            if (operation == 3 && argument == OS_MESG_NOBLOCK && !std::memcmp(mq, &zero, sizeof(zero))) {
                *result = -1;
                return 0;
            }
            return -1;
        }
        if (operation > 3 || (argument != OS_MESG_BLOCK && argument != OS_MESG_NOBLOCK)) return -1;
        if (mq->msgCount <= 0 || mq->msgCount > 0x200000 || mq->validCount < 0 || mq->validCount > mq->msgCount ||
            mq->first < 0 || mq->first >= mq->msgCount || !range(uint32_t(mq->msg), uint32_t(mq->msgCount) * 4) ||
            overlap(queue, sizeof(*mq), uint32_t(mq->msg), uint32_t(mq->msgCount) * 4)) return -1;
        if (operation == 3 && value && (!range(value, 4) || overlap(value, 4, queue, sizeof(*mq)) ||
            overlap(value, 4, uint32_t(mq->msg), uint32_t(mq->msgCount) * 4))) return -1;
        // The owner drives the headless loop and must never suspend itself.
        if (uint32_t(ultramodern::this_thread()) == host_slot && argument == OS_MESG_BLOCK &&
            (operation == 3 ? mq->validCount == 0 : mq->validCount == mq->msgCount)) return -3;
    }
    switch (operation) {
        case 0:
            message_queues.insert(queue);
            osCreateMesgQueue(ram, int32_t(queue), int32_t(value), argument);
            *result = 0;
            break;
        case 1: *result = osSendMesg(ram, int32_t(queue), int32_t(value), argument); break;
        case 2: *result = osJamMesg(ram, int32_t(queue), int32_t(value), argument); break;
        case 3: *result = osRecvMesg(ram, int32_t(queue), int32_t(value), argument); break;
        default: return -1;
    }
    return 0;
}

int jfg_threads_end(uint8_t* ram) {
    if (!host_access() || ram != session_ram) return -1;
#ifdef JFG_CONTROLLER_PROFILE
    if (jfg_controllers_end(ram)) return -1;
#endif
#ifdef JFG_AUDIO_MANAGER_PROFILE
    if (jfg_ai_end(ram)) return -1;
#endif
#ifdef JFG_INIT_PROFILE
    if (jfg_io_end(ram)) return -1;
#endif
#ifdef JFG_EVENT_PROFILE
    if (jfg_events_end(ram)) return -1;
#endif
    for (auto& worker : workers) {
        auto* thread = reinterpret_cast<OSThread*>(ram + worker->slot - ram_base);
        if (thread->context) {
            if (!worker->entered.load(std::memory_order_acquire)) {
                worker->status = -100;
                worker->state.store(cancelled, std::memory_order_release);
            }
            osDestroyThread(ram, int32_t(worker->slot));
        }
    }
    if (jfg_kernel_join_retired(uint32_t(workers.size())) || jfg_kernel_detach(ram)) return -7;
    workers.clear();
    message_queues.clear();
    session_ram = nullptr;
    return 0;
}

uint32_t jfg_threads_joined() { return jfg_kernel_retired_count(); }
