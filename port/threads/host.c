#include "runtime.h"
#include "scheduler.h"

static void message(uint8_t* rdram, recomp_context* ctx, uint32_t operation) {
    int32_t result;
    int status = jfg_threads_queue(rdram, operation, (uint32_t)ctx->r4,
                                   (uint32_t)ctx->r5, (int32_t)ctx->r6, &result);
    if (status) jfg_poc_fail(-11);
    if (operation) ctx->r2 = (gpr)(int32_t)result;
}

void jfg_host_osCreateMesgQueue(uint8_t* rdram, recomp_context* ctx) { message(rdram, ctx, 0); }
void jfg_host_osSendMesg(uint8_t* rdram, recomp_context* ctx) { message(rdram, ctx, 1); }
void jfg_host_osJamMesg(uint8_t* rdram, recomp_context* ctx) { message(rdram, ctx, 2); }
void jfg_host_osRecvMesg(uint8_t* rdram, recomp_context* ctx) { message(rdram, ctx, 3); }

void jfg_host_osCreateThread(uint8_t* rdram, recomp_context* ctx) {
    uint32_t sp = (uint32_t)ctx->r29;
    if (sp < 0x80000000u || sp > 0x807FFFE8u || (sp & 7)) jfg_poc_fail(-11);
    uint64_t arguments[4] = { ctx->r7, 0, 0, 0 };
    int status = jfg_threads_create(rdram, (uint32_t)ctx->r4, (int32_t)ctx->r5, (uint32_t)ctx->r6,
                                    MEM_W(0x10, ctx->r29), MEM_W(0x14, ctx->r29), arguments);
    if (status) jfg_poc_fail(-11);
}

void jfg_host_osStartThread(uint8_t* rdram, recomp_context* ctx) {
    if (jfg_threads_start(rdram, (uint32_t)ctx->r4)) jfg_poc_fail(-11);
}
