#pragma once
#include "recomp.h"

void jfg_poc_require_section(uint32_t section);
void jfg_poc_call(uint32_t section, uint32_t offset, uint8_t *rdram, recomp_context *ctx);
