"""Isolated loader/ABI tests; stdlib only. No biometric dataset required."""
import argparse
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile


def pe_info(path):
    data = path.read_bytes()
    pe = struct.unpack_from('<I', data, 0x3c)[0]
    assert data[pe:pe+4] == b'PE\0\0'
    count = struct.unpack_from('<H', data, pe+6)[0]
    opt_size = struct.unpack_from('<H', data, pe+20)[0]
    opt = pe+24
    directory = opt+(112 if struct.unpack_from('<H', data, opt)[0] == 0x20b else 96)
    sections = []
    for i in range(count):
        base = opt+opt_size+40*i
        virtual_size, virtual, size, raw = struct.unpack_from('<4I', data, base+8)
        sections.append((virtual, max(size, virtual_size), raw))
    def offset(rva):
        return next(raw+rva-start for start, size, raw in sections if start <= rva < start+size)
    def string(rva):
        p = offset(rva)
        return data[p:data.index(b'\0', p)].decode('ascii')
    exports = []
    export_rva = struct.unpack_from('<I', data, directory)[0]
    if export_rva:
        base = offset(export_rva)
        n = struct.unpack_from('<I', data, base+24)[0]
        names = offset(struct.unpack_from('<I', data, base+32)[0])
        exports = [string(struct.unpack_from('<I', data, names+4*i)[0]) for i in range(n)]
    imports = []
    import_rva = struct.unpack_from('<I', data, directory+8)[0]
    if import_rva:
        current = offset(import_rva)
        while any(data[current:current+20]):
            imports.append(string(struct.unpack_from('<I', data, current+12)[0]))
            current += 20
    return dict(exports=sorted(exports), imports=imports)


def run(build, output):
    build = build.resolve()
    env = os.environ.copy()
    windows = env.get('SystemRoot', env.get('SYSTEMROOT', 'C:/Windows'))
    env['SystemRoot'] = windows
    env['PATH'] = str(Path(windows)/'System32')
    records = []
    with tempfile.TemporaryDirectory(prefix='fp-v1-contract-') as folder:
        root = Path(folder)
        (root/'unrelated cwd').mkdir()
        raw = root/'input.raw'
        raw.write_bytes(bytes([128])*90000)
        for name in ['fingerprint_algorithm.dll', 'fingerprint_algorithm_eval.dll']:
            info = pe_info(build/name)
            assert info['exports'] == ['KISA_Attempt', 'KISA_Compare', 'KISA_Enroll'], info
            assert not any('frfxll' in item.lower() for item in info['imports']), info
            assert not any('vcruntime' in item.lower() or 'msvcp140' in item.lower() for item in info['imports']), info
            for fixture in ['sibling', 'missing', 'throwing']:
                target = root/(name+' '+fixture+' 한글 경로')
                target.mkdir()
                shutil.copy2(build/name, target/name)
                if fixture != 'missing':
                    shutil.copy2(build/('FRFXLL.dll' if fixture == 'sibling' else 'throwing_extractor.dll'),
                                 target/'FRFXLL.dll')
                def execute(*args):
                    return subprocess.run([str(build/'fp_probe.exe'), *map(str, args)],
                        cwd=root/'unrelated cwd', env=env, capture_output=True, text=True, timeout=30)
                loaded = execute('load', target/name)
                assert loaded.returncode == 0 and 'LOAD_OK' in loaded.stdout, loaded.stdout+loaded.stderr
                if fixture != 'sibling':
                    test = execute('enroll', target/name, 2, raw, root/'output.bin')
                    fields = test.stdout.split('\t')
                    expected = 110 if fixture == 'missing' else 999
                    assert test.returncode == 3 and fields[:2] == ['0', str(expected)], (fixture, test.returncode, test.stdout, test.stderr)
                else:
                    errors = execute('errors', target/name)
                    assert errors.returncode == 0 and 'ERROR_CONTRACT_OK' in errors.stdout, errors.stdout+errors.stderr
                    test = execute('enroll', target/name, 99, raw, root/'output.bin')
                    assert test.returncode == 3 and test.stdout.split('\t')[:2] == ['0', '101'], test.stdout
                records.append(dict(dll=name, fixture=fixture, plain_load=True,
                                    imports=info['imports'], exports=info['exports']))
    result = dict(status='PASS', cases=records)
    if output:
        output.write_text(json.dumps(result, indent=2)+'\n', encoding='utf-8')
    print('PASS: exact three exports, no implicit FRFXLL/VC runtime imports; plain loader; unrelated CWD/PATH; missing dependency and nonstandard exception containment')


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--build', type=Path, required=True)
    parser.add_argument('--out', type=Path)
    args = parser.parse_args()
    run(args.build, args.out)
