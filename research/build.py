#!/usr/bin/env python3
"""Draw the paper's figures from data/ and print the Markdown paper to PDF.

Needs the `markdown` Python package and Google Chrome. Set CHROME to use another path.
"""
import csv
import html
import os
import re
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent
PAPER = ROOT / 'rssi-slope-fm-on-cc1101.md'
CHROME = os.environ.get('CHROME', '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome')

INK, MUTED, RULE, LINE, PICK = '#1a1a1a', '#5f5f5f', '#d9d9d9', '#2a6f97', '#b3361f'
FONT = 'font-family="Helvetica, Arial, sans-serif"'


def text(x, y, value, size=13, fill=INK, anchor='middle', weight='normal'):
    return (f'<text x="{x}" y="{y}" {FONT} font-size="{size}" fill="{fill}" '
            f'text-anchor="{anchor}" font-weight="{weight}">{html.escape(value)}</text>')


def svg(width, height, body):
    return (f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {width} {height}" '
            f'width="{width}" height="{height}" role="img">'
            f'<rect width="{width}" height="{height}" fill="#ffffff"/>{body}</svg>\n')


def signal_chain():
    stages = [
        ('Carrier on', 'filter skirt', '35 kHz off channel'),
        ('RSSI', 'register', '½ dB steps'),
        ('SPI read', '8 kHz', 'timer interrupt'),
        ('Band-pass', '4th order', 'from 180 Hz'),
        ('De-emphasis', '450 Hz', 'one pole'),
        ('Audio out', 'speaker, WAV', 'USB, Bluetooth'),
    ]
    box, gap, top = 128, 26, 26
    body = ['<defs><marker id="a" viewBox="0 0 8 8" refX="7" refY="4" markerWidth="7" '
            f'markerHeight="7" orient="auto"><path d="M0 0L8 4L0 8z" fill="{MUTED}"/></marker></defs>']
    for i, (title, line1, line2) in enumerate(stages):
        x = 16 + i * (box + gap)
        body.append(f'<rect x="{x}" y="{top}" width="{box}" height="88" rx="4" fill="#f6f6f6" stroke="{RULE}"/>')
        centre = x + box / 2
        body.append(text(centre, top + 30, title, 14, INK, weight='bold'))
        body.append(text(centre, top + 52, line1, 12.5, MUTED))
        body.append(text(centre, top + 70, line2, 12.5, MUTED))
        if i < len(stages) - 1:
            y = top + 44
            body.append(f'<line x1="{x + box + 3}" y1="{y}" x2="{x + box + gap - 3}" y2="{y}" '
                        f'stroke="{MUTED}" stroke-width="1.5" marker-end="url(#a)"/>')
    width = 32 + len(stages) * box + (len(stages) - 1) * gap
    (ROOT / 'figures' / 'signal-chain.svg').write_text(svg(width, 140, ''.join(body)))


def panel(x0, title, unit, points, y_min, y_max, y_step, label_for_pick):
    width, top, height = 372, 46, 270
    x_lo, x_hi = -60, 60
    sx = lambda v: x0 + 46 + (v - x_lo) / (x_hi - x_lo) * (width - 58)
    sy = lambda v: top + (y_max - v) / (y_max - y_min) * height
    out = [text(x0 + 46, 22, title, 15, INK, 'start', 'bold'), text(x0 + 46, 39, unit, 12.5, MUTED, 'start')]
    tick = y_min
    while tick <= y_max:
        out.append(f'<line x1="{sx(x_lo)}" y1="{sy(tick)}" x2="{sx(x_hi)}" y2="{sy(tick)}" stroke="{RULE}"/>')
        out.append(text(sx(x_lo) - 7, sy(tick) + 4, f'{tick:g}'.replace('-', '−'), 12, MUTED, 'end'))
        tick += y_step
    for tick in (-55, -35, 0, 35, 55):
        label = f'{tick:+d}'.replace('-', '−') if tick else '0'
        out.append(text(sx(tick), top + height + 20, label, 12, MUTED))
    out.append(text(sx(0), top + height + 40, 'Tuning offset from channel (kHz)', 12.5, MUTED))
    for side in (points[:4], points[4:]):
        path = ' '.join(f'{"M" if i == 0 else "L"}{sx(p[0]):.1f} {sy(p[1]):.1f}' for i, p in enumerate(side))
        out.append(f'<path d="{path}" fill="none" stroke="{LINE}" stroke-width="2"/>')
    for x, y in points:
        chosen = x == -35
        out.append(f'<circle cx="{sx(x):.1f}" cy="{sy(y):.1f}" r="{6 if chosen else 4.5}" '
                   f'fill="{PICK if chosen else LINE}" stroke="#ffffff" stroke-width="2"/>')
    x, y = next(p for p in points if p[0] == -35)
    out.append(text(sx(x) + 12, sy(y) - 10, label_for_pick, 12.5, INK, 'start', 'bold'))
    return ''.join(out)


