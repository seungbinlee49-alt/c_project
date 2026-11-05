"""Publish allowlisted numerical outputs; never copy images, templates or local paths.

python tools/export_results.py           # requires the original local experiments
python tools/export_results.py --verify  # works on a fresh clone, without datasets
"""
import argparse
import csv
import hashlib
import io
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
FILES = ('metrics.json', 'timing.json', 'reference-checks.json', 'curve.csv')
DBS = {1: 'FVC2000_DB3_B', 2: 'FVC2002_DB3_B', 3: 'FVC2004_DB1_B'}


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def read(path):
    return json.loads(path.read_text(encoding='utf-8'))


def selected_runs():
    for algorithm in ('baseline', 'aligned', 'minutiae-v2', 'graph-v1'):
        for db in range(1, 4):
            noises = ('clean',) if algorithm == 'baseline' else ('clean', 'gaussian15', 'impulse2')
            for noise in noises:
                prefix = 'graph-v1-paired' if algorithm == 'graph-v1' and noise != 'clean' else algorithm
                yield algorithm, db, noise, f'{prefix}-db{db}-{noise}'


def export():
    output = ROOT / 'results'
    rows, runs = [], []
    for algorithm, db, noise, name in selected_runs():
        source = ROOT / 'experiments' / name
        provenance = read(source / 'provenance.json')
        metrics = read(source / 'metrics.json')
        target = output / 'runs' / name
        target.mkdir(parents=True, exist_ok=True)
        published = {}
        for filename in FILES:
            path = source / filename
            if path.exists():
                content = path.read_bytes()
                if b':\\' in content or b'Users/' in content or b'Users\\' in content:
                    raise ValueError(f'Local path in {name}/{filename}')
                (target / filename).write_bytes(content)
                published[filename] = digest(path)
        # Preserve audit hashes without source-image paths or image/feature content.
        image_hashes = sorted(image['sha256'] for image in provenance['images'])
        runs.append({
            'run': name, 'algorithm': algorithm, 'dataset': DBS[db], 'noise': noise,
            'noise_seed': provenance['noise_seed'], 'data_role': 'DEVELOPMENT_EXPOSED',
            'dll_sha256': provenance['dll_sha256'], 'plan_sha256': provenance['plan_sha256'],
            'original_provenance_sha256': digest(source / 'provenance.json'),
            'extractor_dependency_sha256': provenance.get('extractor_dependency_sha256'),
            'source_image_count': len(image_hashes),
            'sorted_source_sha256_digest': hashlib.sha256('\n'.join(image_hashes).encode()).hexdigest(),
            'files_sha256': published,
        })
        reference = read(source / 'reference-checks.json') if (source / 'reference-checks.json').exists() else {}
        rows.append({
            'run': name, 'algorithm': algorithm, 'dataset': DBS[db], 'noise': noise,
            'noise_seed': provenance['noise_seed'], 'EER': metrics['EER'],
            'EER_star': metrics['EER_star'], 'FTE': metrics['FTE'], 'FTA': reference.get('FTA'),
            'planned_genuine': metrics['planned_genuine'], 'planned_impostor': metrics['planned_impostor'],
            'successful_genuine': metrics['successful_genuine'],
            'successful_impostor': metrics['successful_impostor'],
        })
    csvfile = io.StringIO(newline='')
    writer = csv.DictWriter(csvfile, fieldnames=list(rows[0]))
    writer.writeheader()
    writer.writerows(rows)
    (output / 'summary.csv').write_text(csvfile.getvalue(), encoding='utf-8', newline='')
    manifest = {'schema_version': 1, 'run_count': len(runs), 'data_role': 'DEVELOPMENT_EXPOSED',
                'official_certification': False, 'runs': runs,
                'summary_sha256': digest(output / 'summary.csv')}
    (output / 'manifest.json').write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    verify()


def verify():
    output = ROOT / 'results'
    manifest = read(output / 'manifest.json')
    if digest(output / 'summary.csv') != manifest['summary_sha256']:
        raise ValueError('Summary hash mismatch')
    rows = list(csv.DictReader((output / 'summary.csv').read_text(encoding='utf-8').splitlines()))
    if len(rows) != manifest['run_count'] or len(rows) != len(manifest['runs']):
        raise ValueError('Run count mismatch')
    by_name = {row['run']: row for row in rows}
    for run in manifest['runs']:
        target = output / 'runs' / run['run']
        for name, expected in run['files_sha256'].items():
            if name not in FILES or digest(target / name) != expected:
                raise ValueError(f'Unexpected or changed output: {run["run"]}/{name}')
        metrics = read(target / 'metrics.json')
        for key in ('EER', 'EER_star', 'FTE', 'planned_genuine', 'planned_impostor',
                    'successful_genuine', 'successful_impostor'):
            raw = by_name[run['run']][key]
            actual = None if raw == '' else float(raw)
            if actual != metrics[key]:
                raise ValueError(f'Summary differs from raw metrics: {run["run"]}/{key}')
    print(f'PASS: {len(rows)} published runs, hashes and summary metrics verified')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--verify', action='store_true')
    args = parser.parse_args()
    verify() if args.verify else export()
