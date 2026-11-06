#pragma once
#include <cli/cli.h>
#include <toolbox/pipe.h>

// CLI commands now receive their own pipe, not the global CLI service.
static inline size_t pmr_cli_read(PipeSide* pipe, void* data, size_t size, uint32_t timeout) {
    uint32_t start = furi_get_tick();
    while(pipe_state(pipe) == PipeStateOpen) {
        size_t available = pipe_bytes_available(pipe);
        if(available) return pipe_receive(pipe, data, MIN(size, available));
        if(furi_get_tick() - start >= timeout) break;
        furi_delay_tick(1);
    }
    return 0;
}
