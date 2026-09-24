#pragma once
#include "bridge.h"

#ifdef __cplusplus
extern "C" {
#endif

POC_EXPORT int jfg_threads_begin(uint8_t* ram, size_t size, uint32_t host_slot);
POC_EXPORT int jfg_threads_create(uint8_t* ram, uint32_t slot, int32_t id, uint32_t entry,
                                 uint32_t stack, int32_t priority, const uint64_t* arguments);
POC_EXPORT int jfg_threads_start(uint8_t* ram, uint32_t slot);
POC_EXPORT int jfg_threads_result(uint32_t slot, int32_t* state, int32_t* status, uint64_t* registers);
POC_EXPORT int jfg_threads_end(uint8_t* ram);
POC_EXPORT uint32_t jfg_threads_joined(void);
int jfg_threads_context(uint8_t* ram, int owner_only);
int jfg_threads_queue_known(uint8_t* ram, uint32_t queue);
POC_EXPORT int jfg_threads_queue(uint8_t* ram, uint32_t operation, uint32_t queue,
                                uint32_t value, int32_t argument, int32_t* result);

#ifdef __cplusplus
}
#endif
