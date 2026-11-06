"""Capture the Flippie Talkie screens from a connected Flipper over USB.

Uploads the built app next to the settings file, opens that copy, walks the
screens and removes it again. The installed app and settings are left alone.
Only short presses are sent and no test is started, so nothing is transmitted.

    python tools/flipper_screenshots.py docs/images
"""
import hashlib
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT))
from flipper_cli import CLI

FAP = ROOT.parent / 'build/sd/apps/Sub-GHz/Flippie_Talkie.fap'
PREVIEW = '/ext/apps_data/pmr_radio/Flippie_Talkie_preview.fap'
SETTINGS = '/ext/apps_data/pmr_radio/settings.bin'
out = Path(sys.argv[1] if len(sys.argv) > 1 else 'docs/images')


def run(*commands, pause=0.25):
    cli = CLI()
    try:
        result = ''
        for command in commands:
            result = cli.command(command)
            time.sleep(pause)
        return result
    finally:
        cli.close()


def keys(*names, pause=0.25):
    run(*[f'input send {name} short' for name in names], pause=pause)


def shot(name):
    time.sleep(0.4)
    subprocess.run([sys.executable, str(ROOT / 'device_screen.py'), str(out / f'flipper-{name}.png')],
                   check=True, capture_output=True)


def main():
    out.mkdir(parents=True, exist_ok=True)
    data = FAP.read_bytes()
    assert 'No application' in run('loader info'), 'Close the running Flipper app first'
    settings = run(f'storage md5 {SETTINGS}')
    cli = CLI()
    try:
        cli.write_new_file(PREVIEW, data)
        assert hashlib.md5(data).hexdigest() in cli.command(f'storage md5 {PREVIEW}')
    finally:
        cli.close()
    try:
        run(f'loader open {PREVIEW}', pause=1)
        shot('home')
        keys('down', 'down', 'ok')
        shot('settings')
        keys('back', 'down', 'down', 'down', 'ok')
        shot('quick-start')
        keys('back', 'ok')
        time.sleep(1.2)
        shot('listen')
        keys('ok')
        shot('paused')
        keys('ok', 'left')
        shot('listen-config')
        keys('down', 'down', 'down', 'down', 'ok')
        shot('controls')
        keys('back', 'back', 'right')
        shot('tests')
        keys('back', 'back')
        time.sleep(1)
        keys('down', 'ok')
        time.sleep(1.2)
        shot('browser-link')
        keys('right')
        shot('how-to-connect')
        keys('back', 'left')
        shot('link-config')
        keys('back', 'back')
        time.sleep(1)
        keys('back')
        time.sleep(0.6)
    finally:
        for _ in range(5):
            if 'No application' in run('loader info'):
                break
            keys('back', pause=0.65)
        run(f'storage remove {PREVIEW}')
    assert run(f'storage md5 {SETTINGS}') == settings, 'Settings changed during capture'
    print('Screens saved to', out)


if __name__ == '__main__':
    main()
