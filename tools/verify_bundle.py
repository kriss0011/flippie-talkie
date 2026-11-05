"""Verify released files and historical archive without accessing a device."""
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def verify_manifest(base, entries):
    for relative, expected in entries.items():
        target = (base / relative).resolve()
        if not target.is_relative_to(base.resolve()):
            raise ValueError(f"Manifest path escapes its directory: {relative}")
        actual = hashlib.sha256(target.read_bytes()).hexdigest()
        if actual != expected:
            raise ValueError(f"Checksum mismatch: {relative}")
    return len(entries)


if __name__ == '__main__':
    apps = list((ROOT / 'sd/apps').rglob('*.fap'))
    manifests = list((ROOT / 'firmware').rglob('application.fam'))
    if [p.name for p in apps] != ['Flippie_Talkie.fap'] or len(manifests) != 1:
        raise ValueError('The maintained firmware and SD package must contain exactly one unified app')
    release = json.loads((ROOT / 'releases/manifest.json').read_text())
    count = verify_manifest(ROOT, release['sha256'])
    archive = json.loads((ROOT / 'archive/manifest.json').read_text())
    historical = verify_manifest(ROOT / 'archive', archive)
    print(f'PASS: {count} released assets and {historical} historical files verified.')
