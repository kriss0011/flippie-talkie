#include <assert.h>
#include <stdio.h>
#include "pmr_tx_tone.h"
int main(void) {
    PmrTxTone tone={0};
    uint64_t total=0,segments[12]={0};
    uint32_t duration,count=0;
    bool level;
    while(tone.segment<12) {
        uint32_t segment=tone.segment;
        assert(pmr_tx_tone_next(&tone,&level,&duration));
        assert(duration>0 && duration<=200000);
        segments[segment]+=duration;total+=duration;
        assert(++count<20000);
    }
    for(unsigned i=0;i<12;i++) assert(segments[i]==(i%2?200000:700000));
    assert(total==5400000);
    assert(!pmr_tx_tone_next(&tone,&level,&duration));
    assert(!pmr_tx_tone_next(&tone,&level,&duration));
    printf("PASS: finite 5400000 us tone sequence, %u intervals, all durations valid\n",count);
}
