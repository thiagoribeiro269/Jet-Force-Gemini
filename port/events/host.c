#include "runtime.h"
#include "events.h"

static void checked(int status) { if (status) jfg_poc_fail(-12); }
static uint64_t pair(uint64_t high, uint64_t low) {
    return ((uint64_t)(uint32_t)high << 32) | (uint32_t)low;
}
void jfg_host_osSetEventMesg(uint8_t* ram, recomp_context* ctx) {
    checked(jfg_events_bind(ram, (uint32_t)ctx->r4, (uint32_t)ctx->r5, (uint32_t)ctx->r6));
}
void jfg_host_osViSetEvent(uint8_t* ram, recomp_context* ctx) {
    checked(jfg_events_vi_bind(ram, (uint32_t)ctx->r4, (uint32_t)ctx->r5, (uint32_t)ctx->r6));
}
void jfg_host_osCreateViManager(uint8_t* ram, recomp_context* ctx) {
    checked(jfg_events_vi_manager(ram, (int32_t)ctx->r4));
}
void jfg_host_osViSetMode(uint8_t* ram, recomp_context* ctx) {
    checked(jfg_events_vi_mode(ram, (uint32_t)ctx->r4));
}
void jfg_host_osViBlack(uint8_t* ram, recomp_context* ctx) {
    checked(jfg_events_vi_black(ram, (uint32_t)ctx->r4));
}
void jfg_host_osGetCount(uint8_t* ram, recomp_context* ctx) {
    uint64_t value;
    checked(jfg_events_clock(ram, 0, 0, &value));
    ctx->r2 = (gpr)(int32_t)value;
}
void jfg_host_osGetTime(uint8_t* ram, recomp_context* ctx) {
    uint64_t value;
    checked(jfg_events_clock(ram, 1, 0, &value));
    ctx->r2 = (gpr)(int32_t)(value >> 32);
    ctx->r3 = (gpr)(int32_t)value;
}
void jfg_host_osSetTime(uint8_t* ram, recomp_context* ctx) {
    uint64_t ignored;
    checked(jfg_events_clock(ram, 2, pair(ctx->r4, ctx->r5), &ignored));
}
void jfg_host_osSetTimer(uint8_t* rdram, recomp_context* ctx) {
    uint32_t sp = (uint32_t)ctx->r29;
    if (sp < 0x80000000u || sp > 0x807FFFE0u || (sp & 7)) jfg_poc_fail(-12);
    uint64_t interval = pair(MEM_W(0x10, ctx->r29), MEM_W(0x14, ctx->r29));
    checked(jfg_events_timer(rdram, (uint32_t)ctx->r4, pair(ctx->r6, ctx->r7), interval,
                             MEM_W(0x18, ctx->r29), MEM_W(0x1C, ctx->r29)));
    ctx->r2 = 0;
}
void jfg_host_osSetIntMask(uint8_t* ram, recomp_context* ctx) {
    uint32_t previous;
    checked(jfg_events_mask(ram, (uint32_t)ctx->r4, &previous));
    ctx->r2 = (gpr)(int32_t)previous;
}
