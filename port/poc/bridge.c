/* AI-assisted test infrastructure. Game functions are generated locally. */
#include <stdio.h>
#include <string.h>
#include "bridge.h"
#include "recomp.h"

_Static_assert(sizeof(void *) == 8, "This proof targets 64-bit hosts");

struct PocFunction {
    const char *name;
    recomp_func_t *function;
    uint32_t section;
    uint32_t offset;
};

#include "poc_symbols.h"

static int32_t section_bases[JFG_POC_SECTION_COUNT];
int32_t *section_addresses = section_bases;

static recomp_func_t *find_function(uint32_t address) {
    for (size_t i = 0; i < JFG_POC_FUNCTION_COUNT; i++) {
        const struct PocFunction *entry = &poc_functions[i];
        uint32_t base = (uint32_t)section_bases[entry->section];
        if (base && address == base + entry->offset) {
            return entry->function;
        }
    }
    return NULL;
}

POC_EXPORT uint32_t jfg_poc_function_count(void) {
    return JFG_POC_FUNCTION_COUNT;
}

POC_EXPORT uint32_t jfg_poc_address(const char *name) {
    if (!name) return 0;
    for (size_t i = 0; i < JFG_POC_FUNCTION_COUNT; i++) {
        const struct PocFunction *entry = &poc_functions[i];
        uint32_t base = (uint32_t)section_bases[entry->section];
        if (base && strcmp(name, entry->name) == 0) return base + entry->offset;
    }
    return 0;
}

POC_EXPORT int jfg_poc_set_section(uint32_t index, uint32_t base) {
    if (index >= JFG_POC_SECTION_COUNT) return -1;
    if (base && ((base & 3u) || base < 0x80000000u ||
                 base >= 0x80800000u || poc_section_sizes[index] > 0x80800000u - base)) {
        return -2;
    }
    section_bases[index] = (int32_t)base;
    return 0;
}

POC_EXPORT int jfg_poc_run(uint32_t address, uint8_t *rdram, size_t size,
                           const uint64_t *input, uint64_t *output) {
    if (!rdram || !input || !output || size != 0x800000u) return -1;
    recomp_func_t *function = find_function(address);
    if (!function) return -2;
    recomp_context ctx;
    memset(&ctx, 0, sizeof(ctx));
    memcpy(&ctx.r0, input, sizeof(uint64_t) * 32);
    ctx.r0 = 0;
    ctx.f_odd = &ctx.f0.u32h;
    function(rdram, &ctx);
    memcpy(output, &ctx.r0, sizeof(uint64_t) * 32);
    return 0;
}

/* Unexpected runtime dependencies are hard failures, not placeholder stubs. */
recomp_func_t *get_function(int32_t address) {
    recomp_func_t *function = find_function((uint32_t)address);
    if (!function) {
        fprintf(stderr, "Unsupported guest call: %08x\n", (uint32_t)address);
        abort();
    }
    return function;
}

void switch_error(const char *func, uint32_t address, uint32_t table) {
    fprintf(stderr, "Unexpected switch in %s at %08x (table %08x)\n", func, address, table);
    abort();
}
void do_break(uint32_t address) {
    fprintf(stderr, "Guest break at %08x\n", address);
    abort();
}
