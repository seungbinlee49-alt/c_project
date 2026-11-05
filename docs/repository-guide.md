# 레포 관리

## 폴더 기준

| 폴더 | 내용 | Git 추적 |
|---|---|---|
| `src/` | C++ 알고리즘·평가 엔진·GUI | 예 |
| `assets/` | 분석 화면 HTML·JavaScript 원본 | 예 |
| `tests/` | 단위·API·고장·선택 규칙·브라우저 검증 | 예 |
| `tools/` | 결과 내보내기·검증·패키징 | 예 |
| `docs/` | 사용법·설계·실험 해석·남은 작업 | 예 |
| `results/` | 선별한 수치 결과·요약·해시 | 예 |
| `protocol/` | 설계·과거 버전 기록 | JSON만; 로컬 경로가 있는 exposed TSV 제외 |
| `third_party/` | 외부 특징 추출 라이브러리 소스·원본 라이선스 | 예; 중복 ZIP 제외 |
| `data/` | 수업 지문 DB | 아니오 |
| `experiments/` | 전체 로컬 실행 기록·특징량·절대 경로 | 아니오 |
| `analysis/` | 지문 이미지가 포함된 분석 HTML/PNG | 아니오 |
| `build/`, `build-win32/`, `dist/`, `test-output/` | 생성물 | 아니오 |

원본 실험 폴더와 이미지를 삭제하거나 옮기지 않는다. `results/`는 허용한 수치 파일만 복사한 공유용 결과다.
원본 PDF와 수업 ZIP은 레포 밖에 보관한다. 외부 라이브러리의 upstream 테스트 자료는 원본 소스와 함께 유지한다.

## 결과 업데이트

```powershell
# 원본 experiments/가 있는 개발 PC에서만 실행
python tools/export_results.py
python tools/compare_graph.py
# DB와 원본 experiments/ 없이도 가능한 검사
python tools/export_results.py --verify
```

내보내기는 30개 대표 실행을 선택한다. plan/provenance/records/pairs, 지문 이미지, 특징량은 복사하지 않는다.
구형 provenance에 없는 해시는 `null`로 남기며 새로 추정하지 않는다.
`protocol/frozen-*.json` 등의 소스 해시는 **기록 당시 버전**의 값이며 현재 HEAD 해시가 아니다.

## 협업 순서

`main`은 빌드 가능한 상태로 유지하고 기능별 브랜치에서 변경한다.
예: `feature/report-options`, `algorithm/extraction-robustness`.
PR에는 변경 목적, 검증 방법, 개발 데이터 노출 여부를 기록한다.
CI는 x64·Win32 빌드와 데이터 없는 단위·ABI·선택 규칙·결과 무결성 검사를 실행한다.
실제 지문 DB 평가와 네이티브 GUI 클릭 검증은 CI에 포함하지 않는다.

외부 라이브러리의 라이선스와 고지는 [NOTICE](../NOTICE.md) 및 원본 `COPYING*`, `COPYRIGHT.txt`를 따른다.
자체 코드에 대한 별도 오픈소스 라이선스는 아직 정하지 않았다.
