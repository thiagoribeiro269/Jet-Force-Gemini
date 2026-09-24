#include "runtime.h"
#include "io.h"
#include "events.h"

void jfg_host_osCreatePiManager(uint8_t* ram, recomp_context* ctx) {
    if (jfg_io_manager(ram, (int32_t)ctx->r4, (uint32_t)ctx->r5, (uint32_t)ctx->r6, (int32_t)ctx->r7)) jfg_poc_fail(-13);
}
void jfg_host_osPiStartDma(uint8_t* rdram, recomp_context* ctx) {
    uint32_t sp = (uint32_t)ctx->r29;
    if (sp < 0x80000000u || sp > 0x807FFFE4u || (sp & 7)) jfg_poc_fail(-13);
    int32_t result;
    if (jfg_io_submit(rdram, (uint32_t)ctx->r4, (int32_t)ctx->r5, (int32_t)ctx->r6, (uint32_t)ctx->r7,
                      MEM_W(0x10, ctx->r29), MEM_W(0x14, ctx->r29), MEM_W(0x18, ctx->r29), &result)) jfg_poc_fail(-13);
    ctx->r2 = (gpr)(int32_t)result;
}
void jfg_host_osViSetSpecialFeatures(uint8_t* ram, recomp_context* ctx) {
    if (jfg_events_vi_features(ram, (uint32_t)ctx->r4)) jfg_poc_fail(-12);
}
