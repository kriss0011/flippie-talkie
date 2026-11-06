#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Text-to-speech small enough to run on the Flipper: English letter-to-sound
// rules feeding a formant synthesizer. Output is the 6 kHz PCM that the radio
// modulator already consumes, generated on demand; nothing is stored.
#define PMR_TTS_RATE       6000U
#define PMR_TTS_PHONES_MAX 640U
#define PMR_TTS_PARAMS     12U

typedef struct {
    uint8_t code, flags, position;
} PmrTtsPhone;
typedef struct {
    float a, b, c, y1, y2;
} PmrTtsResonator;
typedef struct {
    float p[PMR_TTS_PARAMS], f0_start, f0_end;
    uint16_t frames;
    uint8_t formant_blend, level_blend;
} PmrTtsSegment;

typedef struct {
    PmrTtsPhone phones[PMR_TTS_PHONES_MAX];
    uint32_t count, phone, sub;
    uint32_t speed_percent, pitch_hz;
    PmrTtsSegment previous, current, next;
    uint32_t segment_frame, frame_samples;
    bool finished;
    PmrTtsResonator formant[3], noise;
    float noise_x1, noise_x2;
    float voice, voice_step, aspiration, aspiration_step, frication, frication_step;
    float f0, phase, tilt, highpass_x, highpass_y, shelf_x, shelf_y;
    float notch_c, notch_depth, notch_x1, notch_x2;
    float history[32];
    uint32_t history_at;
    uint32_t random;
} PmrTts;

// Parses text into phonemes. Returns false only when nothing is speakable.
// Text longer than the phoneme store is cut at a word boundary.
bool pmr_tts_load(PmrTts* tts, const char* text, uint32_t speed_percent, uint32_t pitch_hz);
// Rewinds to the start of the loaded text, so that repeats are identical.
void pmr_tts_rewind(PmrTts* tts);
// Fills up to count samples and returns how many were written; 0 at the end.
size_t pmr_tts_render(PmrTts* tts, int16_t* out, size_t count);
// Writes the phoneme spelling, for tests and diagnostics.
void pmr_tts_describe(const PmrTts* tts, char* out, size_t size);
