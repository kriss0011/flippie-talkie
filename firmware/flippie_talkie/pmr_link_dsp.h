#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

typedef struct {
    float z[4][2], baseline, deemphasis;
    uint32_t fade, clips;
    bool initialized;
} PmrDsp;

static inline void pmr_dsp_reset(PmrDsp* dsp) {
    uint32_t clips = dsp->clips;
    memset(dsp, 0, sizeof(*dsp));
    dsp->clips = clips;
}

// 8 kHz fourth-order Butterworth: USB 180--1700 Hz, BLE 180--1400 Hz.
// Both retain the existing 450 Hz de-emphasis and fixed gain.
// Fixed gain; bounded, single-precision hardware math in the sampling ISR.
static inline int16_t pmr_dsp_sample_rate(PmrDsp* dsp, float input, bool bluetooth) {
    static const float usb_sos[4][5] = {
        {0.03960266265f, 0.07920532531f, 0.03960266265f, -0.4225871402f, 0.1018973798f},
        {1.0f, 2.0f, 1.0f, -0.3831293958f, 0.5216434222f},
        {1.0f, -2.0f, 1.0f, -1.720117922f, 0.7443408451f},
        {1.0f, -2.0f, 1.0f, -1.891021478f, 0.9108384466f},
    };
    static const float ble_sos[4][5] = {
        {0.0195847967f, 0.0391695934f, 0.0195847967f, -0.7213233429f, 0.1880361728f},
        {1.0f, 2.0f, 1.0f, -0.741875503f, 0.5718788002f},
        {1.0f, -2.0f, 1.0f, -1.712819184f, 0.7394073878f},
        {1.0f, -2.0f, 1.0f, -1.894750985f, 0.9147429947f},
    };
    const float (*sos)[5] = bluetooth ? ble_sos : usb_sos;
    if(!dsp->initialized) {
        dsp->baseline = input;
        dsp->initialized = true;
    }
    float x = input - dsp->baseline;
    for(uint32_t i = 0; i < 4; i++) {
        float y = sos[i][0] * x + dsp->z[i][0];
        dsp->z[i][0] = sos[i][1] * x - sos[i][3] * y + dsp->z[i][1];
        dsp->z[i][1] = sos[i][2] * x - sos[i][4] * y;
        x = y;
    }
    dsp->deemphasis += 0.2977242695f * (x - dsp->deemphasis);
    float y = dsp->deemphasis * 6000.0f;
    if(dsp->fade < 80) y *= (float)++dsp->fade / 80.0f;
    if(y > 32767.0f) {
        dsp->clips++;
        y = 32767.0f;
    }
    if(y < -32767.0f) {
        dsp->clips++;
        y = -32767.0f;
    }
    return (int16_t)y;
}

// Restore the pre-boost speaker mapping. Volume is linear; quiet samples are
// not expanded, and the speaker uses the same de-emphasized PCM as the WAV.
static inline uint32_t pmr_speaker_duty(int16_t pcm, uint32_t volume) {
    if(volume > 8) volume = 8;
    return (uint32_t)(512 + (int32_t)pcm * (int32_t)volume / 1024);
}
