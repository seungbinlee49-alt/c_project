# 검증 실행 안내

모든 명령은 레포 루트에서 실행한다. 먼저 Release 빌드가 필요하다.

| 검사 | 명령 / 파일 | 지문 데이터 |
|---|---|---|
| 지표·정합·기본 DLL API | `ctest --test-dir build -C Release --output-on-failure` | 불필요 |
| graph DLL API | `./build/Release/fp_abi_test.exe ./build/Release/fingerprint_graph.dll` | 불필요 |
| 선택 규칙·시드 재현·오류 거부 | `python tests/planner_test.py` | 불필요; 임시 파일명만 생성 |
| 공개 결과 해시·요약 대조 | `python tools/export_results.py --verify` | 불필요 |
| 초기 이미지 연결·고장 시험 | `tests/smoke.ps1 -DataRoot ZIP폴더` | DB ZIP 필요 |
| 배치 격리·실패·병렬·취소 | `tests/batch.ps1 -Plan 로컬계획.tsv` | DB 또는 유효한 로컬 계획 필요 |
| 외부 형식·아키텍처·노출 감사 | `tests/acceptance.ps1` | DB 및 로컬 노출 inventory 필요 |
| 저장 점수·특징량·변조·취소 | `python tests/inspect_integrity.py 실행폴더` | 로컬 실행 및 원본 이미지 필요 |
| 브라우저 분석 화면 | `node tests/inspect_browser.cjs 분석/index.html 새출력폴더` | 생성된 로컬 분석 화면 필요 |

브라우저 검사는 Playwright와 Edge가 필요하다. `FP_BROWSER` 환경 변수로 다른 실행 파일을 지정할 수 있다.
Node/Python 도구는 C++ 실행 프로그램의 필수 런타임이 아니라 개발 검증용이다.
검사 출력은 Git에서 제외된 `test-output/`에 둔다.

CI는 위 표의 처음 네 항목을 x64·Win32에서 실행한다.
실제 DB 성능, 독립 데이터 일반화, 네이티브 GUI 클릭 검증을 CI 성공으로 대신하지 않는다.
