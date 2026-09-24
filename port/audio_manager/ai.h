#pragma once
#include "bridge.h"

#ifdef __cplusplus
extern "C" {
#endif

/* State fields: bound, clock, requested, result, DAC, BIT, CONTROL,
 * cumulative writes, rejected, last write addresses in execution order. */
enum { JFG_AI_STATE_FIELDS = 12 };
POC_EXPORT int jfg_ai_begin(uint8_t* ram, uint32_t clock_symbol);
POC_EXPORT int jfg_ai_end(uint8_t* ram);
POC_EXPORT int jfg_ai_state(uint32_t* fields, size_t count);
/* Supported request domain: 1..UINT32_MAX; frequency 0 is an adapter error. */
POC_EXPORT int jfg_ai_frequency(uint8_t* ram, uint32_t frequency, int32_t* result);

#ifdef __cplusplus
}
#endif
