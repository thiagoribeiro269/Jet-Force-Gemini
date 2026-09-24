/* Small console diagnostic; no window, GPU or ROM file is opened. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bridge.h"
#include "poc_smoke.h"

int main(void) {
    const size_t size = 0x800000;
    uint8_t *memory = calloc(1, size);
    uint64_t input[32] = {0}, output[32] = {0};
    int status = EXIT_FAILURE;
    if (!memory || sizeof(void *) != 8) goto cleanup;
    if (jfg_poc_set_section(POC_MAIN_SECTION, POC_MAIN_BASE)) goto cleanup;
    uint32_t getter = jfg_poc_address("mainGetZBCheck");
    uint32_t setter = jfg_poc_address("mainSetMode");
    if (!getter || !setter) goto cleanup;
    input[4] = UINT64_MAX;
    if (jfg_poc_run(getter, memory, size, input, output) || output[2] != 1) goto cleanup;
    input[4] = 8;
    if (jfg_poc_run(getter, memory, size, input, output) || output[2] != 1) goto cleanup;
    input[4] = 0;
    if (jfg_poc_run(getter, memory, size, input, output) || output[2] != 0) goto cleanup;
    input[4] = 0x12345678;
    if (jfg_poc_run(setter, memory, size, input, output)) goto cleanup;
    uint32_t stored;
    memcpy(&stored, memory + (POC_GAME_MODE - 0x80000000u), sizeof(stored));
    if (stored != 0x12345678u) goto cleanup;
    printf("PASS: JFG native x64 CPU diagnostic (%u registered functions).\n", jfg_poc_function_count());
    puts("This is a CPU proof, not a playable game; no graphics or audio are initialized.");
    status = EXIT_SUCCESS;
cleanup:
    if (status != EXIT_SUCCESS) fputs("FAIL: JFG CPU diagnostic.\n", stderr);
    free(memory);
    return status;
}
