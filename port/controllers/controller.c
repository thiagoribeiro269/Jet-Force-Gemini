/* Deterministic controller absence for the original joyInit startup path.
 * This models the public result of controller discovery and one deferred SI
 * completion. It does not access a device or emulate PIF commands, DMA, or
 * libultra's initial half-second timer.
 */
#include <string.h>
#include "controller.h"
#include "events.h"
#include "scheduler.h"

enum { RAM_BASE = 0x80000000u, RAM_END = 0x80800000u };
enum { SI_EVENT = 5, NO_RESPONSE = 8, STATUS_BYTES = 16, QUEUE_BYTES = 24 };
enum { EVENTS_STATE_FIELDS = 16, EVENT_DELIVERED = 4, EVENT_DROPPED = 5 };

static struct {
    uint8_t* ram;
    uint32_t queue;
    uint64_t started;
    uint64_t completed;
    uint64_t dropped;
    uint64_t rejected;
    int initialized;
    int pending;
} controllers;

static int valid_range(uint32_t address, uint32_t size, uint32_t alignment) {
    return address >= RAM_BASE && address < RAM_END && size <= RAM_END - address &&
           (address % alignment) == 0;
}

static int access_ram(uint8_t* ram, int owner_only) {
    return ram && ram == controllers.ram && jfg_threads_context(ram, owner_only);
}

static int overlaps(uint32_t a, uint32_t a_size, uint32_t b, uint32_t b_size) {
    return a < b + b_size && b < a + a_size;
}

int jfg_controllers_begin(uint8_t* ram) {
    if (controllers.ram || !jfg_threads_context(ram, 1)) return -1;
    memset(&controllers, 0, sizeof(controllers));
    controllers.ram = ram;
    return 0;
}

int jfg_controllers_end(uint8_t* ram) {
    if (!controllers.ram) return 0;
    if (!access_ram(ram, 1)) return -1;
    memset(&controllers, 0, sizeof(controllers));
    return 0;
}

int jfg_controllers_state(uint64_t* fields, size_t count) {
    uint64_t values[JFG_CONTROLLER_STATE_FIELDS];
    if (!fields || count != JFG_CONTROLLER_STATE_FIELDS ||
        (controllers.ram && !access_ram(controllers.ram, 1))) return -1;
    values[0] = controllers.ram != NULL;
    values[1] = controllers.initialized;
    values[2] = controllers.pending;
    values[3] = controllers.started;
    values[4] = controllers.completed;
    values[5] = controllers.dropped;
    values[6] = controllers.rejected;
    values[7] = controllers.queue;
    memcpy(fields, values, sizeof(values));
    return 0;
}

int jfg_controllers_init(uint8_t* ram, uint32_t queue, uint32_t pattern,
                         uint32_t status, int32_t* result) {
    unsigned i;
    if (!access_ram(ram, 0) || !result) return -1;
    /* As in libultra, a second osContInit returns without writing outputs. */
    if (controllers.initialized) {
        *result = 0;
        return 0;
    }
    if (!jfg_threads_queue_known(ram, queue) || !valid_range(pattern, 1, 1) ||
        !valid_range(status, STATUS_BYTES, 2) ||
        (pattern >= status && pattern < status + STATUS_BYTES) ||
        overlaps(pattern, 1, queue, QUEUE_BYTES) || overlaps(status, STATUS_BYTES, queue, QUEUE_BYTES)) {
        controllers.rejected++;
        return -1;
    }
    ram[(pattern - RAM_BASE) ^ 3u] = 0;
    /* __osContGetInitData writes only errno for a missing channel. Preserve
     * each status entry's type and accessory status bytes. */
    for (i = 0; i < 4; ++i) ram[((status + i * 4u + 3u) - RAM_BASE) ^ 3u] = NO_RESPONSE;
    controllers.queue = queue;
    controllers.initialized = 1;
    *result = 0;
    return 0;
}

int jfg_controllers_start_read(uint8_t* ram, uint32_t queue, int32_t* result) {
    if (!access_ram(ram, 0) || !result) return -1;
    if (!controllers.initialized || queue != controllers.queue ||
        !jfg_threads_queue_known(ram, queue)) {
        controllers.rejected++;
        return -1;
    }
    if (controllers.pending) {
        controllers.rejected++;
        return -2;
    }
    controllers.pending = 1;
    controllers.started++;
    *result = 0;
    return 0;
}

int jfg_controllers_pump(uint8_t* ram) {
    uint64_t before[EVENTS_STATE_FIELDS], after[EVENTS_STATE_FIELDS];
    if (!access_ram(ram, 1)) return -1;
    if (!controllers.pending) return 0;
    if (jfg_events_state(before, EVENTS_STATE_FIELDS) ||
        jfg_events_post(ram, SI_EVENT) ||
        jfg_events_state(after, EVENTS_STATE_FIELDS)) return -1;
    if (after[EVENT_DELIVERED] == before[EVENT_DELIVERED] + 1) {
        controllers.completed++;
    } else if (after[EVENT_DROPPED] == before[EVENT_DROPPED] + 1) {
        controllers.dropped++;
    } else {
        /* No SI route was registered. Keep the completion pending. */
        return -3;
    }
    controllers.pending = 0;
    return 0;
}
