"""Check sampling semantics, reproducibility and invalid-plan rejection."""
from pathlib import Path
import subprocess
import tempfile
import sys

root = Path(__file__).resolve().parents[1]
(root / 'test-output').mkdir(exist_ok=True)
exe = Path(sys.argv[1]) if len(sys.argv) > 1 else root / 'build/Release/fp_plan.exe'
work = Path(tempfile.mkdtemp(prefix='planner-', dir=root / 'test-output'))
db = work / 'database'
db.mkdir()
for finger in range(101, 111):
    for capture in range(1, 9):
        (db / f'{finger}_{capture}.tif').touch()

def run(name, rules, counts=None, seed=42):
    path = work / (name + '.tsv')
    result = subprocess.run([str(exe), str(db), '1', str(path), str(seed), *rules], capture_output=True)
    if counts is None:
        assert result.returncode != 0 and not path.exists(), result.stdout
        return
    assert result.returncode == 0, result.stderr
    rows = [line.split('\t') for line in path.read_text(encoding='utf-8').splitlines()[1:]]
    assert (sum(r[3] == 'G' for r in rows), sum(r[3] == 'I' for r in rows)) == counts
    assert len({(r[4], r[5]) for r in rows}) == len(rows)
    assert all(r[4] != r[5] for r in rows)
    before = path.read_bytes()
    again = subprocess.run([str(exe), str(db), '1', str(path), str(seed), *rules], capture_output=True)
    assert again.returncode != 0 and path.read_bytes() == before
    return before

run('first', ['first','all','all'], (70,720))
run('all', ['all','all','all'], (560,5760))
rules = ['random:2','random:3','random:1']
a = run('random-a', rules, (60,180))
assert a == run('random-b', rules, (60,180))
assert a != run('random-c', rules, (60,180), 43)
run('indices', ['indices:1,3','all','first'], (140,180))
run('probe-indices', ['first','indices:1,3','indices:2,4'], (10,180))
run('none', ['first','none','first'], (0,90))
for i, rules in enumerate([
    ['indices:9','all','all'], ['random:9','all','all'],
    ['first','random:8','all'], ['indices:1,1','all','all'],
    ['none','all','all'], ['indices:1,','all','all'],
]):
    run(f'invalid-{i}', rules)
print('Planner semantics, reproducibility and rejection checks passed:', work)
