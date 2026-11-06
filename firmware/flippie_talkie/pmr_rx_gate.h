#pragma once
#include <stdbool.h>
#include <stdint.h>

// Learn the on-slope level of a carrier already confirmed at channel centre.
// A fixed absolute squelch threshold can otherwise interrupt a stable carrier
// every 0.6/1.2/2.4 seconds, including carriers that suppress the noise floor.
typedef struct {
    int32_t floor, reference, sum;
    uint32_t count, started, last_present;
    bool learned, ambiguous;
} PmrRxGate;
static inline void pmr_rx_gate_start(PmrRxGate* g, int32_t floor, uint32_t now) {
    *g = (PmrRxGate){.floor = floor, .started = now, .last_present = now};
}
static inline void pmr_rx_gate_update(PmrRxGate* g, int32_t level, uint32_t now) {
    if(!g->learned) {
        g->sum += level;
        g->count++;
        if((uint32_t)(now - g->started) < 200) return;
        g->reference = g->sum / (int32_t)g->count;
        g->learned = true;
        int32_t contrast = g->reference - g->floor;
        g->ambiguous = contrast > -4 && contrast < 4;
        g->last_present = now;
    }
    int32_t contrast = g->reference - g->floor;
    bool present;
    if(g->ambiguous) {
        int32_t difference = level - g->reference;
        present = difference > -4 && difference < 4;
    } else if(contrast > 0)
        present = level - g->floor > contrast / 3;
    else
        present = g->floor - level > (-contrast) / 3;
    if(present) g->last_present = now;
}
static inline bool pmr_rx_gate_needs_check(const PmrRxGate* g, uint32_t now) {
    if(!g->learned) return false;
    // Only indistinguishable carrier/noise levels require a periodic fallback.
    return (uint32_t)(now - g->last_present) > 600 ||
           (g->ambiguous && (uint32_t)(now - g->started) > 8000);
}
