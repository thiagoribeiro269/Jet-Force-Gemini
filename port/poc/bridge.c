/* AI-assisted test infrastructure. Game functions are generated locally. */
#include <stdio.h>
#include <string.h>
#include <setjmp.h>
#include "bridge.h"
#include "runtime.h"

_Static_assert(sizeof(void *) == 8, "This proof targets 64-bit hosts");

struct PocFunction {
    const char *name;
    recomp_func_t *function;
    uint32_t section;
    uint32_t offset;
};

struct PocSection {
    uint32_t rom_offset;
    uint32_t load_size;
    uint32_t memory_size;
    uint32_t fixed_base;
};

#include "poc_symbols.h"

static int32_t section_bases[JFG_POC_SECTION_COUNT];
int32_t *section_addresses = section_bases;
/* This proof's dispatcher is deliberately single-threaded. */
static jmp_buf dispatch_guard;
static int dispatch_active;
static int dispatch_error;
static uint32_t call_count;
static uint32_t last_call_target;
static uint64_t last_return_address;

static _Noreturn void dispatch_fail(int error) {
    if (dispatch_active) {
        dispatch_error = error;
        longjmp(dispatch_guard, 1);
    }
    abort();
}

void jfg_poc_require_section(uint32_t section) {
    if (section >= JFG_POC_SECTION_COUNT || !section_bases[section]) dispatch_fail(-3);
}

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

POC_EXPORT uint32_t jfg_poc_last_call_count(void) { return call_count; }
POC_EXPORT uint32_t jfg_poc_last_call_target(void) { return last_call_target; }
POC_EXPORT uint64_t jfg_poc_last_return_address(void) { return last_return_address; }

void jfg_poc_call(uint32_t section, uint32_t offset, uint8_t *rdram, recomp_context *ctx) {
    jfg_poc_require_section(section);
    if (offset >= poc_sections[section].memory_size) dispatch_fail(-3);
    recomp_func_t *function = find_function((uint32_t)section_bases[section] + offset);
    if (!function) dispatch_fail(-3);
    call_count++;
    last_call_target = (uint32_t)section_bases[section] + offset;
    last_return_address = ctx->r31;
    function(rdram, ctx);
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

static int validate_section(uint32_t index, uint32_t base) {
    if (index >= JFG_POC_SECTION_COUNT) return -1;
    if (base && poc_sections[index].fixed_base && base != poc_sections[index].fixed_base) return -2;
    if (base && ((base & 3u) || base < 0x80000000u ||
                 base >= 0x80800000u || poc_sections[index].memory_size > 0x80800000u - base)) {
        return -2;
    }
    if (base) {
        uint32_t end = base + poc_sections[index].memory_size;
        for (uint32_t other = 0; other < JFG_POC_SECTION_COUNT; other++) {
            uint32_t other_base = (uint32_t)section_bases[other];
            if (other != index && other_base && base < other_base + poc_sections[other].memory_size && other_base < end) return -5;
        }
    }
    return 0;
}

POC_EXPORT int jfg_poc_set_section(uint32_t index, uint32_t base) {
    int error = validate_section(index, base);
    if (error) return error;
    section_bases[index] = (int32_t)base;
    return 0;
}

POC_EXPORT int jfg_poc_load_section(uint32_t index, uint32_t base, uint8_t *rdram,
                                    size_t ram_size, const uint8_t *rom, size_t rom_size) {
    if (!rdram || !rom || ram_size != 0x800000u || rom_size != 0x2000000u || !base) return -1;
    int error = validate_section(index, base);
    if (error) return error;
    if (section_bases[index]) return -6;
    const struct PocSection *section = &poc_sections[index];
    if (section->load_size > section->memory_size || section->rom_offset > rom_size || section->load_size > rom_size - section->rom_offset) return -1;
    uint32_t offset = base - 0x80000000u;
    for (uint32_t i = 0; i < section->load_size; i++) rdram[(offset + i) ^ 3u] = rom[section->rom_offset + i];
    for (uint32_t i = section->load_size; i < section->memory_size; i++) rdram[(offset + i) ^ 3u] = 0;
    section_bases[index] = (int32_t)base;
    return 0;
}

POC_EXPORT int jfg_poc_unload_section(uint32_t index) {
    return jfg_poc_set_section(index, 0);
}

POC_EXPORT int jfg_poc_run(uint32_t address, uint8_t *rdram, size_t size,
                           const uint64_t *input, uint64_t *output) {
    if (!rdram || !input || !output || size != 0x800000u || dispatch_active) return -1;
    recomp_func_t *function = find_function(address);
    if (!function) return -2;
    recomp_context ctx;
    memset(&ctx, 0, sizeof(ctx));
    memcpy(&ctx.r0, input, sizeof(uint64_t) * 32);
    ctx.r0 = 0;
    ctx.f_odd = &ctx.f0.u32h;
    dispatch_active = 1;
    dispatch_error = 0;
    call_count = 0;
    last_call_target = 0;
    last_return_address = 0;
    if (setjmp(dispatch_guard)) {
        dispatch_active = 0;
        return dispatch_error;
    }
    function(rdram, &ctx);
    dispatch_active = 0;
    memcpy(output, &ctx.r0, sizeof(uint64_t) * 32);
    return 0;
}

/* Unknown dependencies return a dispatch failure, never placeholder success. */
recomp_func_t *get_function(int32_t address) {
    recomp_func_t *function = find_function((uint32_t)address);
    if (!function) {
        dispatch_fail(-3);
    }
    return function;
}

void switch_error(const char *func, uint32_t address, uint32_t table) {
    fprintf(stderr, "Unexpected switch in %s at %08x (table %08x)\n", func, address, table);
    dispatch_fail(-4);
}
void do_break(uint32_t address) {
    fprintf(stderr, "Guest break at %08x\n", address);
    dispatch_fail(-4);
}
