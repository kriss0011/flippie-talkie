#include <assert.h>
#include "pmr_preferences_codec.h"
int main(void) {
    PmrPreferences original={16,0,1,1},result={0};uint8_t data[9];
    pmr_preferences_encode(&original,data);
    assert(pmr_preferences_decode(data,9,&result));assert(!memcmp(&original,&result,sizeof(result)));
    for(unsigned i=0;i<9;i++){data[i]^=1;assert(!pmr_preferences_decode(data,9,&result));data[i]^=1;}
    for(size_t n=0;n<8;n++)assert(!pmr_preferences_decode(data,n,&result));
    assert(!pmr_preferences_decode(data,10,&result));
    const uint8_t legacy[8]={'P','M','R',1,9,8,1,'P'^'M'^'R'^1^9^8^1};
    assert(pmr_preferences_decode(legacy,8,&result));assert(result.channel==9 && result.volume==8 && result.record==1 && result.transport==0);
    for(unsigned i=0;i<4;i++) {
        original=(PmrPreferences){9,8,1,0};
        if(i==0)original.channel=0;
        if(i==1)original.volume=9;
        if(i==2)original.record=2;
        if(i==3)original.transport=2;
        pmr_preferences_encode(&original,data);assert(!pmr_preferences_decode(data,9,&result));
    }
    original=(PmrPreferences){17,8,1,0};pmr_preferences_encode(&original,data);assert(!pmr_preferences_decode(data,9,&result));
    return 0;
}
