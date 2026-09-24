#pragma once
#include "bridge.h"
#ifdef __cplusplus
extern "C" {
#endif
POC_EXPORT int jfg_io_begin(uint8_t* ram);
POC_EXPORT int jfg_io_end(uint8_t* ram);
POC_EXPORT int jfg_io_pump(uint8_t* ram, uint32_t budget);
POC_EXPORT int jfg_io_state(uint64_t* fields, size_t count);
int jfg_io_manager(uint8_t* ram, int32_t priority, uint32_t queue, uint32_t buffer, int32_t capacity);
int jfg_io_submit(uint8_t* ram, uint32_t message, int32_t priority, int32_t direction,
                  uint32_t source, uint32_t destination, uint32_t size, uint32_t return_queue, int32_t* result);
#ifdef __cplusplus
}
#endif
