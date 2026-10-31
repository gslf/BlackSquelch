#!/usr/bin/env python3
"""Package a complete, offline-ready PWA using only Python's standard library."""
import argparse
import hashlib
from pathlib import Path
import sys
import zipfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('directory', type=Path)
parser.add_argument('--output-dir', type=Path, default=Path('build/dist'))
args = parser.parse_args()
root = args.directory.resolve()
required = ('index.html', 'app.js', 'worker.js', 'audio.js', 'audio-worklet.js',
            'modem.mjs', 'modem.wasm', 'sw.js', 'manifest.webmanifest', 'style.css',
            'icons/icon.svg', 'icons/icon-32.png', 'icons/icon-192.png', 'icons/icon-512.png',
            'icons/maskable-512.png', 'icons/apple-touch-icon.png',
            'screenshots/wide.png', 'screenshots/narrow.png')
for name in required:
    if not (root / name).is_file():
        parser.exit(1, f'Missing web asset: {name}\n')
if '__BUILD__' in (root / 'sw.js').read_text():
    parser.exit(1, 'Offline cache version has not been finalized. Rebuild the web target.\n')
args.output_dir.mkdir(parents=True, exist_ok=True)
archive = args.output_dir / 'blacksquelch-web.zip'
with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED) as output:
    for name in required:
        output.write(root / name, f'blacksquelch-web/{name}')
    output.write(Path(__file__).resolve().parents[1] / 'docs/web.md', 'blacksquelch-web/README.md')
digest = hashlib.sha256(archive.read_bytes()).hexdigest()
(archive.parent / (archive.name + '.sha256')).write_text(f'{digest}  {archive.name}\n', encoding='ascii')
print(archive)
