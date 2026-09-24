/* Explicit single-threaded platform contracts for the headless boot slice. */
#include <string.h>
#if defined(_MSC_VER)
#include <intrin.h>
#endif
#include "bridge.h"
#include "runtime.h"

#define EVENT_CAPACITY 16384u
static const uint8_t *rom_image;
static size_t rom_length;
static uint32_t events[EVENT_CAPACITY][4];
static uint32_t event_count;

POC_EXPORT int jfg_boot_bind_rom(const uint8_t *rom, size_t length) {
    if (!rom && !length) { rom_image = NULL; rom_length = 0; event_count = 0; return 0; }
    if (!rom || length != 0x2000000u) return -1;
    rom_image = rom;
    rom_length = length;
    event_count = 0;
    return 0;
}

POC_EXPORT void jfg_boot_clear_events(void) { event_count = 0; }
POC_EXPORT uint32_t jfg_boot_event_count(void) { return event_count; }
POC_EXPORT int jfg_boot_event(uint32_t index, uint32_t *out) {
    if (!out || index >= event_count) return -1;
    memcpy(out, events[index], sizeof(events[index]));
    return 0;
}

static void event(uint32_t type, uint32_t a, uint32_t b, uint32_t c) {
    if (event_count >= EVENT_CAPACITY) jfg_poc_fail(-10);
    events[event_count][0] = type;
    events[event_count][1] = a;
    events[event_count][2] = b;
    events[event_count][3] = c;
    event_count++;
}

void jfg_host_romCopy(uint8_t *rdram, recomp_context *ctx) {
    uint32_t source = (uint32_t)ctx->r4, destination = (uint32_t)ctx->r5, length = (uint32_t)ctx->r6;
    if (!rom_image || source > rom_length || length > rom_length - source ||
        destination < 0x80000000u || destination > 0x80800000u || length > 0x80800000u - destination) {
        jfg_poc_fail(-10);
    }
    event(1, source, destination, length);
    uint32_t offset = destination - 0x80000000u;
    for (uint32_t i = 0; i < length; i++) rdram[(offset + i) ^ 3u] = rom_image[source + i];
}

static void cache_contract(uint32_t type, recomp_context *ctx) {
    uint32_t address = (uint32_t)ctx->r4, length = (uint32_t)ctx->r5;
    if (address < 0x80000000u || address > 0x80800000u || length > 0x80800000u - address) jfg_poc_fail(-10);
    event(type, address, length, 0);
    /* Guest RAM is coherent CPU memory in this single-threaded prototype.
       Keep compiler ordering; this does not model an N64 cache or GPU fence. */
#if defined(_MSC_VER)
    _ReadWriteBarrier();
#else
    __asm__ __volatile__("" ::: "memory");
#endif
}

void jfg_host_osWritebackDCache(uint8_t *rdram, recomp_context *ctx) { (void)rdram; cache_contract(2, ctx); }
void jfg_host_osInvalICache(uint8_t *rdram, recomp_context *ctx) { (void)rdram; cache_contract(3, ctx); }
