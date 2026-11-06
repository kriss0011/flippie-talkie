import test from 'node:test';
import assert from 'node:assert/strict';
import {waitForTxIdle,TX_LEAD_IN_MS} from '../web/tx-timing.js';
function scenario(overrides={}){
  let time=0;const controller=new AbortController();
  return {controller, options:{epoch:8,getStatus:()=>({epoch:8,state:time<700?4:1,error:0}),isReady:()=>true,signal:controller.signal,now:()=>time,delay:async ms=>{time+=ms;},...overrides},time:()=>time};
}
test('Recorded sends wait for the complete radio tail, beyond the previous 220 ms',async()=>{
  const f=scenario();await waitForTxIdle(f.options);assert.equal(f.time(),700);assert.equal(TX_LEAD_IN_MS,500);
});
test('Stop during drain cancels before another message can be scheduled',async()=>{
  const f=scenario();f.options.delay=async()=>f.controller.abort();await assert.rejects(waitForTxIdle(f.options),/stopped/);
});
test('A stale epoch cannot confirm completion; missing status times out',async()=>{
  const f=scenario({getStatus:()=>({epoch:7,state:0,error:0}),timeoutMs:50});await assert.rejects(waitForTxIdle(f.options),/did not confirm/);assert.equal(f.time(),50);
});
test('Disconnects and radio errors are not counted as successful sends',async()=>{
  const disconnected=scenario({isReady:()=>false});await assert.rejects(waitForTxIdle(disconnected.options),/disconnected/);
  const failed=scenario({getStatus:()=>({epoch:8,state:0,error:5})});await assert.rejects(waitForTxIdle(failed.options),/stopped before/);
});
