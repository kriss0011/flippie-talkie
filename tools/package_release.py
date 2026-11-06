"""Package the single maintained app after building and copying its FAP to sd/."""
import hashlib
import json
from pathlib import Path
from zipfile import ZipFile, ZIP_DEFLATED

ROOT = Path(__file__).resolve().parents[1]
VERSION = '0.10'

def package():
    files = [ROOT / name for name in ('LICENSE', 'README.md', 'CHANGELOG.md', 'THIRD_PARTY_NOTICES.md', 'package.json', 'requirements.txt')]
    for folder in ('firmware', 'sd', 'web', 'docs', 'tools', 'tests', 'assets', 'validation/redesign-0.5', 'validation/roguemaster-0.6', 'validation/tx-timing-0.7', 'validation/tx-lead-in-0.8', 'validation/ui-0.9', 'validation/speak-text'):
        files.extend(p for p in (ROOT / folder).rglob('*') if p.is_file() and '__pycache__' not in p.parts and p.suffix != '.pyc' and not p.is_relative_to(ROOT / 'docs/media'))
    files = sorted(set(files))
    apps = list((ROOT / 'sd/apps').rglob('*.fap'))
    assert len(apps) == 1 and apps[0].name == 'Flippie_Talkie.fap'
    assert len(list((ROOT / 'firmware').rglob('application.fam'))) == 1
    release = ROOT / f'releases/Flippie-Talkie-v{VERSION}.zip'
    with ZipFile(release, 'w', ZIP_DEFLATED) as z:
        for p in files:
            z.write(p, f'Flippie-Talkie-v{VERSION}/{p.relative_to(ROOT)}')
    checksums = {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest() for p in files + [release]}
    metadata = {
        'project': 'Flippie Talkie', 'date': '2026-11-06',
        'firmware_repository': 'https://github.com/RogueMaster/flipperzero-firmware-wPlugins.git',
        'firmware_commit': 'e1837ff6a1a7d5c91a0782343a98e2e600d62da7', 'firmware_api': '88.23',
        'apps': {'Flippie Talkie': VERSION}, 'sha256': checksums,
    }
    (ROOT / 'releases/manifest.json').write_text(json.dumps(metadata, indent=2) + '\n')
    (ROOT / 'releases/SHA256SUMS').write_text(''.join(f'{digest}  {path}\n' for path, digest in checksums.items()))
    print(release)

if __name__ == '__main__':
    package()
