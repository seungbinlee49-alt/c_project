"""Development-only source/evidence integrity check; no model download required."""
from pathlib import Path
import hashlib,json,csv
def digest(path):
 data=path.read_bytes()
 # Match Git text normalization while preserving binary test vectors exactly.
 if b'\0' not in data[:8000]:data=data.replace(b'\r\n',b'\n')
 return hashlib.sha256(data).hexdigest()
root=Path(__file__).resolve().parent
manifest=json.loads((root/'manifest.json').read_text(encoding='utf-8'))
for name,expected in manifest['files'].items():
 p=(root/name).resolve()
 if not p.is_relative_to(root) or not p.is_file() or digest(p)!=expected:raise SystemExit('Changed or missing DMD file: '+name)
repo=root.parent
for name,expected in manifest['dependencies'].items():
 p=(repo/name).resolve()
 if not p.is_relative_to(repo/'third_party') or not p.is_file() or digest(p)!=expected:raise SystemExit('Changed or missing dependency: '+name)
rows=list(csv.DictReader((root/'results/accuracy.csv').open(encoding='utf-8')))
assert len(rows)==18 and all(int(r['genuine'])==70 and int(r['impostor'])==720 for r in rows)
lat=list(csv.DictReader((root/'results/latency.csv').open(encoding='utf-8')))
assert len(lat)==27 and all(r['under_5s']=='True' and r['cuda_provider_loaded']=='False' and float(r['total_ms'])<5000 for r in lat)
print(f"PASS: {len(manifest['files'])} DMD files, {len(manifest['dependencies'])} dependency files; evidence schema and CPU latency records")
