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

POC_EXPORT uint32_t jfg_poc_function_count(void);
POC_EXPORT uint32_t jfg_poc_address(const char *name);
POC_EXPORT int jfg_poc_set_section(uint32_t index, uint32_t base);
POC_EXPORT int jfg_poc_run(uint32_t address, uint8_t *rdram, size_t size,
                           const uint64_t *input, uint64_t *output);
