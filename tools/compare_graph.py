"""Generate the paired development report only from matched evaluation protocols."""
import json
from pathlib import Path

root = Path(__file__).resolve().parents[1]
lines = ['# 동일 잡음으로 비교한 Graph-v1 개발 결과', '',
         '이미 노출된 개발 DB 결과다. 독립 검증이나 인증 결과가 아니다.', '',
         '| DB | 조건 | v2 EER (%) | graph EER (%) | 차이 (%p) |',
         '|---|---|---:|---:|---:|']
for db in range(1, 4):
    for noise in ('clean', 'gaussian15', 'impulse2'):
        baseline = root / 'experiments' / f'minutiae-v2-db{db}-{noise}'
        candidate = root / 'experiments' / (
            f'graph-v1-db{db}-clean' if noise == 'clean'
            else f'graph-v1-paired-db{db}-{noise}')
        def load(folder, name):
            return json.loads((folder / name).read_text(encoding='utf-8'))
        a, b = load(baseline, 'provenance.json'), load(candidate, 'provenance.json')
        keys = ['plan_sha256', 'noise']
        if 'extractor_dependency_sha256' in a and 'extractor_dependency_sha256' in b:
            keys.append('extractor_dependency_sha256')
        if noise != 'clean':
            keys.append('noise_seed')
        for key in keys:
            assert a[key] == b[key], (db, noise, key)
        assert {(i['path'],i['sha256']) for i in a['images']} == {(i['path'],i['sha256']) for i in b['images']}, (db, noise, 'images')
        ma, mb = load(baseline, 'metrics.json'), load(candidate, 'metrics.json')
        for key in ('planned_genuine', 'planned_impostor', 'successful_genuine', 'successful_impostor'):
            assert ma[key] == mb[key], (db, noise, key)
        x, y = ma['EER'] * 100, mb['EER'] * 100
        lines.append(f'| {db} | {noise} | {x:.4f} | {y:.4f} | {y-x:+.4f} |')
lines += ['', '계획·원본 이미지 해시·잡음 조건의 일치를 자동 확인했다. 구형 기록에는 추출 라이브러리 해시가 없어 그 항목은 소급 검증하지 못한다.',
    '실패를 포함한 프로젝트 EER 정의를 사용한다. 모든 파라미터는 DB 공통이며 기존 v2는 보존한다.',
    '이 비교는 두 약한 잡음 조건뿐이다. 미관측 강도의 잡음, 회전·부분 지문·압력 변형과 독립 지문에서 추가 평가가 필요하다.',
    'DB2 clean의 특징 추출 실패도 남아 있으므로 추출 안정성 개선은 별도 과제다.']
out = root / 'docs/graph-v1-paired-results.md'
out.write_text('\n'.join(lines) + '\n', encoding='utf-8')
print(out)
