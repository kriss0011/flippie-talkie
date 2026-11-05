#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pmr_radio_core.h"
int main(int argc,char** argv) {
    assert(pmr_channel_hz(0)==0 && pmr_channel_hz(17)==0);
    assert(pmr_channel_hz(1)==446006250 && pmr_channel_hz(9)==446106250 && pmr_channel_hz(16)==446193750);
    for(unsigned ch=2;ch<=16;ch++) assert(pmr_channel_hz(ch)-pmr_channel_hz(ch-1)==12500);
    for(int arg=1;arg<argc;arg++) {
        FILE* file=fopen(argv[arg],"rb");assert(file);
        char magic[8];uint32_t header[4];
        assert(fread(magic,1,8,file)==8 && !memcmp(magic,"PMRPDM1",8));
        assert(fread(header,4,4,file)==4 && header[1]==48000 && header[2]>=1 && header[2]<=16);
        uint32_t bytes=(header[0]+7)/8;uint8_t* bits=malloc(bytes);assert(bits);
        assert(fread(bits,1,bytes,file)==bytes && fgetc(file)==EOF);fclose(file);
        assert(pmr_checksum(bits,bytes)==header[3]);
        PmrPdm pdm={.bits=bits,.count=header[0]};uint64_t total=0;
        uint32_t cursor=0,duration,runs=0;bool level,previous=false;
        while(pmr_pdm_next(&pdm,&level,&duration)) {
            assert(duration>0 && duration<100000);
            if(runs) assert(previous!=level);
            while(cursor<pdm.position) {assert(pmr_pdm_bit(&pdm,cursor)==level);cursor++;}
            total+=duration;previous=level;runs++;
        }
        assert(cursor==header[0]);assert(total*48000ULL==header[0]*1000000ULL);
        assert(!pmr_pdm_next(&pdm,&level,&duration));
        printf("CH %u: checksum, finite timing, bit-exact run reconstruction PASS (%u runs, %llu us)\n",header[2],runs,(unsigned long long)total);
        free(bits);
    }
}
