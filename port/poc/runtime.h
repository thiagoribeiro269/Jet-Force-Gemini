#pragma once
#include "recomp.h"

void jfg_poc_require_section(uint32_t section);
void jfg_poc_call(uint32_t section, uint32_t offset, uint8_t *rdram, recomp_context *ctx);
void jfg_poc_prepare_indirect(uint64_t target);
void jfg_poc_call_indirect(uint8_t *rdram, recomp_context *ctx);
void jfg_poc_require_runtime_patch(uint8_t *rdram, uint32_t address, uint32_t word);
void jfg_poc_fail(int code);
