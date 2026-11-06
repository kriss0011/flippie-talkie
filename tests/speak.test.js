import test from 'node:test';
import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import {mkdtempSync, readFileSync, rmSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {join, resolve} from 'node:path';

const firmware = resolve('firmware/flippie_talkie');
function run(sources, expected) {
  const dir = mkdtempSync(join(tmpdir(), 'pmr-speak-'));
  try {
    const binary = join(dir, 'test');
    execFileSync(process.env.CC || 'cc', ['-std=c11', '-Wall', '-Wextra', '-Werror', '-I', firmware, ...sources, '-lm', '-o', binary]);
    assert.match(execFileSync(binary, {encoding: 'utf8'}), expected);
  } finally {
    rmSync(dir, {recursive: true, force: true});
  }
}
test('Speech stays out of the main app, whose RAM Listen needs to start recording', () => {
  const manifest = readFileSync('firmware/flippie_talkie/application.fam', 'utf8');
  const [main, plugin] = manifest.split(/^App\(/m).slice(1);
  assert.match(main, /sources=\[[^\]]*pmr_capture\.c/, 'main app must list its sources explicitly');
  assert.doesNotMatch(main, /pmr_tts|pmr_speak/);
  assert.match(plugin, /apptype=FlipperAppType\.PLUGIN/);
  assert.match(plugin, /sources=\[[^\]]*pmr_tts\.c/);
  assert.match(plugin, /fal_embedded=True/, 'installing must stay a single file');
});
test('Text becomes bounded, repeatable speech and silent text is refused', () => {
  run(['tests/test_pmr_tts.c', 'firmware/flippie_talkie/pmr_tts.c'], /tts ok/);
});
test('Spoken messages keep the lead-in, end by themselves and store their settings', () => {
  run(['tests/test_pmr_speak.c'], /speak ok/);
});
