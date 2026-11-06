import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';

const read = path => readFileSync(path, 'utf8');
const match = (path, pattern) => {
  const found = read(path).match(pattern);
  assert.ok(found, `No version found in ${path}`);
  return found[1];
};

test('Every published surface reports the same version', () => {
  const version = JSON.parse(read('package.json')).version.replace(/\.0$/, '');
  assert.equal(match('firmware/flippie_talkie/pmr_ui.h', /FLIPPIE_TALKIE_VERSION "([\d.]+)"/), version);
  assert.equal(
    match('firmware/flippie_talkie/application.fam', /fap_version=\((\d+, \d+)\)/).replace(', ', '.'),
    version,
  );
  assert.equal(match('web/index.html', /Flippie Talkie Web ([\d.]+)/), version);
  assert.equal(match('tools/package_release.py', /VERSION = '([\d.]+)'/), version);
});
