import test from 'node:test';
import {execFileSync} from 'node:child_process';
import {mkdtempSync, rmSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {join, resolve} from 'node:path';

for (const [source, voiceAssets] of [['test_pmr_radio_core.c', true], ['test_pmr_tx_tone.c', false]]) {
  test(voiceAssets ? 'All 16 voice assets preserve checksum, bits and finite timing' : 'Tone test terminates after exactly 5.4 seconds', () => {
    const temp = mkdtempSync(join(tmpdir(), 'pmr-radio-'));
    try {
      const binary = join(temp, 'check');
      execFileSync(process.env.CC || 'cc', ['-std=c11', '-Wall', '-Wextra', '-Werror', '-I', resolve('firmware/pmr_capture'), `tests/${source}`, '-o', binary]);
      const assets = voiceAssets ? Array.from({length:16}, (_, i) => `sd/apps_data/pmr_radio/voice/ch${String(i + 1).padStart(2,'0')}.pdm`) : [];
      execFileSync(binary, assets);
    } finally {
      rmSync(temp, {recursive:true, force:true});
    }
  });
}
