#pragma once
#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint32_t segment, elapsed_us;
    bool level;
} PmrTxTone;
// Two groups of 500 / 1000 / 1500 Hz, each tone 700 ms followed by 200 ms
// without modulation. The finite sequence is 5.4 seconds in total.
static inline bool pmr_tx_tone_next(PmrTxTone* tone, bool* level, uint32_t* duration) {
    if(tone->segment >= 12) return false;
    bool silence = (tone->segment & 1) != 0;
    uint32_t segment_us = silence ? 200000 : 700000;
    uint32_t frequency = 500 * (1 + (tone->segment / 2) % 3);
    uint32_t remaining = segment_us - tone->elapsed_us;
    uint32_t period = silence ? remaining : 500000 / frequency;
    if(period > remaining) period = remaining;
    tone->level = silence ? false : !tone->level;
    *level = tone->level;
    *duration = period;
    tone->elapsed_us += period;
    if(tone->elapsed_us == segment_us) {
        tone->elapsed_us = 0;
        tone->segment++;
    }
    return true;
}
