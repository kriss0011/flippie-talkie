// Writes what the Flipper would transmit to a WAV file, without any radio.
//   cc -std=c11 -O2 -I firmware/flippie_talkie tools/tts_preview.c \
//      firmware/flippie_talkie/pmr_tts.c -lm -o build/tts_preview
//   build/tts_preview "Hello, this is a test." hello.wav [speed%] [pitch Hz]
#include "pmr_tts.h"
#include <stdio.h>
#include <stdlib.h>

static void u32(FILE* f, uint32_t v) {
    uint8_t b[4] = {(uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24)};
    fwrite(b, 1, 4, f);
}
static void u16(FILE* f, uint16_t v) {
    uint8_t b[2] = {(uint8_t)v, (uint8_t)(v >> 8)};
    fwrite(b, 1, 2, f);
}
int main(int argc, char** argv) {
    if(argc < 3) {
        fprintf(stderr, "usage: %s text out.wav [speed%%] [pitch Hz]\n", argv[0]);
        return 2;
    }
    static PmrTts tts;
    uint32_t speed = argc > 3 ? (uint32_t)atoi(argv[3]) : 85;
    uint32_t pitch = argc > 4 ? (uint32_t)atoi(argv[4]) : 135;
    if(!pmr_tts_load(&tts, argv[1], speed, pitch)) {
        fprintf(stderr, "nothing to say\n");
        return 1;
    }
    char phonemes[2048];
    pmr_tts_describe(&tts, phonemes, sizeof(phonemes));
    printf("%s\n", phonemes);
    FILE* f = fopen(argv[2], "wb");
    if(!f) return 1;
    fwrite("RIFF\0\0\0\0WAVEfmt ", 1, 16, f);
    u32(f, 16);
    u16(f, 1);
    u16(f, 1);
    u32(f, PMR_TTS_RATE);
    u32(f, PMR_TTS_RATE * 2);
    u16(f, 2);
    u16(f, 16);
    fwrite("data\0\0\0\0", 1, 8, f);
    uint32_t samples = 0;
    int16_t block[256];
    size_t n;
    while((n = pmr_tts_render(&tts, block, 256)) != 0) {
        fwrite(block, 2, n, f);
        samples += (uint32_t)n;
    }
    fseek(f, 4, SEEK_SET);
    u32(f, 36 + samples * 2);
    fseek(f, 40, SEEK_SET);
    u32(f, samples * 2);
    fclose(f);
    printf("%.2f s\n", samples / (double)PMR_TTS_RATE);
    return 0;
}
