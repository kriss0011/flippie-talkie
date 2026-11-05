#include <assert.h>
#include <stdio.h>
#include "pmr_rx_gate.h"
#include "pmr_link_dsp.h"
int main(void) {
    const int levels[]={-80,-98,-110};
    for(unsigned j=0;j<sizeof(levels)/sizeof(levels[0]);j++) {
        PmrRxGate gate;pmr_rx_gate_start(&gate,-104,0);
        for(unsigned t=20;t<=30000;t+=20) {
            pmr_rx_gate_update(&gate,levels[j]+(int)(t/20%3)-1,t);
            assert(!pmr_rx_gate_needs_check(&gate,t));
        }
        for(unsigned t=30020;t<=30700;t+=20)pmr_rx_gate_update(&gate,-104,t);
        assert(pmr_rx_gate_needs_check(&gate,30700));
    }
    PmrRxGate ambiguous;pmr_rx_gate_start(&ambiguous,-104,0);
    for(unsigned t=20;t<=8000;t+=20){pmr_rx_gate_update(&ambiguous,-103,t);assert(!pmr_rx_gate_needs_check(&ambiguous,t));}
    assert(pmr_rx_gate_needs_check(&ambiguous,8020));
    PmrRxGate wrap;pmr_rx_gate_start(&wrap,-104,UINT32_MAX-100);
    for(uint32_t elapsed=20;elapsed<10000;elapsed+=20){uint32_t t=UINT32_MAX-100+elapsed;pmr_rx_gate_update(&wrap,-80,t);assert(!pmr_rx_gate_needs_check(&wrap,t));}
    for(unsigned bt=0;bt<2;bt++) {
        PmrDsp dsp={0};
        for(unsigned i=0;i<80000;i++)assert(pmr_dsp_sample_rate(&dsp,-60,bt)==0);
    }
    puts("PASS: stable positive/negative RSSI contrast, carrier release, ambiguous fallback, clock wrap and DSP silence");
}
