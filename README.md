# 지문인식 알고리즘 & 평가 프로그램

[![Windows build and tests](https://github.com/seungbinlee49-alt/c_project/actions/workflows/windows.yml/badge.svg)](https://github.com/seungbinlee49-alt/c_project/actions/workflows/windows.yml)

C++17 / Windows x64·Win32 기반 수업 프로젝트. **지문 특징점 추출·정합 알고리즘**과 **재현 가능한 평가 프로그램**을 함께 개발한다. 교수님이 제공 데이터에 잡음을 섞어 평가하는 상황을 고려하며, 과적합 방지와 실패를 포함한 측정을 중시한다.

[실행 방법](docs/data-setup.md) · [문서 전체](docs/README.md) · [코드 지도](src/README.md) · [실험 결과](results/README.md) · [남은 작업](docs/architecture.md)

## 현재 구현

- **알고리즘:** 방향 히스토그램 → 방향장·질감 → 특징점 v2 → 관계 일관성을 추가한 graph-v1 후보. 버전별 DLL을 분리했다.
- **평가:** 등록·본인·타인 선택, 고정 시드 잡음, 프로세스 격리, 시간 초과·충돌·잘못된 출력 감지, 취소, JSON/CSV/HTML/SVG 보고서.
- **선택 규칙:** 첫 이미지, 전체, 무작위 N장, 지정 순번. 같은 입력·시드는 같은 시험 목록을 만든다.
- **분석:** 특징점 위치·방향·대응점·회전·이동을 보는 브라우저 화면. 저장 점수와 재계산 점수를 대조한다.
- **검증:** x64·Win32 단위/API 검사, 고장 주입, 데이터 노출 감사, 임계값 보정과 평가 분리 도구.

기본 GUI DLL은 `fingerprint_minutiae.dll`이다. 새 후보는 `fingerprint_graph.dll`을 선택해 시험한다.

## 성능 현황

**개발 데이터의 EER(%, 낮을수록 좋음)**. 본인 70쌍·타인 720쌍/DB, 첫 촬영본 등록. 실패를 포함한 과제 정의로 계산했다.

| 데이터 | 초기 기준 | 방향장·질감 | 특징점 v2 | graph-v1 후보 |
|---|---:|---:|---:|---:|
| FVC2000 DB3 | 27.11 | 21.96 | 1.62 | **1.48** |
| FVC2002 DB3 | 35.70 | 24.23 | 12.82 | **8.52** |
| FVC2004 DB1 | 42.82 | 34.23 | 5.70 | **5.70** |

동일 잡음 시드의 9개 비교 조건에서 graph-v1은 v2보다 8개 개선·1개 동일했다.
[동일 조건 상세표](docs/graph-v1-paired-results.md) · [30회 수치 결과](results/summary.csv)

이미 살펴본 세 DB는 모두 **개발에 노출된 데이터**다. 독립 검증과 인증 결과가 아니며, DB2의 추출 실패와 높은 오류율이 남아 있다. K-NBTC를 목표 기준으로 참고하며 실제 외부 제출은 하지 않았다. 목표와 현재 차이는 [여기](docs/certification-target.md)에 정리했다.

## 폴더 구성

```text
src/           알고리즘 DLL, 평가 엔진, GUI, 지표, 분석기
assets/        정합 분석 화면의 HTML·JavaScript 원본
tests/        단위·API·고장·선택 규칙·브라우저 검사
tools/        결과 내보내기·무결성 검사·패키징
docs/         사용법·설계·실험 해석·개발 이력
results/      수치 결과·요약 CSV·출처 및 파일 해시
protocol/     알고리즘 설계와 과거 버전 기록
third_party/  외부 특징 추출기 소스·라이선스
.github/      Windows x64·Win32 CI
```

지문 DB·특징량·이미지를 포함한 분석 결과·빌드 파일은 Git에서 제외한다.
전체 로컬 실행 기록은 `experiments/`에 보존하고, 공개 가능한 수치만 `results/`로 내보낸다.
[Git 추적 범위와 협업 방식](docs/repository-guide.md)

## 빌드와 실행

Visual Studio 2022의 **C++ 데스크톱 개발 도구**, Windows SDK, CMake가 필요하다. 레포 루트에서 실행한다.

```powershell
git clone https://github.com/seungbinlee49-alt/c_project.git
cd c_project
cmake -S . -B build -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
./build/Release/fp_gui.exe
```

Win32는 `cmake -S . -B build-win32 -A Win32`로 별도 구성한다.
`FRFXLL.dll`, `fp_task.exe`, `inspect-assets/`는 실행 파일과 함께 유지한다.
수업 DB는 [데이터 준비](docs/data-setup.md)에 따라 로컬에 둔다. PDF의 타입 `1, 2, 2`는 `1, 2, 3`으로 해석한다.

```powershell
New-Item -ItemType Directory -Force experiments | Out-Null
./build/Release/fp_plan.exe ./data/FVC2000_DB3_B 1 ./experiments/my-plan.tsv 20261103 first all all
./build/Release/fp_bench.exe run ./experiments/my-plan.tsv ./build/Release/fingerprint_graph.dll ./experiments/my-run 5000 4 clean 20261103
```

시험 결과의 `report.html`을 열어 지표를 확인한다. 기존 목록과 출력 폴더는 덮어쓰지 않는다.
[전체 사용 설명](docs/usage.md) · [정합 시각화](docs/matching-inspection.md)

## 검증

아래 검사는 지문 DB가 없어도 실행할 수 있다. Python 3는 결과·선택 규칙 검사에만 필요하다.

```powershell
ctest --test-dir build -C Release --output-on-failure
./build/Release/fp_abi_test.exe ./build/Release/fingerprint_graph.dll
python tests/planner_test.py
python tools/export_results.py --verify
```

CI도 x64·Win32에서 위 검사를 실행한다. 실제 DB 시험·브라우저 검사·네이티브 GUI 클릭은 별도다.
네이티브 GUI 실제 클릭, 독립 데이터 일반화, 깨끗한 PC 설치 검증은 아직 완료하지 않았다.

## 다음 과제

- 독립 지문과 미관측 잡음·변형 검증, DB2 특징 추출 안정성 개선.
- GUI 실제 실행·취소 시연, 보고서 항목 선택과 Word 출력.
- 개선된 DET 축, 매번 추출하는 평가 정책, 중단 후 이어서 실행.

[구현 상태 전체](docs/architecture.md) · [과적합 방지 절차](docs/validation-protocol.md)

## 외부 코드와 고지

This product includes the DigitalPersona FingerJetFX OSE fingerprint feature extractor. (http://digitalpersona.com/fingerjetfx)

FingerJetFX OSE의 **특징 추출 라이브러리**를 별도 DLL로 사용한다. 자체 코드는 descriptor·정합·평가·보고서·GUI를 담당한다. 외부 코드의 원본 라이선스와 조건은 [NOTICE](NOTICE.md), [COPYRIGHT](third_party/FingerJetFXOSE-master/COPYRIGHT.txt), [LGPL](third_party/FingerJetFXOSE-master/COPYING.LESSER.txt)을 확인한다. 자체 코드의 별도 오픈소스 라이선스는 아직 정하지 않았다.
