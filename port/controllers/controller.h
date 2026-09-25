#pragma once
#include "bridge.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Headless, deterministic SI contract. All four controller ports are absent.
 * State fields: bound, initialized, pending read, reads started, SI messages
 * delivered, SI messages dropped by a full queue, rejected requests, SI queue.
 * Adapter errors: -1 invalid context/address/state, -2 overlapping read,
 * -3 pending completion has no registered SI route. Guest calls return 0.
 */
enum { JFG_CONTROLLER_STATE_FIELDS = 8 };
POC_EXPORT int jfg_controllers_begin(uint8_t* ram);
POC_EXPORT int jfg_controllers_end(uint8_t* ram);
POC_EXPORT int jfg_controllers_pump(uint8_t* ram);
POC_EXPORT int jfg_controllers_state(uint64_t* fields, size_t count);

int jfg_controllers_init(uint8_t* ram, uint32_t queue, uint32_t pattern,
                         uint32_t status, int32_t* result);
int jfg_controllers_start_read(uint8_t* ram, uint32_t queue, int32_t* result);

#ifdef __cplusplus
}
#endif
