"""Verify that the published core and DLLs match the selected v1 snapshot."""
import hashlib
import json
from pathlib import Path

root = Path(__file__).resolve().parent
manifest = json.loads((root / 'manifest.json').read_text(encoding='utf-8'))
for name, expected in manifest['files'].items():
    path = (root / name).resolve()
    if not path.is_relative_to(root) or not path.is_file():
        raise SystemExit(f'Missing or invalid path: {name}')
    if hashlib.sha256(path.read_bytes()).hexdigest() != expected:
        raise SystemExit(f'Changed selected file: {name}')
print(f"PASS: selected v1 core and DLLs ({len(manifest['files'])} files)")
