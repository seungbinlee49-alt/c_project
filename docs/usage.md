# 전체 사용 설명과 개발 이력

이 문서의 명령·경로는 **레포 루트 기준**이다. 최신 요약은 [루트 README](../README.md), graph-v1 비교는 [동일 조건 결과](graph-v1-paired-results.md)를 우선한다. 아래 v0.2 설명은 해당 단계의 개발 기록도 포함한다.

# 지문인식 평가 프로젝트

Windows x64/Win32 / C++17 프로젝트. 현재 권장 개발 후보는 **특징점 기반 fingerprint_minutiae.dll**이며, 전체 시험 엔진과 Windows GUI 초기 버전이 연결되어 있다. 기존 두 알고리즘은 비교 기준으로 보존한다.

## 인증 수준을 목표로 한 v0.2 고도화

실제 외부 제출은 하지 않는다. KISA K-NBTC를 목표 수준으로 삼는다. 규격과 현재의 차이는 `docs/certification-target.md`, 과적합 방지 절차는 `docs/validation-protocol.md`에 있다.

- 특징 추출: 공개 FingerJetFX OSE 라이브러리의 단위 기능을 별도 FRFXLL.dll로 사용. 직접 구현한 상대 특징점 descriptor와 기하학적 합의 정합을 연결했다. 출처·라이선스는 NOTICE.md에 명시한다.
- 첫 개발용 원본 EER: 1.62% / 12.82% / 5.70%. 인증 통과 또는 독립 검증 결과가 아니다.
- 센서 타입 1/2/3 외에 안내서의 5/7/8/9도 지원한다. KISA 입력 경로의 500dpi 가정은 실제 센서 시험 전에 확인이 필요하다.
- 템플릿 버전/손상 검사, 디코딩된 동일 이미지 차단, 픽셀 해시 기반 노출 감사, 임계값 보정/평가 분리 도구를 추가했다.
- x64 및 Win32의 ABI, 회전/이동, 손상 입력, 외부 형식 어댑터, 아키텍처 간 템플릿 호환 시험을 수행했다.
- GUI의 기본 DLL은 fingerprint_minutiae.dll이다. 실제 클릭 흐름의 검증은 남아 있다.

새 알고리즘 시험 예시:

```powershell
./build/Release/fp_bench.exe run ./my-plan.tsv ./build/Release/fingerprint_minutiae.dll ./my-minutiae-run 5000 4 clean 20261103
./tests/acceptance.ps1
```

`reference-checks.json`에는 FTE·FTA 및 참고 기준과의 로컬 비교를 저장한다. 공식 인증 상태는 항상 NOT_ASSESSED로 구분한다. 실행·소스 패키지의 설명은 `docs/local-package.md`에 있다.

## 특징점·정합 진단

저장된 특징점 v2 시험 결과에서 낮은 본인 점수·높은 타인 점수·처리 실패를 골라 브라우저 분석 화면을 만든다.

```powershell
./build/Release/fp_inspect.exe ./experiments/minutiae-v2-db2-clean ./analysis/db2 12
./build/Release/fp_inspect.exe --verify ./experiments/minutiae-v2-db2-clean
```

`analysis/db2/index.html`을 열면 특징점 위치·방향, 선택된 대응, 회전·이동, 잔차를 확인할 수 있다. 나란히/겹쳐 보기, 대응 선택, 품질 표시 필터, PNG 저장을 제공한다. 원본·시험 목록 해시와 템플릿 손상을 검사하고 기록된 점수와 재계산 점수가 다르면 분석을 거부한다. 자세한 사용법과 한계는 `docs/matching-inspection.md`에 있다.

Windows GUI에도 시험 완료 후 사용하는 `특징점·정합 분석` 버튼을 연결했다. 생성·중단은 백그라운드에서 처리하고, 완료된 분석을 기본 브라우저로 연다. 이 네이티브 버튼의 실제 클릭 흐름은 아직 미검증이며 CLI와 브라우저 뷰어는 별도 검증했다.

## 빌드

Visual Studio 2022의 C++ 데스크톱 개발 도구 및 Windows SDK 필요.

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

