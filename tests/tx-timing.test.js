import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync,writeFileSync,mkdtempSync,rmSync} from 'node:fs';
import {execFileSync} from 'node:child_process';
import {tmpdir} from 'node:os';
import {join,resolve} from 'node:path';

function extractFunction(text, name) {
  const start = text.indexOf(`static ${name === 'tx_bit' ? 'bool' : 'LevelDuration'} ${name}(`);
  assert.ok(start >= 0, `missing actual callback ${name}`);
  const brace = text.indexOf('{', start); let depth = 1, end = brace + 1;
  while (depth && end < text.length) { if (text[end] === '{') depth++; else if (text[end] === '}') depth--; end++; }
  assert.equal(depth, 0);
  return text.slice(start, end);
}

test('Actual API 88 callbacks preserve voice timing, add exactly 500 ms of silence and retain queued PCM', () => {
  const temp = mkdtempSync(join(tmpdir(), 'flippie-tx-timing-'));
  try {
    const standalone = readFileSync('firmware/flippie_talkie/pmr_capture.c', 'utf8');
    const remote = readFileSync('firmware/flippie_talkie/pmr_connect.c', 'utf8');
    const source = `
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pmr_radio_core.h"
typedef struct {bool level,reset;uint32_t duration;} LevelDuration;
static LevelDuration level_duration_make(bool level,uint32_t duration){return (LevelDuration){level,false,duration};}
static LevelDuration level_duration_reset(void){return (LevelDuration){false,true,0};}
#define TX_RING 8192
#define __DMB() ((void)0)
typedef struct {int16_t tx[TX_RING];uint32_t tx_head,tx_tail,tx_lead_bits,pdm_phase,pdm_slot,underruns;int32_t pdm_error,pdm_sample;bool carry_valid,pdm_carry,ptt_draining;} App;
${extractFunction(standalone, 'tx_voice_yield')}
${extractFunction(remote, 'tx_bit')}
${extractFunction(remote, 'tx_yield')}
int main(int argc,char** argv){
  for(int n=1;n<argc;n++){
    FILE* f=fopen(argv[n],"rb");assert(f);uint32_t header[6];assert(fread(header,4,6,f)==6);
    uint32_t size=(header[2]+7)/8;uint8_t* bytes=malloc(size);assert(bytes);assert(fread(bytes,1,size,f)==size);fclose(f);
    PmrPdm pdm={.bits=bytes,.count=header[2],.lead_remaining=PMR_TX_LEAD_BITS};uint64_t elapsed=0;LevelDuration pulse;
    while(!(pulse=tx_voice_yield(&pdm)).reset){
      assert(pulse.duration>2);uint32_t arr=pulse.duration-1;elapsed+=arr+1;
    }
    assert((elapsed-500000ULL)*48000ULL==header[2]*1000000ULL);free(bytes);
  }
  App app={.tx_head=128};uint64_t elapsed=0;
  // Zero PCM must give alternating balanced bits, six pulses per 125 us.
  for(unsigned n=0;n<768;n++){
    LevelDuration p=tx_yield(&app);assert(!p.reset);assert(p.level==(n%2==0));
    assert(p.duration==(n%6==5?20U:21U));uint32_t arr=p.duration-1;elapsed+=arr+1;
  }
  assert(elapsed==16000 && app.underruns==0);
  App prefixed={.tx_head=4000,.tx_lead_bits=PMR_TX_LEAD_BITS};elapsed=0;
  for(unsigned n=0;n<PMR_TX_LEAD_BITS;n++){
    LevelDuration p=tx_yield(&prefixed);assert(!p.reset);assert(p.level==(n%2==0));
    assert(p.duration==(n%6==5?20U:21U));elapsed+=p.duration;
    if(n+1<PMR_TX_LEAD_BITS)assert(prefixed.tx_tail==0);
  }
  assert(elapsed==500000 && prefixed.tx_tail<=1 && prefixed.underruns==0);
  assert(pmr_tx_drain_ms(PMR_TX_LEAD_BITS,321)==604);
  assert(pmr_tx_drain_ms(0,3000)==550);
  assert(pmr_tx_drain_ms(PMR_TX_LEAD_BITS,TX_RING)<2000);

  puts("PASS: all 16 actual voice callbacks and remote 48 kHz pulses retain exact timing");
}
`;
    const file = join(temp, 'check.c'), binary = join(temp, 'check');
    writeFileSync(file, source);
    execFileSync(process.env.CC || 'cc', ['-std=c11','-Wall','-Wextra','-Werror','-I',resolve('firmware/flippie_talkie'),file,'-o',binary]);
    execFileSync(binary, Array.from({length:16},(_,i)=>`sd/apps_data/pmr_radio/voice/ch${String(i+1).padStart(2,'0')}.pdm`));
  } finally { rmSync(temp,{recursive:true,force:true}); }
});
