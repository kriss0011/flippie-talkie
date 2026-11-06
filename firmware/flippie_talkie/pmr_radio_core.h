#pragma once
#include <stdint.h>
#include <stdbool.h>

#define PMR_TX_LEAD_MS   500U
#define PMR_TX_LEAD_BITS (48U * PMR_TX_LEAD_MS)
// Quiet balanced PDM opens the receiving radio before the first voice sample.
static inline bool pmr_tx_lead_pulse(uint32_t* remaining, bool* level, uint32_t* duration) {
    if(!*remaining) return false;
    uint32_t emitted = PMR_TX_LEAD_BITS - (*remaining)--;
    *level = (emitted % 2) == 0;
    *duration = (emitted % 6) == 5 ? 20 : 21;
    return true;
}
static inline uint32_t pmr_tx_drain_ms(uint32_t lead_bits, uint32_t queued_samples) {
    return (lead_bits * 1000U + 47999U) / 48000U + (queued_samples * 1000U + 5999U) / 6000U + 50U;
}
static inline uint32_t pmr_channel_hz(uint32_t channel) {
    return channel >= 1 && channel <= 16 ? 446006250UL + (channel - 1) * 12500UL : 0;
}
static inline uint32_t pmr_checksum(const uint8_t* bytes, uint32_t size) {
    uint32_t hash = 2166136261UL;
    for(uint32_t i = 0; i < size; i++)
        hash = (hash ^ bytes[i]) * 16777619UL;
    return hash;
}
typedef struct {
    const uint8_t* bits;
    uint32_t count, position, lead_remaining;
} PmrPdm;
static inline bool pmr_pdm_bit(const PmrPdm* pdm, uint32_t i) {
    return (pdm->bits[i / 8] >> (i % 8)) & 1;
}
// Coalesce equal PDM bits. The older firmware inserts an extra edge for equal
// consecutive levels; merging runs preserves both the waveform and its timing.
static inline bool pmr_pdm_next(PmrPdm* pdm, bool* level, uint32_t* duration) {
    if(pdm->position >= pdm->count) return false;
    uint32_t start = pdm->position;
    bool value = pmr_pdm_bit(pdm, start);
    do {
        pdm->position++;
    } while(pdm->position < pdm->count && pmr_pdm_bit(pdm, pdm->position) == value);
    *level = value;
    *duration = 21 * (pdm->position - start) - (pdm->position / 6 - start / 6);
    return true;
}
