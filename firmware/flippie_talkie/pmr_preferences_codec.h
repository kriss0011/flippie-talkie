#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef struct {uint8_t channel,volume,record,transport;} PmrPreferences;

static inline bool pmr_preferences_decode(const uint8_t* data,size_t size,PmrPreferences* prefs) {
    if(size!=8 && size!=9)return false;
    uint8_t check=0;for(size_t i=0;i<size-1;i++)check^=data[i];
    bool legacy=size==8 && !memcmp(data,"PMR\1",4);
    bool current=size==9 && !memcmp(data,"PMR\2",4) && data[7]<=1;
    if((!legacy && !current) || data[4]<1 || data[4]>16 || data[5]>8 || data[6]>1 || check!=data[size-1])return false;
    *prefs=(PmrPreferences){data[4],data[5],data[6],legacy?0:data[7]};return true;
}
static inline void pmr_preferences_encode(const PmrPreferences* prefs,uint8_t data[9]) {
    memcpy(data,"PMR\2",4);data[4]=prefs->channel;data[5]=prefs->volume;data[6]=prefs->record;data[7]=prefs->transport;
    data[8]=0;for(unsigned i=0;i<8;i++)data[8]^=data[i];
}