CMake가 PATH에 없다면 Visual Studio에 포함된 CMake 실행 파일을 사용한다.

- `build/Release/fp_gui.exe`: DLL·DB·저장 폴더, 등록 정책, 잡음, 병렬 수, 시간 제한, 시드를 선택하는 GUI. 시험 시작/중단/리포트 열기 지원. 빌드는 통과했지만 자동 화면 검사 도구의 앱 실행 승인 시간 초과로 실제 클릭 흐름은 미검증이다.
- `build/Release/fp_bench.exe`: 현재 검증된 배치 평가 경로.
- `build/Release/fingerprint_minutiae.dll`: 현재 특징점 기반 개발 후보. FRFXLL.dll을 같은 폴더에 둔다.
- `build/Release/fingerprint.dll`: 이전 평활화·방향장 정합·질감 비교 알고리즘.
- `build/Release/fingerprint_baseline.dll`: 처음 만든 방향 히스토그램 기준 알고리즘.
- `fp_task.exe`: DLL 호출을 격리하는 내부 실행 파일. 지우거나 별도로 이동하지 않는다.
- `fp_eval.exe` / `fp_worker.exe`: 최초의 두 이미지 연결 확인 도구. 본격 시험은 fp_bench를 사용한다.

## 전체 시험

ZIP에서 각 DB의 TIFF를 폴더로 푼 뒤 실행한다. 분석 중에 `data/`에 이미 풀어 두었다.

```powershell
./build/Release/fp_bench.exe plan ./data/FVC2000_DB3_B 1 ./my-plan.tsv 20261103 first
./build/Release/fp_bench.exe run ./my-plan.tsv ./build/Release/fingerprint.dll ./my-run 5000 4 clean 20261103
```

`plan`: DB 폴더, iType, 새 시험 목록 파일, 무작위 시드, 등록 정책 순서.
`run`: 목록 파일, DLL, 새 결과 폴더, 작업별 시간 제한(ms), 병렬 수(1~64), 잡음 조건, 잡음 시드 순서.
공백이 있는 경로는 따옴표로 감싼다. 기존 시험 목록과 결과 폴더는 덮어쓰지 않는다. TIFF/BMP/PNG와 프로파일 크기에 맞는 헤더 없는 8비트 RAW를 지원한다.

| iType | DB | 크기 |
|---|---|---|
| 1 | FVC2000_DB3_B | 448 × 478 |
| 2 | FVC2002_DB3_B | 300 × 300 |
| 3 | FVC2004_DB1_B | 640 × 480 |

- `first`: 지문 ID별 가장 작은 촬영 순번을 등록. 80장 DB 기준 본인 70쌍 + 타인 720쌍.
- `all`: 각 이미지를 각각 등록. 방향성 있는 전체 조합으로 본인 560쌍 + 타인 5,760쌍.
- 목록의 행을 일부만 남기면 부분 시험이 된다. 헤더와 각 행의 필드는 유지한다.
- 목록에 정확한 비교 순서가 저장되며, 읽을 때 중복·동일 파일·파일명과 본인/타인 라벨의 모순을 검사한다.
- 병렬 실행에서는 시작/완료 시각의 순서가 운영체제에 따라 달라진다. 비교 목록의 논리 순서와 난수 조건을 재사용한다.
- Ctrl+C 또는 결과 폴더에 `cancel.request` 파일을 생성하면 진행 중인 작업을 중단한다. 부분 원본 기록은 남기고 최종 통계는 생성하지 않는다.

## 잡음 조건

`clean`, `gaussian15`, `gaussian30`, `impulse2`, `blur1`, `brightness`.
등록에는 원본을 사용하고, 비교 이미지에만 변형을 적용한다. 이미지별 변형은 시드에 의해 고정되며 같은 실행 안에서 재사용한다. 확률 분포 구현까지 다른 컴파일러/표준 라이브러리 사이의 동일성을 보장하지는 않는다.

- gaussian15/30: 8비트 픽셀에 표준편차 15/30 가우시안 잡음 후 범위 제한.
- impulse2: 각 픽셀에 약 1% 확률로 검정, 약 1% 확률로 흰색.
- blur1: [1,2,1] 분리형 3×3 커널.
- brightness: 픽셀값 × 0.65 + 35.

