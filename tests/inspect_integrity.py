"""Read-only regression against an existing run; all mutations use isolated copies."""
import json
import hashlib
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time

root = Path(__file__).resolve().parents[1]
run = Path(sys.argv[1]).resolve()
exe = root / "build/Release/fp_inspect.exe"
work = Path(tempfile.mkdtemp(prefix="inspect-integrity-", dir=root / "test-output"))
fixture = work / "run"
(fixture / "templates").mkdir(parents=True)
(fixture / "tasks").mkdir()
plan = (run / "plan.tsv").read_text(encoding="utf-8").splitlines()
rows = [line.split("\t") for line in plan[1:]]
pair_lines = (run / "pairs.tsv").read_text(encoding="utf-8").splitlines()
results = {f[0]: f for f in (line.split("\t") for line in pair_lines[1:])}
chosen = {
    next(row[0] for row in rows if row[3] == kind and results[row[0]][2] == "OK")
    for kind in ("G", "I")
}
old_indices, new_indices = [{}, {}], [{}, {}]
for row in rows:
    for role, column in enumerate((4, 5)):
        old_indices[role].setdefault(row[column], len(old_indices[role]))
selected = [row[:] for row in rows if row[0] in chosen]
for row in selected:
    for role, column in enumerate((4, 5)):
        new_indices[role].setdefault(row[column], len(new_indices[role]))
provenance = json.loads((run / "provenance.json").read_text(encoding="utf-8"))
old_images = {str(Path(item["path"])): item for item in provenance["images"]}
new_images, paths = [], {}
for role, entries in enumerate(new_indices):
    for source, index in entries.items():
        key = "a" if role else "e"
        task = "attempt" if role else "enroll"
        original = old_indices[role][source]
        shutil.copyfile(run / f"templates/{key}{original}.bin", fixture / f"templates/{key}{index}.bin")
        shutil.copyfile(run / f"tasks/{task}{original}.tsv", fixture / f"tasks/{task}{index}.tsv")
        if source not in paths:
            destination = work / "source-images" / str(len(paths)) / Path(source).name
            destination.parent.mkdir(parents=True)
            shutil.copyfile(source, destination)
            paths[source] = destination.as_posix()
            item = dict(old_images[str(Path(source))])
            item["path"] = paths[source]
            new_images.append(item)
for row in selected:
    row[4], row[5] = paths[row[4]], paths[row[5]]
(fixture / "plan.tsv").write_text("\n".join([plan[0]] + ["\t".join(row) for row in selected]) + "\n", encoding="utf-8")
(fixture / "pairs.tsv").write_text("\n".join([pair_lines[0]] + ["\t".join(results[row[0]]) for row in selected]) + "\n", encoding="utf-8")
provenance["images"] = new_images
provenance["plan_sha256"] = hashlib.sha256((fixture / "plan.tsv").read_bytes()).hexdigest()
(fixture / "provenance.json").write_text(json.dumps(provenance, ensure_ascii=False, separators=(",", ":")), encoding="utf-8")


def check(name, expected, message=""):
    output = work / name
    result = subprocess.run([str(exe), str(fixture), str(output), "2"], capture_output=True)
    assert result.returncode == expected, (name, result.stderr)
    if message:
        assert message.encode() in result.stderr, (name, result.stderr)
    assert (output / "index.html").exists() == (expected == 0), name


check("valid", 0)
saved_plan=fixture / "plan.tsv"
original_plan=saved_plan.read_bytes()
saved_plan.write_bytes(original_plan+b"\n")
check("changed-plan",2,"Saved plan hash differs")
saved_plan.write_bytes(original_plan)
template = fixture / "templates/e0.bin"
original = template.read_bytes()
corrupt = bytearray(original)
corrupt[64] ^= 1
template.write_bytes(corrupt)
check("corrupt-template", 2, "Invalid/corrupt")
template.write_bytes(original)
table = fixture / "pairs.tsv"
original_table = table.read_text(encoding="utf-8")
lines = original_table.splitlines()
fields = lines[1].split("\t")
fields[4] = str(float(fields[4]) + 0.01)
lines[1] = "\t".join(fields)
table.write_text("\n".join(lines) + "\n", encoding="utf-8")
check("score-mismatch", 2, "differs from recorded score")
table.write_text(original_table, encoding="utf-8")
changed = Path(next(iter(paths.values())))
changed.write_bytes(changed.read_bytes() + b"\0")
check("changed-source", 2, "Source image changed")
cancel_output=work / "cancelled-inspection"
process=subprocess.Popen([str(exe),str(run),str(cancel_output),"100"],stdout=subprocess.PIPE,stderr=subprocess.PIPE)
deadline=time.monotonic()+10
while not cancel_output.exists():
    assert process.poll() is None and time.monotonic()<deadline,"Cancellation test did not start"
    time.sleep(0.002)
(cancel_output / "cancel.request").write_text("stop",encoding="ascii")
process.communicate(timeout=10)
assert process.returncode==130 and not (cancel_output / "index.html").exists(),"Cancelled inspection produced a final page"
print("PASS: valid run, changed plan/source, corrupt template, mismatched score and cooperative cancellation.")