def offset_sweep():
    with open(ROOT / 'data' / 'offset-sweep.csv', newline='') as source:
        rows = [r for r in csv.DictReader(source) if 1 <= int(r['step']) <= 8]
    rows.sort(key=lambda r: int(r['offset_hz']))
    level = [(int(r['offset_hz']) / 1000, float(r['mean_rssi_dbm'])) for r in rows]
    score = [(int(r['offset_hz']) / 1000, float(r['tone_score_db'])) for r in rows]
    body = panel(0, 'Mean level', 'dBm', level, -70, -10, 10, '−35 kHz chosen')
    body += panel(392, 'Tone score', 'dB, higher is better', score, -30, 10, 10, '+3.5 dB')
    (ROOT / 'figures' / 'offset-sweep.svg').write_text(svg(776, 362, body))


def pdf():
    import markdown

    source = PAPER.read_text()
    title = re.match(r'# (.+)', source).group(1)
    source = re.sub(r'\[`data/`\]\(data/\)', '`data/`', source)
    body = markdown.markdown(source, extensions=['tables', 'sane_lists'])
    body = re.sub(r'<p><em>(Figure|Table)', r'<p class="caption"><em>\1', body)
    style = """
      @page { size: A4; margin: 22mm 22mm 20mm; }
      body { font: 10.5pt/1.42 Georgia, 'Times New Roman', serif; color: #111; }
      h1 { font-size: 19pt; line-height: 1.2; margin: 0 0 10pt; }
      h2 { font-size: 12.5pt; margin: 16pt 0 5pt; break-after: avoid; }
      h3 { font-size: 10.5pt; margin: 11pt 0 3pt; break-after: avoid; }
      p, li { margin: 0 0 6pt; orphans: 3; widows: 3; }
      ul, ol { margin: 0 0 6pt; padding-left: 16pt; }
      img { display: block; max-width: 100%; height: auto; margin: 8pt auto 3pt; }
      .caption { font-size: 9pt; color: #333; break-before: avoid; }
      table { width: 100%; border-collapse: collapse; margin: 6pt 0; font-size: 9pt; line-height: 1.3; break-inside: avoid; }
      th { text-align: left; border-bottom: 1px solid #111; border-top: 1px solid #111; padding: 3pt 5pt 3pt 0; }
      td { border-bottom: 1px solid #ddd; padding: 3pt 5pt 3pt 0; vertical-align: top; }
      code { font: 9pt Menlo, monospace; }
      a { color: inherit; word-break: break-all; }
    """
    page = (f'<!doctype html><html lang="en"><head><meta charset="utf-8"><title>{html.escape(title)}</title>'
            f'<base href="{ROOT.as_uri()}/"><style>{style}</style></head><body>{body}</body></html>')
    with tempfile.TemporaryDirectory() as folder:
        document = Path(folder) / 'paper.html'
        document.write_text(page)
        subprocess.run([
            CHROME, '--headless=new', '--disable-gpu', '--no-pdf-header-footer',
            '--allow-file-access-from-files', f'--user-data-dir={folder}/chrome',
            f'--print-to-pdf={PAPER.with_suffix(".pdf")}', document.as_uri(),
        ], check=True, stderr=subprocess.DEVNULL)


if __name__ == '__main__':
    signal_chain()
    offset_sweep()
    pdf()