교수님의 비공개 잡음 조건을 재현한다는 뜻은 아니다.

## 결과 파일

| 파일 | 내용 |
|---|---|
| plan.tsv | 실행한 비교 목록 |
| provenance.json | DLL·목록·각 이미지의 SHA-256, 잡음 및 실행 조건 |
| records.jsonl | 등록·비교용 특징 추출·비교의 원본 결과, 오류 코드, 메시지 바이트의 hex, 시간 |
| run.json | RUNNING / COMPLETE / FAILED / CANCELLED 및 비교 수 |
| metrics.json | FTE, REJ, EER, EER*, ZeroFMR/FNMR, FMR100/1000, 특징량 크기·시간 |
| pairs.tsv | 목록 순서대로 정리한 비교 결과. 최근 엔진 버전에서 추가 |
| timing.json | 전체·본인·타인의 성공 매칭 시간. 최근 엔진 버전에서 추가 |
| curve.csv | 임계값별 FMR/FNMR 원본 수치 |
| report.html | 개발용 HTML 리포트 |
| scores.svg / threshold.svg / det.svg | 별도 벡터 그래프 |
| templates/ | 실행 중 재사용한 특징량 |
| tasks/ | 각 작업의 상태와 종료 직전 단계 |

비율은 JSON/CSV에서 0~1이다. 계산 불가능한 값은 0으로 꾸미지 않고 null로 표시한다.
`det.svg`는 현재 선형 축이다. 로그/정규편차 축과 보고서 항목 선택, Word 출력은 남은 작업이다.

## 캐시 및 시간 정의

등록은 선택된 등록 이미지별 한 번 실행한다. 비교용 특징도 비교 이미지별 한 번 추출하며, 각 비교 호출은 별도 프로세스에서 실행한다. 특징 추출의 실패는 해당 이미지에 의존하는 모든 비교로 전파한다. 이는 명시적인 캐시 시험 정책이며, 매번 특징을 다시 추출하는 정책은 아직 없다.

FTE 분모는 고유 등록 시도 수. FMR/FNMR 분모는 전체 사전 정의 비교 수이다. 본인 등록 실패와 매칭 실패를 FNMR 분자에 포함한다. EER*는 성공 점수만 사용한다. REJ 분모는 등록이 성공하여 매칭 처리에 진입한 비교 수이며, 비교용 추출 실패도 매칭 실패로 센다.

DLL 호출 시간과 프로세스/이미지 IO 시간은 구분한다. 현재 평균 시간은 성공한 호출 기준이다. `timing.json`의 논리 매칭 시간은 해당 이미지의 캐시된 Attempt 호출 시간 + 실제 Compare 호출 시간이며, 프로세스 생성과 IO는 포함하지 않는다.

## 검증과 결과

```powershell
./tests/smoke.ps1
./tests/batch.ps1
```

- 두 이미지 통합 검사 11건.
- 배치 시나리오 11건: 실패·시간 초과·충돌·잘못된 크기/점수·입력 변경/출력 초과 쓰기 감지, 병렬 재현성, 취소.
- 지표 경계 사례와 고정 시드 잡음 검사.
- 실제 DB 3개에서 12개 실행, 총 9,480쌍 비교 및 원본 기록과 모든 FMR/FNMR 출력 지점 대조.

상세 수치와 해석은 `docs/experiment-results.md`, 설계와 남은 요구사항은 `docs/architecture.md`에 있다. 알고리즘은 개선되었지만 EER이 아직 높고, 이번 개발 데이터 결과만으로 비공개 시험 성능을 보장할 수 없다.

## Graph-v1 및 선택 규칙

`fingerprint_graph.dll`은 별도 정합 후보다. [개발 비교](graph-v1-results.md)에 수치와 한계를 기록했다. `fp_plan.exe`와 GUI에서 등록·진짜·가짜 선택을 각각 first/all/random:N/indices로 지정한다.

잡음 시드를 맞춘 최종 개발 비교: [동일 조건 결과](graph-v1-paired-results.md).
