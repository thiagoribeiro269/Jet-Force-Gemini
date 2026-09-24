#pragma once
#include "bridge.h"
#ifdef __cplusplus
extern "C" {
#endif

POC_EXPORT int jfg_events_begin(uint8_t* ram, uint32_t table, int deterministic);
POC_EXPORT int jfg_events_end(uint8_t* ram);
POC_EXPORT int jfg_events_advance(uint8_t* ram, uint64_t ticks);
POC_EXPORT int jfg_events_wait(uint8_t* ram, uint32_t max_ms);
POC_EXPORT int jfg_events_post(uint8_t* ram, uint32_t event);
POC_EXPORT int jfg_events_cancel_timer(uint8_t* ram, uint32_t timer);
POC_EXPORT int jfg_events_state(uint64_t* fields, size_t count);
int jfg_events_bind(uint8_t* ram, uint32_t event, uint32_t queue, uint32_t message);
int jfg_events_vi_bind(uint8_t* ram, uint32_t queue, uint32_t message, uint32_t retraces);
int jfg_events_vi_manager(uint8_t* ram, int32_t priority);
int jfg_events_vi_mode(uint8_t* ram, uint32_t mode);
int jfg_events_vi_black(uint8_t* ram, uint32_t black);
int jfg_events_vi_features(uint8_t* ram, uint32_t features);
POC_EXPORT int jfg_events_vi_state(uint32_t* fields, size_t count);
int jfg_events_clock(uint8_t* ram, int operation, uint64_t value, uint64_t* result);
int jfg_events_timer(uint8_t* ram, uint32_t timer, uint64_t countdown, uint64_t interval,
                      uint32_t queue, uint32_t message);
int jfg_events_mask(uint8_t* ram, uint32_t value, uint32_t* previous);

#ifdef __cplusplus
}
#endif
