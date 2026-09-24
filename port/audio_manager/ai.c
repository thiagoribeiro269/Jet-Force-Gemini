/* Headless AI register contract from JFG US osAiSetFrequency. No sample output. */
#include <string.h>
#include "ai.h"
#include "scheduler.h"

enum { RAM_BASE = 0x80000000u, RAM_END = 0x80800000u };
enum { VI_PAL_CLOCK = 49656530, VI_NTSC_CLOCK = 48681812, VI_MPAL_CLOCK = 48628316 };
enum { AI_DACRATE = 0x04500010u, AI_BITRATE = 0x04500014u, AI_CONTROL = 0x04500008u };

static struct {
    uint8_t* ram;
    uint32_t clock_symbol;
    uint32_t fields[JFG_AI_STATE_FIELDS];
} ai;

static int valid_clock(uint32_t clock) {
    return clock == VI_PAL_CLOCK || clock == VI_NTSC_CLOCK || clock == VI_MPAL_CLOCK;
}

int jfg_ai_begin(uint8_t* ram, uint32_t clock_symbol) {
    if (ai.ram || !jfg_threads_context(ram, 1) || clock_symbol < RAM_BASE ||
        clock_symbol > RAM_END - sizeof(uint32_t) || (clock_symbol & 3u)) return -1;
    memset(&ai, 0, sizeof(ai));
    ai.ram = ram;
    ai.clock_symbol = clock_symbol;
    ai.fields[0] = 1;
    return 0;
}

int jfg_ai_end(uint8_t* ram) {
    if (!ai.ram) return 0;
    if (ram != ai.ram || !jfg_threads_context(ram, 1)) return -1;
    memset(&ai, 0, sizeof(ai));
    return 0;
}

int jfg_ai_state(uint32_t* fields, size_t count) {
    if (!fields || count != JFG_AI_STATE_FIELDS ||
        (ai.ram && !jfg_threads_context(ai.ram, 1))) return -1;
    memcpy(fields, ai.fields, sizeof(ai.fields));
    return 0;
}

int jfg_ai_frequency(uint8_t* ram, uint32_t frequency, int32_t* result) {
    uint32_t clock;
    if (!result || !ai.ram || ram != ai.ram || !jfg_threads_context(ram, 0) || frequency == 0) return -1;
    memcpy(&clock, ram + (ai.clock_symbol - RAM_BASE), sizeof(clock));
    if (!valid_clock(clock)) return -1;

    /* The ELF converts the unsigned request to float, divides in single
       precision, adds 0.5f, then truncates an unsigned divisor. The three VI
       clocks and nonzero requests keep that conversion defined. */
    float divisor_float = (float)(int32_t)clock / (float)frequency + 0.5f;
    uint32_t divisor = (uint32_t)divisor_float;
    ai.fields[1] = clock;
    ai.fields[2] = (uint32_t)frequency;
    if (divisor < 132u) {
        *result = -1;
        ai.fields[3] = UINT32_MAX;
        ai.fields[8]++;
        return 0;
    }

    /* The original first narrows to unsigned char, then clamps to 16. */
    uint32_t bitrate = (uint8_t)(divisor / 66u);
    if (bitrate > 16u) bitrate = 16u;
    ai.fields[4] = divisor - 1u;
    ai.fields[5] = bitrate - 1u;
    ai.fields[6] = 1u;
    ai.fields[7] += 3u;
    ai.fields[9] = AI_DACRATE;
    ai.fields[10] = AI_BITRATE;
    ai.fields[11] = AI_CONTROL;
    *result = (int32_t)clock / (int32_t)divisor;
    ai.fields[3] = (uint32_t)*result;
    return 0;
}
