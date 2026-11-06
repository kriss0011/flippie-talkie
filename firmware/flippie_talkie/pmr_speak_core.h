#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "pmr_radio_core.h"

// Settings and the radio modulator for spoken messages, free of Flipper APIs
// so that the host tests compile exactly this code.
#define PMR_SPEAK_TEXT_MAX    160U
#define PMR_SPEAK_REPEATS_MAX 99U
#define PMR_SPEAK_PAUSE_MAX   60U
#define PMR_SPEAK_RING        2048U // 341 ms at 6 kHz; a power of two

// Percent and hertz. Normal and Mid scored best through a simulated radio.
static const uint8_t pmr_speak_speeds[3] = {70, 85, 105};
static const uint8_t pmr_speak_pitches[3] = {105, 135, 165};

typedef struct {
    char text[PMR_SPEAK_TEXT_MAX + 1];
    uint8_t repeats, pause_s, speed, pitch;
} PmrSpeakSettings;

static inline void pmr_speak_defaults(PmrSpeakSettings* s) {
    memset(s, 0, sizeof(*s));
    strcpy(s->text, "Hello, this is a test message.");
    s->repeats = 1;
    s->pause_s = 5;
    s->speed = 1;
    s->pitch = 1;
}
// Writes "PMRSPEAK1 repeats pause speed pitch", a newline, then the message,
// so that a message can also be prepared on a computer.
static inline size_t pmr_speak_encode(const PmrSpeakSettings* s, char* out, size_t size) {
    int n = snprintf(
        out,
        size,
        "PMRSPEAK1 %u %u %u %u\n%s\n",
        s->repeats,
        s->pause_s,
        s->speed,
        s->pitch,
        s->text);
    return n < 0 || (size_t)n >= size ? 0 : (size_t)n;
}
static inline bool pmr_speak_number(const char** text, uint32_t* value) {
    const char* p = *text;
    while(*p == ' ')
        p++;
    if(*p < '0' || *p > '9') return false;
    uint32_t number = 0;
    while(*p >= '0' && *p <= '9' && number < 1000)
        number = number * 10 + (uint32_t)(*p++ - '0');
    *value = number;
    *text = p;
    return true;
}
// Leaves the settings unchanged unless the whole file is valid.
static inline bool pmr_speak_decode(const char* data, PmrSpeakSettings* s) {
    if(strncmp(data, "PMRSPEAK1", 9)) return false;
    const char* p = data + 9;
    uint32_t repeats, pause, speed, pitch;
    if(!pmr_speak_number(&p, &repeats) || !pmr_speak_number(&p, &pause) ||
       !pmr_speak_number(&p, &speed) || !pmr_speak_number(&p, &pitch))
        return false;
    if(repeats < 1 || repeats > PMR_SPEAK_REPEATS_MAX || pause < 1 ||
       pause > PMR_SPEAK_PAUSE_MAX || speed > 2 || pitch > 2 || *p != '\n')
        return false;
    p++;
    char text[PMR_SPEAK_TEXT_MAX + 1];
    size_t n = 0;
    while(*p && n < PMR_SPEAK_TEXT_MAX) {
        char c = *p++;
        if(c == '\r') continue;
        // A trailing newline ends the message; others read as a pause.
        if(c == '\n') {
            if(!*p) break;
            c = ' ';
        }
        text[n++] = (unsigned char)c < 32 ? ' ' : c;
    }
    text[n] = 0;
    if(!n) return false;
    memcpy(s->text, text, n + 1);
    s->repeats = (uint8_t)repeats;
    s->pause_s = (uint8_t)pause;
    s->speed = (uint8_t)speed;
    s->pitch = (uint8_t)pitch;
    return true;
}

// Spoken audio peaks at 0.85, so at full volume this drives nearly the whole PWM
// range. Listen's mapping is gentler because received audio is much louder.
static inline uint32_t pmr_speak_speaker_duty(int16_t pcm, uint32_t volume) {
    if(volume > 8) volume = 8;
    int32_t duty = 512 + (int32_t)pcm * (int32_t)volume / 460;
    return duty < 8 ? 8 : duty > 1015 ? 1015 : (uint32_t)duty;
}

// PCM ring to PDM bits: the same 48 kbit/s first-order modulator and 500 ms
// lead-in as browser audio. Unlike a held push-to-talk, the stream has an end.
typedef struct {
    int16_t ring[PMR_SPEAK_RING];
    volatile uint32_t head, tail, underruns;
    volatile bool finished;
    uint32_t lead_bits, phase, slot;
    int32_t error;
    int16_t sample;
    bool carry, carry_valid;
} PmrSpeakTx;

static inline void pmr_speak_tx_reset(PmrSpeakTx* tx) {
    tx->head = tx->tail = tx->underruns = 0;
    tx->finished = false;
    tx->lead_bits = PMR_TX_LEAD_BITS;
    tx->phase = tx->slot = 0;
    tx->error = 0;
    tx->sample = 0;
    tx->carry_valid = false;
}
static inline uint32_t pmr_speak_tx_space(const PmrSpeakTx* tx) {
    return PMR_SPEAK_RING - (tx->head - tx->tail);
}
static inline void pmr_speak_tx_push(PmrSpeakTx* tx, const int16_t* pcm, uint32_t count) {
    uint32_t head = tx->head;
    for(uint32_t i = 0; i < count; i++)
        tx->ring[(head + i) & (PMR_SPEAK_RING - 1)] = pcm[i];
    __sync_synchronize();
    tx->head = head + count;
}
static inline bool pmr_speak_tx_pop(PmrSpeakTx* tx, int16_t* sample) {
    uint32_t tail = tx->tail;
    if(tail == tx->head) return false;
    *sample = tx->ring[tail & (PMR_SPEAK_RING - 1)];
    __sync_synchronize();
    tx->tail = tail + 1;
    return true;
}
static inline bool pmr_speak_tx_bit(PmrSpeakTx* tx, bool* bit) {
    // Queued speech waits for the receiver-opening prefix to finish.
    if(tx->lead_bits) {
        *bit = (--tx->lead_bits & 1U) != 0;
        return true;
    }
    if(tx->phase == 0 && !pmr_speak_tx_pop(tx, &tx->sample)) {
        if(tx->finished) return false;
        tx->sample = 0;
        tx->underruns++;
    }
    tx->phase = (tx->phase + 1) % 8;
    tx->error += tx->sample;
    *bit = tx->error >= 0;
    tx->error += *bit ? -32767 : 32768;
    return true;
}
// Called from the radio DMA interrupt. Returns false at the end of the stream.
static inline bool pmr_speak_tx_next(PmrSpeakTx* tx, bool* level, uint32_t* duration) {
    bool value;
    if(tx->carry_valid) {
        value = tx->carry;
        tx->carry_valid = false;
    } else if(!pmr_speak_tx_bit(tx, &value))
        return false;
    uint32_t us = 0;
    for(uint32_t run = 0; run < 256; run++) {
        tx->slot++;
        us += tx->slot == 6 ? 20 : 21;
        if(tx->slot == 6) tx->slot = 0;
        bool next;
        if(!pmr_speak_tx_bit(tx, &next)) break;
        if(next != value || run == 255) {
            tx->carry = next;
            tx->carry_valid = true;
            break;
        }
    }
    *level = value;
    *duration = us;
    return true;
}
