#pragma once
#include <stddef.h>
#include <stdint.h>

#ifdef _WIN32
#ifdef JFG_POC_BUILD
#define POC_EXPORT __declspec(dllexport)
#else
#define POC_EXPORT __declspec(dllimport)
#endif
#else
#define POC_EXPORT __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

POC_EXPORT uint32_t jfg_poc_function_count(void);
POC_EXPORT int jfg_poc_is_callable(uint32_t address);
POC_EXPORT uint32_t jfg_poc_address(const char *name);
POC_EXPORT uint32_t jfg_poc_last_call_count(void);
POC_EXPORT uint32_t jfg_poc_last_call_target(void);
POC_EXPORT uint64_t jfg_poc_last_return_address(void);
POC_EXPORT uint32_t jfg_poc_error_target(void);
POC_EXPORT uint32_t jfg_poc_error_site(void);
POC_EXPORT size_t jfg_boot_rom_size(void);
POC_EXPORT int jfg_boot_rom_read(uint8_t* ram, uint32_t source, uint32_t destination, uint32_t length);
POC_EXPORT int jfg_poc_set_section(uint32_t index, uint32_t base);
POC_EXPORT int jfg_poc_load_section(uint32_t index, uint32_t base, uint8_t *rdram,
                                    size_t ram_size, const uint8_t *rom, size_t rom_size);
POC_EXPORT int jfg_poc_unload_section(uint32_t index);
POC_EXPORT int jfg_poc_bind_game_linker(uint8_t *rdram, size_t size, uint32_t table_global, uint32_t count_global);
POC_EXPORT int jfg_poc_watch_game_linker(uint8_t *rdram, size_t size, uint32_t table_global, uint32_t count_global);
POC_EXPORT void jfg_poc_unbind_game_linker(void);
POC_EXPORT int jfg_poc_run(uint32_t address, uint8_t *rdram, size_t size,
                           const uint64_t *input, uint64_t *output);

#ifdef __cplusplus
}
#endif
