#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "pmr_speak_core.h"

static PmrSpeakTx tx;

// Runs the modulator exactly as the radio DMA does, returning microseconds.
static uint64_t drain(uint32_t* bits_out, float* decoded, uint32_t decoded_size) {
    bool level;
    uint32_t duration, last_level = 2;
    uint64_t total = 0;
    uint32_t bits = 0;
    while(pmr_speak_tx_next(&tx, &level, &duration)) {
        assert(duration > 0 && duration <= 256U * 21U);
        assert((uint32_t)level != last_level || duration >= 5000U); // runs are merged
        last_level = level;
        uint32_t count = (duration * 48U + 500U) / 1000U;
        if(decoded)
            for(uint32_t i = 0; i < count && bits + i < decoded_size; i++)
                decoded[bits + i] = level ? 1.0f : -1.0f;
        bits += count;
        total += duration;
    }
    if(bits_out) *bits_out = bits;
    return total;
}

int main(void) {
    // Settings round trip, including text with punctuation.
    PmrSpeakSettings a, b;
    char file[256];
    pmr_speak_defaults(&a);
    assert(a.repeats == 1 && a.pause_s == 5 && a.speed == 1 && a.pitch == 1);
    strcpy(a.text, "Base, come in? 1 2 3");
    a.repeats = 42;
    a.pause_s = 60;
    a.speed = 2;
    a.pitch = 0;
    assert(pmr_speak_encode(&a, file, sizeof(file)));
    memset(&b, 0, sizeof(b));
    assert(pmr_speak_decode(file, &b));
    assert(!memcmp(a.text, b.text, strlen(a.text) + 1));
    assert(b.repeats == 42 && b.pause_s == 60 && b.speed == 2 && b.pitch == 0);

    // A file edited on a computer: CRLF, and line breaks inside the message.
    assert(pmr_speak_decode("PMRSPEAK1 3 10 0 2\nline one\r\nline two\r\n", &b));
    assert(!strcmp(b.text, "line one line two"));

    // Invalid files never replace good settings.
    PmrSpeakSettings kept = b;
    const char* bad[] = {
        "",
        "hello",
        "PMRSPEAK1",
        "PMRSPEAK1 0 5 1 1\ntext\n",
        "PMRSPEAK1 100 5 1 1\ntext\n",
        "PMRSPEAK1 1 0 1 1\ntext\n",
        "PMRSPEAK1 1 61 1 1\ntext\n",
        "PMRSPEAK1 1 5 3 1\ntext\n",
        "PMRSPEAK1 1 5 1 3\ntext\n",
        "PMRSPEAK1 1 5 1 1\n",
        "PMRSPEAK1 1 5 1 1 text\n",
    };
    for(size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        assert(!pmr_speak_decode(bad[i], &b));
        assert(!memcmp(&b, &kept, sizeof(b)));
    }
    char big[400];
    memset(big, 0, sizeof(big));
    strcpy(big, "PMRSPEAK1 1 5 1 1\n");
    memset(big + strlen(big), 'a', 300);
    assert(pmr_speak_decode(big, &b));
    assert(strlen(b.text) == PMR_SPEAK_TEXT_MAX);

    // Full volume uses nearly the whole speaker range and can never leave it.
    assert(pmr_speak_speaker_duty(0, 8) == 512);
    assert(pmr_speak_speaker_duty(27852, 8) > 980);
    assert(pmr_speak_speaker_duty(-27852, 8) < 40);
    assert(pmr_speak_speaker_duty(32767, 8) <= 1015);
    assert(pmr_speak_speaker_duty(-32768, 8) >= 8);
    assert(pmr_speak_speaker_duty(27852, 0) == 512); // muted
    assert(pmr_speak_speaker_duty(27852, 99) == pmr_speak_speaker_duty(27852, 8));

    // An empty message is only the 500 ms lead-in, to the bit.
    pmr_speak_tx_reset(&tx);
    tx.finished = true;
    uint32_t bits;
    uint64_t us = drain(&bits, NULL, 0);
    assert(us >= 499000 && us <= 501000);

    // Queued speech survives the lead-in, and the stream ends by itself.
    pmr_speak_tx_reset(&tx);
    static int16_t pcm[1200];
    for(uint32_t i = 0; i < 1200; i++)
        pcm[i] = (int16_t)(20000.0 * sin(2.0 * 3.14159265 * 500.0 * i / 6000.0));
    assert(pmr_speak_tx_space(&tx) == PMR_SPEAK_RING);
    pmr_speak_tx_push(&tx, pcm, 1200);
    assert(pmr_speak_tx_space(&tx) == PMR_SPEAK_RING - 1200);
    tx.finished = true;
    static float decoded[24000 + 9600 + 64];
    us = drain(&bits, decoded, 24000 + 9600 + 64);
    assert(tx.tail == 1200 && tx.underruns == 0);
    assert(us >= 699000 && us <= 701000); // 500 ms lead + 200 ms of audio
    bool level;
    uint32_t duration;
    assert(!pmr_speak_tx_next(&tx, &level, &duration)); // and stays ended

    // The bit stream carries the waveform: average it back to 6 kHz.
    double dot = 0, a2 = 0, b2 = 0;
    for(uint32_t i = 8; i < 1192; i++) {
        double sum = 0;
        for(uint32_t k = 0; k < 8; k++)
            sum += decoded[24000 + i * 8 + k];
        double got = sum / 8.0, want = pcm[i] / 32767.0;
        dot += got * want;
        a2 += got * got;
        b2 += want * want;
    }
    assert(dot / sqrt(a2 * b2) > 0.95);

    // A slow synthesizer shows up as a counted underrun, not a stall.
    pmr_speak_tx_reset(&tx);
    tx.lead_bits = 0;
    for(uint32_t i = 0; i < 16; i++) {
        bool bit;
        assert(pmr_speak_tx_bit(&tx, &bit));
    }
    assert(tx.underruns == 2);
    puts("speak ok");
    return 0;
}
