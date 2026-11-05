#include <stdio.h>
#include <string.h>
#include "pmr_codec.h"
int main(int argc,char**argv){
 if(argc>1 && !strcmp(argv[1],"decode")){uint8_t data[VOICE_BYTES];int16_t pcm[VOICE_SAMPLES];while(fread(data,1,sizeof data,stdin)==sizeof data){if(!ima_decode(data,sizeof data,pcm))return 2;fwrite(pcm,sizeof *pcm,VOICE_SAMPLES,stdout);}}
 else{int16_t pcm[VOICE_SAMPLES];uint8_t data[VOICE_BYTES];int32_t index=0;while(fread(pcm,sizeof *pcm,VOICE_SAMPLES,stdin)==VOICE_SAMPLES){ima_encode(pcm,data,&index);fwrite(data,1,sizeof data,stdout);}}
 return 0;
}
