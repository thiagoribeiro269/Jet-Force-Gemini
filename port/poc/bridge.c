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
    uint32_t overlay;
};

#include "poc_symbols.h"

static int32_t section_bases[JFG_POC_SECTION_COUNT];
int32_t *section_addresses = section_bases;
/* Each cooperatively scheduled host thread retains its own suspended call.
   Guest RAM and section registration remain shared and serialized by the
   runtime scheduler; this does not allow arbitrary concurrent guest writes. */
static _Thread_local jmp_buf dispatch_guard;
static _Thread_local int dispatch_active;
static _Thread_local int dispatch_error;
static _Thread_local uint32_t call_count;
static _Thread_local uint32_t last_call_target;
static _Thread_local uint64_t last_return_address;
static _Thread_local uint64_t indirect_target;
static _Thread_local int indirect_prepared;
static _Thread_local uint32_t error_target;
static _Thread_local uint32_t error_site;
static _Thread_local uint64_t missing_function_registers[32];
static _Thread_local int missing_function_registers_valid;
static uint32_t game_table_global;
static uint32_t game_count_global;
static int game_linker_pending;

static int sync_game_sections(uint8_t *rdram);
static int guest_word(uint8_t *rdram, uint32_t address, uint32_t *value);
static int sync_watched_game_sections(uint8_t *rdram);

static _Noreturn void dispatch_fail(int error) {
    if (dispatch_active) {
        dispatch_error = error;
        longjmp(dispatch_guard, 1);
    }
    abort();
}

void jfg_poc_fail(int code) { dispatch_fail(code); }

void jfg_poc_require_runtime_patch(uint8_t *rdram, uint32_t address, uint32_t word) {
    if (address < 0x80000000u || address > 0x807FFFFCu || (address & 3)) dispatch_fail(-9);
    uint32_t actual;
    memcpy(&actual, rdram + address - 0x80000000u, 4);
    if (actual != word) dispatch_fail(-9);
}

void jfg_poc_prepare_indirect(uint64_t target) {
    indirect_target = target;
    indirect_prepared = 1;
}

