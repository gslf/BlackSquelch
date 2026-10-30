#!/usr/bin/env python3
"""Give each complete web build a distinct offline cache version."""
import hashlib
import re
from pathlib import Path
import sys
root = Path(sys.argv[1])
digest = hashlib.sha256()
worker = root / 'sw.js'
template = re.sub(r"const VERSION = '[^']+';", "const VERSION = '__BUILD__';", worker.read_text())
for path in sorted(root.rglob('*')):
    if path.is_file():
        digest.update(path.relative_to(root).as_posix().encode())
        digest.update(template.encode() if path == worker else path.read_bytes())
worker.write_text(template.replace('__BUILD__', digest.hexdigest()[:20]), encoding='utf-8')
print(f'Offline PWA ready: {root}')
