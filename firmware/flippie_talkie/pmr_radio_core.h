#pragma once
#include <stdint.h>
#include <stdbool.h>

static inline uint32_t pmr_channel_hz(uint32_t channel) {
    return channel>=1 && channel<=16 ? 446006250UL+(channel-1)*12500UL : 0;
}
static inline uint32_t pmr_checksum(const uint8_t* bytes,uint32_t size) {
    uint32_t hash=2166136261UL;
    for(uint32_t i=0;i<size;i++) hash=(hash^bytes[i])*16777619UL;
    return hash;
}
typedef struct {
    const uint8_t* bits;
    uint32_t count,position;
} PmrPdm;
static inline bool pmr_pdm_bit(const PmrPdm* pdm,uint32_t i) {
    return (pdm->bits[i/8]>>(i%8))&1;
}
// Coalesce equal PDM bits. The older firmware inserts an extra edge for equal
// consecutive levels; merging runs preserves both the waveform and its timing.
static inline bool pmr_pdm_next(PmrPdm* pdm,bool* level,uint32_t* duration) {
    if(pdm->position>=pdm->count) return false;
    uint32_t start=pdm->position;
    bool value=pmr_pdm_bit(pdm,start);
    do {pdm->position++;} while(pdm->position<pdm->count && pmr_pdm_bit(pdm,pdm->position)==value);
    *level=value;
    *duration=21*(pdm->position-start)-(pdm->position/6-start/6);
    return true;
}