void jfg_poc_require_section(uint32_t section) {
    if (section >= JFG_POC_SECTION_COUNT || !section_bases[section]) {
        fprintf(stderr, "Unregistered recompilation section %u\n", section);
        dispatch_fail(-3);
    }
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

POC_EXPORT int jfg_poc_is_callable(uint32_t address) { return find_function(address) != NULL; }

POC_EXPORT uint32_t jfg_poc_last_call_count(void) { return call_count; }
POC_EXPORT uint32_t jfg_poc_last_call_target(void) { return last_call_target; }
POC_EXPORT uint64_t jfg_poc_last_return_address(void) { return last_return_address; }
POC_EXPORT uint32_t jfg_poc_error_target(void) { return error_target; }
POC_EXPORT uint32_t jfg_poc_error_site(void) { return error_site; }

void jfg_poc_call(uint32_t section, uint32_t offset, uint8_t *rdram, recomp_context *ctx) {
    jfg_poc_require_section(section);
    if (offset >= poc_sections[section].memory_size) dispatch_fail(-3);
    recomp_func_t *function = find_function((uint32_t)section_bases[section] + offset);
    if (!function) {
        error_target = (uint32_t)section_bases[section] + offset;
        error_site = (uint32_t)ctx->r31 - 8;
        memcpy(missing_function_registers, &ctx->r0, sizeof(missing_function_registers));
        missing_function_registers_valid = 1;
        dispatch_fail(-3);
    }
    call_count++;
    last_call_target = (uint32_t)section_bases[section] + offset;
    last_return_address = ctx->r31;
    function(rdram, ctx);
}

void jfg_poc_call_indirect(uint8_t *rdram, recomp_context *ctx) {
    if (!indirect_prepared) dispatch_fail(-3);
    uint32_t target = (uint32_t)indirect_target;
    indirect_prepared = 0;
    if (game_table_global && sync_watched_game_sections(rdram)) dispatch_fail(-7);
    recomp_func_t *function = find_function(target);
    if (!function) {
        error_target = target;
        error_site = (uint32_t)ctx->r31 - 8;
        memcpy(missing_function_registers, &ctx->r0, sizeof(missing_function_registers));
        missing_function_registers_valid = 1;
        dispatch_fail(-3);
    }
    call_count++;
    last_call_target = target;
    last_return_address = ctx->r31;
    function(rdram, ctx);
}

void jfg_poc_call_jal(uint8_t *rdram, recomp_context *ctx) {
    uint32_t site = (uint32_t)ctx->r31 - 8u;
    uint32_t word = 0;
    if (guest_word(rdram, site, &word) || (word >> 26) != 3u) {
        error_site = site;
        error_target = 0;
        fprintf(stderr, "Invalid patched JAL at %08x: %08x\n", site, word);
        dispatch_fail(-3);
    }
    uint32_t target = ((site + 4u) & 0xF0000000u) | ((word & 0x03FFFFFFu) << 2);
    jfg_poc_prepare_indirect(target);
    jfg_poc_call_indirect(rdram, ctx);
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

static int guest_word(uint8_t *rdram, uint32_t address, uint32_t *value) {
    if (address < 0x80000000u || address > 0x807FFFFCu || (address & 3)) return -1;
    memcpy(value, rdram + address - 0x80000000u, 4);
    return 0;
}

static int sync_game_sections(uint8_t *rdram) {
    uint32_t table, count;
    if (guest_word(rdram, game_table_global, &table) || guest_word(rdram, game_count_global, &count)) return -1;
    if (!count || count > 158 || table < 0x80000000u || table > 0x80800000u - count * 32) return -1;
    int32_t next[JFG_POC_SECTION_COUNT];
    for (uint32_t i = 0; i < JFG_POC_SECTION_COUNT; i++) {
        const struct PocSection *section = &poc_sections[i];
        uint32_t base = section->fixed_base;
        if (section->overlay) {
            if (section->overlay >= count || guest_word(rdram, table + section->overlay * 32, &base)) return -1;
        }
        if (base && ((base & 3) || base < 0x80000000u || base >= 0x80800000u || section->memory_size > 0x80800000u - base)) return -1;
        next[i] = (int32_t)base;
    }
    for (uint32_t i = 0; i < JFG_POC_SECTION_COUNT; i++) {
        if (!next[i]) continue;
        for (uint32_t j = 0; j < i; j++) {
            if (next[j] && (uint32_t)next[i] < (uint32_t)next[j] + poc_sections[j].memory_size &&
                (uint32_t)next[j] < (uint32_t)next[i] + poc_sections[i].memory_size) return -1;
        }
    }
    memcpy(section_bases, next, sizeof(next));
    return 0;
}

static int sync_watched_game_sections(uint8_t *rdram) {
    if (game_linker_pending) {
        uint32_t table, count;
        if (guest_word(rdram, game_table_global, &table) ||
            guest_word(rdram, game_count_global, &count)) return -1;
        if (!table && !count) return 0;
        if (!table || !count) return -1;
    }
    if (sync_game_sections(rdram)) return -1;
    game_linker_pending = 0;
    return 0;
}

POC_EXPORT int jfg_poc_bind_game_linker(uint8_t *rdram, size_t size, uint32_t table_global, uint32_t count_global) {
    if (!rdram || size != 0x800000u || dispatch_active) return -1;
    game_table_global = table_global;
    game_count_global = count_global;
    game_linker_pending = 0;
    int status = sync_game_sections(rdram);
    if (status) { game_table_global = 0; game_count_global = 0; }
    return status;
}

POC_EXPORT int jfg_poc_watch_game_linker(uint8_t *rdram, size_t size, uint32_t table_global, uint32_t count_global) {
    if (!rdram || size != 0x800000u || dispatch_active || table_global == count_global) return -1;
    uint32_t table, count;
    if (guest_word(rdram, table_global, &table) || guest_word(rdram, count_global, &count)) return -1;
    if ((!table && count) || (table && !count)) return -1;
    game_table_global = table_global;
    game_count_global = count_global;
    game_linker_pending = !table;
    if (!game_linker_pending && sync_game_sections(rdram)) {
        game_table_global = game_count_global = 0;
        return -1;
    }
    return 0;
}

POC_EXPORT void jfg_poc_unbind_game_linker(void) {
    game_table_global = game_count_global = 0;
    game_linker_pending = 0;
}

POC_EXPORT int jfg_poc_run(uint32_t address, uint8_t *rdram, size_t size,
                           const uint64_t *input, uint64_t *output) {
    if (!rdram || !input || !output || size != 0x800000u || dispatch_active) return -1;
    /* Reject stale overlay entries before executing any guest instruction. */
    if (game_table_global && sync_watched_game_sections(rdram)) return -7;
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
    indirect_prepared = 0;
    error_target = error_site = 0;
    missing_function_registers_valid = 0;
    if (setjmp(dispatch_guard)) {
        dispatch_active = 0;
        if (missing_function_registers_valid) {
            memcpy(output, missing_function_registers, sizeof(missing_function_registers));
        }
        return dispatch_error;
    }
    function(rdram, &ctx);
    if (game_table_global && sync_watched_game_sections(rdram)) dispatch_fail(-7);
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
