// Access the pinned runtime's private retirement queue to join every thread
// deterministically. The vendor files themselves remain unchanged.
#include <atomic>
#include <cstring>
#include <memory>
#include "ultramodern/ultramodern.hpp"

namespace ultramodern {
bool jfg_vendor_thread_queue_remove(RDRAM_ARG PTR(PTR(OSThread)), PTR(OSThread));
}
#define thread_queue_remove jfg_vendor_thread_queue_remove
#include "../vendor/N64ModernRuntime/ultramodern/src/threadqueue.cpp"
#undef thread_queue_remove

// The pinned implementation repeatedly reads the head when removing a
// non-head node. Keep its other operations and advance the link correctly.
bool ultramodern::thread_queue_remove(RDRAM_ARG PTR(PTR(OSThread)) queue, PTR(OSThread) target) {
    auto* link = queue_to_ptr(PASS_RDRAM queue);
    while (*link != NULLPTR) {
        auto* node = TO_PTR(OSThread, *link);
        if (*link == target) {
            *link = node->next;
            node->next = NULLPTR;
            node->queue = NULLPTR;
            return true;
        }
        link = &node->next;
    }
    return false;
}

#include "../vendor/N64ModernRuntime/ultramodern/src/threads.cpp"

std::atomic_bool exited{false};
static std::unique_ptr<UltraThreadContext> host_context;
static unsigned char saved_host_slot[sizeof(OSThread)];
static bool saved_game_thread, saved_entrypoint_thread;
static uint32_t retired_count;

int jfg_kernel_attach(uint8_t* rdram, int32_t host_slot) {
    if (host_context || thread_self || !ultramodern::thread_queue_empty(rdram, ultramodern::running_queue)) return -1;
    host_context = std::make_unique<UltraThreadContext>();
    auto* host = TO_PTR(OSThread, host_slot);
    std::memcpy(saved_host_slot, host, sizeof(*host));
    std::memset(host, 0, sizeof(*host));
    host->id = 0;
    host->priority = -1; // Below every valid guest priority, including idle=0.
    host->context = host_context.get();
    host->state = OSThreadState::RUNNING;
    saved_game_thread = is_game_thread;
    saved_entrypoint_thread = is_entrypoint_thread;
    thread_self = host_slot;
    is_game_thread = true;
    is_entrypoint_thread = true;
    retired_count = 0;
    return 0;
}

int jfg_kernel_join_retired(uint32_t expected) {
    using namespace std::chrono_literals;
    while (retired_count < expected) {
        UltraThreadContext* context;
        if (!deleted_threads.wait_dequeue_timed(context, 2s)) return -1;
        context->host_thread.join();
        delete context;
        retired_count++;
    }
    return 0;
}

uint32_t jfg_kernel_retired_count() { return retired_count; }

int jfg_kernel_detach(uint8_t* rdram) {
    if (!host_context || !ultramodern::thread_queue_empty(rdram, ultramodern::running_queue)) return -1;
    std::memcpy(TO_PTR(OSThread, thread_self), saved_host_slot, sizeof(saved_host_slot));
    thread_self = NULLPTR;
    is_game_thread = saved_game_thread;
    is_entrypoint_thread = saved_entrypoint_thread;
    host_context.reset();
    return 0;
}
