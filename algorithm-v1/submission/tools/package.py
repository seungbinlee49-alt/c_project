"""Build an allowlisted DLL installation ZIP with corresponding LGPL source."""
from pathlib import Path
import argparse,hashlib,json,re,shutil,tempfile,zipfile

HERE=Path(__file__).resolve().parent
SUBMISSION=HERE.parent
REPO=SUBMISSION.parents[1]

def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()

def package(name):
    if not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9._-]*',name):raise ValueError('Invalid tag')
    dist=SUBMISSION/'dist';dist.mkdir(exist_ok=True)
    target=dist/name
    if target.exists() or (dist/(name+'.zip')).exists():raise FileExistsError('Preserve existing release')
    target.mkdir()
    for arch in ('x64','win32'):
        build=SUBMISSION/f'build-{arch}/Release';dest=target/'bin'/arch;dest.mkdir(parents=True)
        for file in ('fingerprint_algorithm.dll','fingerprint_algorithm_eval.dll','FRFXLL.dll','fp_compare.exe'):
            shutil.copy2(build/file,dest/file)
    shutil.copy2(SUBMISSION/'README.md',target/'README.md')
    shutil.copytree(SUBMISSION/'results',target/'results')
    for file in ('install.ps1','uninstall.ps1'):shutil.copy2(HERE/file,target/file)
    licenses=target/'licenses';licenses.mkdir()
    vendor=REPO/'third_party/FingerJetFXOSE-master'
    for name_ in ('COPYRIGHT.txt','COPYING.txt','COPYING.LESSER.txt'):shutil.copy2(vendor/name_,licenses/name_)
    shutil.copy2(REPO/'NOTICE.md',licenses/'NOTICE.md')
    with tempfile.TemporaryDirectory(prefix='source-stage-',dir=dist) as temporary:
        source=Path(temporary)
        shutil.copytree(vendor,source/'third_party/FingerJetFXOSE-master')
        shutil.copytree(REPO/'algorithm-v1/src',source/'algorithm-v1/src')
        for folder in ('src','tests','tools','examples'):
            shutil.copytree(SUBMISSION/folder,source/'algorithm-v1/submission'/folder,
                            ignore=shutil.ignore_patterns('__pycache__','*.pyc'))
        for name_ in ('CMakeLists.txt','README.md'):
            shutil.copy2(SUBMISSION/name_,source/'algorithm-v1/submission'/name_)
        shutil.copy2(REPO/'.clang-format',source/'.clang-format')
        shutil.copy2(REPO/'NOTICE.md',source/'NOTICE.md')
        with zipfile.ZipFile(target/'corresponding-source.zip','w',zipfile.ZIP_DEFLATED) as z:
            for file in sorted(source.rglob('*')):
                if file.is_file():z.write(file,file.relative_to(source))
    files={str(p.relative_to(target)).replace('\\','/'):sha(p) for p in sorted(target.rglob('*')) if p.is_file()}
    metadata=dict(product='FingerprintV1Submission',version=name,algorithm='V1; unchanged scoring rules',
                  template_version=2,default_evaluator_dll='fingerprint_algorithm_eval.dll',
                  runtime='Windows x64/Win32; static Microsoft CRT; separate replaceable FRFXLL.dll',files=files)
    (target/'package.json').write_text(json.dumps(metadata,indent=2)+'\n',encoding='utf-8')
    archive=dist/(name+'.zip')
    with zipfile.ZipFile(archive,'w',zipfile.ZIP_DEFLATED) as z:
        for file in sorted(target.rglob('*')):
            if file.is_file():z.write(file,Path(name)/file.relative_to(target))
    print(json.dumps(dict(path=str(archive),bytes=archive.stat().st_size,sha256=sha(archive)),indent=2))

if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--name',default='v1-submission-20261123')
    package(parser.parse_args().name)
