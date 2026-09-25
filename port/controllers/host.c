#include "runtime.h"
#include "controller.h"

void jfg_host_osContInit(uint8_t* ram, recomp_context* ctx) {
    int32_t result;
    if (jfg_controllers_init(ram, (uint32_t)ctx->r4, (uint32_t)ctx->r5,
                             (uint32_t)ctx->r6, &result)) jfg_poc_fail(-15);
    ctx->r2 = (gpr)(int64_t)result;
}

void jfg_host_osContStartReadData(uint8_t* ram, recomp_context* ctx) {
    int32_t result;
    if (jfg_controllers_start_read(ram, (uint32_t)ctx->r4, &result)) jfg_poc_fail(-15);
    ctx->r2 = (gpr)(int64_t)result;
}
