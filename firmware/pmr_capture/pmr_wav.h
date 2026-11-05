#pragma once
#include <stdint.h>
#include <string.h>

static inline void pmr_wav_u32(uint8_t* p,uint32_t v) {
    p[0]=v;p[1]=v>>8;p[2]=v>>16;p[3]=v>>24;
}

// A standard PCM WAV with a JUNK chunk aligns audio at an SD sector.
static inline void pmr_wav_header(uint8_t h[512],uint32_t frames) {
    memset(h,0,512);
    memcpy(h,"RIFF",4);pmr_wav_u32(h+4,504+frames*2);
    memcpy(h+8,"WAVEfmt ",8);pmr_wav_u32(h+16,16);
    h[20]=1;h[22]=1;
    pmr_wav_u32(h+24,8000);pmr_wav_u32(h+28,16000);
    h[32]=2;h[34]=16;
    memcpy(h+36,"JUNK",4);pmr_wav_u32(h+40,460);
    memcpy(h+504,"data",4);pmr_wav_u32(h+508,frames*2);
}
