#include "runtime.h"
#include "ai.h"

void jfg_host_osAiSetFrequency(uint8_t* ram, recomp_context* ctx) {
    int32_t actual;
    if (jfg_ai_frequency(ram, (uint32_t)ctx->r4, &actual)) jfg_poc_fail(-14);
    ctx->r2 = (gpr)(int64_t)actual;
}
