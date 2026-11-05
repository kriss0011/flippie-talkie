#pragma once
#include <stdint.h>
#include <stddef.h>
#define VOICE_RATE 6000U
#define RECEIVE_RATE 4000U
#define VOICE_SAMPLES 321U
#define VOICE_BYTES 164U
static const int16_t ima_steps[89]={7,8,9,10,11,12,13,14,16,17,19,21,23,25,28,31,34,37,41,45,50,55,60,66,73,80,88,97,107,118,130,143,157,173,190,209,230,253,279,307,337,371,408,449,494,544,598,658,724,796,876,963,1060,1166,1282,1411,1552,1707,1878,2066,2272,2499,2749,3024,3327,3660,4026,4428,4871,5358,5894,6484,7132,7845,8630,9493,10442,11487,12635,13899,15289,16818,18500,20350,22385,24623,27086,29794,32767};
static const int8_t ima_indices[8]={-1,-1,-1,-1,2,4,6,8};
static inline int32_t ima_limit(int32_t x,int32_t lo,int32_t hi) {return x<lo?lo:x>hi?hi:x;}
static inline int16_t ima_nibble(uint8_t code,int32_t* predictor,int32_t* index) {
    int32_t step=ima_steps[*index],delta=step>>3;
    if(code&1)delta+=step>>2;
    if(code&2)delta+=step>>1;
    if(code&4)delta+=step;
    *predictor=ima_limit(*predictor+((code&8)?-delta:delta),-32768,32767);
    *index=ima_limit(*index+ima_indices[code&7],0,88);return (int16_t)*predictor;
}
static inline void ima_encode(const int16_t* pcm,uint8_t* data,int32_t* previous_index) {
    int32_t predictor=pcm[0],index=ima_limit(*previous_index,0,88);
    data[0]=(uint8_t)predictor;data[1]=(uint8_t)(predictor>>8);data[2]=index;data[3]=0;
    for(uint32_t i=1;i<VOICE_SAMPLES;i++) {
        int32_t delta=(int32_t)pcm[i]-predictor,step=ima_steps[index];uint8_t code=0;
        if(delta<0){code=8;delta=-delta;}
        if(delta>=step){code|=4;delta-=step;}step>>=1;
        if(delta>=step){code|=2;delta-=step;}step>>=1;
        if(delta>=step)code|=1;
        ima_nibble(code,&predictor,&index);
        if(i&1)data[4+(i-1)/2]=code;else data[4+(i-1)/2]|=code<<4;
    }
    *previous_index=index;
}
static inline int ima_decode(const uint8_t* data,size_t n,int16_t* pcm) {
    if(n!=VOICE_BYTES || data[2]>88 || data[3])return 0;
    int32_t predictor=(int16_t)((uint16_t)data[0]|((uint16_t)data[1]<<8)),index=data[2];pcm[0]=predictor;
    for(uint32_t i=1;i<VOICE_SAMPLES;i++)pcm[i]=ima_nibble((data[4+(i-1)/2]>>((i&1)?0:4))&15,&predictor,&index);
    return 1;
}
