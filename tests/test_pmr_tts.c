#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pmr_tts.h"

static PmrTts tts;

static void spells(const char* text, const char* expected) {
    char actual[256];
    assert(pmr_tts_load(&tts, text, 85, 135));
    pmr_tts_describe(&tts, actual, sizeof(actual));
    if(strcmp(actual, expected)) {
        fprintf(stderr, "%s: expected [%s], got [%s]\n", text, expected, actual);
        abort();
    }
}
static uint32_t render_all(int16_t* copy, uint32_t limit, int32_t* peak, double* power) {
    int16_t block[97]; // not a multiple of the frame size
    uint32_t total = 0;
    size_t n;
    while((n = pmr_tts_render(&tts, block, 97)) != 0) {
        for(size_t i = 0; i < n; i++) {
            int32_t size = abs(block[i]);
            if(peak && size > *peak) *peak = size;
            if(power) *power += (double)block[i] * block[i];
            if(copy && total + i < limit) copy[total + i] = block[i];
        }
        total += (uint32_t)n;
        assert(total < 6000U * 120U); // always terminates
    }
    return total;
}

int main(void) {
    // Rules, word list, digits, symbols, letter names and stress.
    spells("testing", "t'EstIN  ");
    spells("station", "st'eS@n  ");
    spells("location", "lok'eS@n  ");
    spells("the battery", "D@ b'atRi  ");
    spells("me", "m'i  ");
    spells("knife night", "n'Yf n'Yt  ");
    spells("channel 9", "C'an@l n'Yn  ");
    spells("a+b", "@ pl'Vs b'i  ");
    spells("pmr", "p'i 'Em 'Ar  ");
    spells("Don't stop", "d'ont st'Ap  ");
    spells("snake_case", "sn'ek k'es  ");

    // Text with nothing to say must not reach the transmitter.
    assert(!pmr_tts_load(&tts, "", 85, 135));
    assert(!pmr_tts_load(&tts, "...", 85, 135));
    assert(!pmr_tts_load(&tts, " ,_~ ", 85, 135));

    // Level stays inside the 0.85 of the stored prompts and is not silence.
    assert(pmr_tts_load(&tts, "Hello, this is a test message. Can you hear me?", 85, 135));
    static int16_t first[6000 * 8], second[6000 * 8];
    int32_t peak = 0;
    double power = 0;
    uint32_t samples = render_all(first, 6000 * 8, &peak, &power);
    double rms = 0;
    assert(samples > 6000 * 2 && samples < 6000 * 8);
    rms = power / samples;
    assert(peak <= (int32_t)(32767 * 0.85) + 1);
    assert(rms > 1000.0 * 1000.0);
    assert(pmr_tts_render(&tts, first, 1) == 0); // stays finished

    // Every repeat is the same audio.
    pmr_tts_rewind(&tts);
    assert(render_all(second, 6000 * 8, NULL, NULL) == samples);
    assert(!memcmp(first, second, samples * sizeof(int16_t)));

    // Speed changes duration in the expected direction.
    assert(pmr_tts_load(&tts, "Hello, this is a test message.", 70, 135));
    uint32_t slow = render_all(NULL, 0, NULL, NULL);
    assert(pmr_tts_load(&tts, "Hello, this is a test message.", 105, 135));
    uint32_t fast = render_all(NULL, 0, NULL, NULL);
    assert(slow > fast * 13 / 10);

    // A full ordinary message fits inside the 60 s transmission limit.
    char text[161];
    memset(text, 0, sizeof(text));
    while(strlen(text) + 20 <= 160)
        strcat(text, "this is a long one. ");
    assert(pmr_tts_load(&tts, text, 70, 135));
    assert(render_all(NULL, 0, NULL, NULL) < 6000U * 59U);

    // Worst case input is cut at a word, without overflow.
    memset(text, '7', 160);
    text[160] = 0;
    assert(pmr_tts_load(&tts, text, 85, 135));
    assert(tts.count <= PMR_TTS_PHONES_MAX);
    render_all(NULL, 0, NULL, NULL);
    memset(text, 'x', 160); // one enormous word
    assert(pmr_tts_load(&tts, text, 85, 135));
    render_all(NULL, 0, NULL, NULL);

    // Out-of-range voice settings are clamped, not rejected.
    assert(pmr_tts_load(&tts, "hello", 0, 100000));
    assert(tts.speed_percent == 50 && tts.pitch_hz == 240);
    render_all(NULL, 0, NULL, NULL);
    puts("tts ok");
    return 0;
}
