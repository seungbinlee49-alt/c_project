# 실험 결과

30회 대표 실행, 총 23,700개 계획 비교의 **수치 결과**를 모았다.
모든 실행은 제공된 개발 DB에서 측정했으며 독립 검증 결과가 아니다.

| 먼저 볼 파일 | 내용 |
|---|---|
| [동일 조건 비교](../docs/graph-v1-paired-results.md) | 현재 graph-v1과 v2의 원본·동일 잡음 비교 |
| [summary.csv](summary.csv) | 실행별 EER·EER*·FTE·FTA·성공/계획 쌍 수 |
| [manifest.json](manifest.json) | 원본 provenance·DLL·계획 해시, 공개 파일 SHA-256 |
| [runs/](runs/) | 실행별 원본 수치 JSON·임계값별 곡선 CSV |

## 선정 기준

| 실행 이름 | 알고리즘 | 조건 | 횟수 |
|---|---|---|---:|
| `baseline-db…` | 방향 히스토그램 | 원본 | 3 |
| `aligned-db…` | 방향장·질감 | 원본·가우시안 σ15·점 잡음 2% | 9 |
| `minutiae-v2-db…` | 특징점 v2 | 원본·가우시안 σ15·점 잡음 2% | 9 |
| `graph-v1-db…-clean`, `graph-v1-paired-db…` | graph-v1 후보 | 원본·v2와 동일 잡음 시드 | 9 |

`db1`=FVC2000 DB3, `db2`=FVC2002 DB3, `db3`=FVC2004 DB1.
지문 10개 × 촬영 8장, 지문별 첫 촬영본 등록, 자기 자신 제외, 본인 70쌍·타인 720쌍/실행이다.
중간 v1 후보, 중복 확인 실행, 시드가 달랐던 graph 초기 시험은 대표 집계에 포함하지 않았다.
전체 원본 실험은 로컬 `experiments/`에 남아 있다.

## 수치 해석

- JSON·CSV 비율은 **0~1**이다. README·비교표에서는 100을 곱한 %로 표시한다.
- EER은 실패를 포함한 과제의 이산 임계값 계산이다. EER*는 성공 점수만 사용한다.
- DB2 원본에서는 비교용 추출 1/80 실패가 1개 본인·9개 타인 비교에 전파된다. 실패를 제거하지 않았다.
- `null`이나 CSV 빈칸은 계산 불가 또는 당시 미기록을 뜻하며 0이 아니다.
- 시간은 당시 개발 PC의 측정치다. 병렬 실행·부하를 통제한 속도 우열 비교로 해석하지 않는다.
- 구형 실행에는 추출기 의존성 해시가 없으므로 manifest에 `null`로 기록한다.

## 공개 파일과 원본 보존

각 폴더에는 `metrics.json`, 가능한 경우 `timing.json`·`reference-checks.json`, `curve.csv`를 넣었다.
`curve.csv`는 threshold/FMR/FNMR을 담으며 지문 이미지나 특징량을 포함하지 않는다.
이미지·템플릿·경로가 들어간 plan/provenance/records·분석 HTML은 공개 대상에서 제외했다.
원본 provenance의 파일 해시와 이미지 해시 집합의 digest는 출처 확인용으로 남겼다.

```powershell
# 데이터가 없는 새 clone에서도 가능
python tools/export_results.py --verify
# 원본 experiments/가 있는 개발 PC에서 수치 파일을 다시 내보냄
python tools/export_results.py
```

내보내기는 원본 수치 파일의 바이트를 보존하고 요약값과 해시를 검증한다.
[재현 절차](../docs/data-setup.md) · [과적합 방지와 한계](../docs/validation-protocol.md)
