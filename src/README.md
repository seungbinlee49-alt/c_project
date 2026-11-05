# 코드 지도

| 역할 | 파일 |
|---|---|
| DLL 입력·출력 규격과 센서 크기 | `api.h` |
| 초기 기준 알고리즘 | `baseline.cpp` |
| 방향장·질감 알고리즘 | `improved.cpp` |
| 특징점 추출·DLL 진입점 | `minutiae_algorithm.cpp` |
| v2 특징점 descriptor·기하 정합 | `minutiae.h` |
| graph-v1 관계 일관성 정합 | `minutiae_graph.h` |
| 등록·본인·타인 선택 규칙과 시험 계획 | `planner.cpp` |
| 전체 평가·기록·캐시·실패 전파 | `bench.cpp` |
| DLL 호출을 격리하는 작업 프로세스 | `task.cpp`, `process.h` |
| 지표·HTML/SVG 보고서 | `metrics.h`, `report.h` |
| 이미지 읽기·고정 시드 잡음 | `image.h` |
| 메모리 보호·결과 형식·해시 | `core.h`, `provenance.h` |
| 특징점과 정합 분석 | `inspect.cpp` |
| 노출·중복 감사 | `protocol.cpp` |
| 개발 임계값 고정·고정 임계값 평가 | `calibrate.cpp` |
| Windows GUI | `gui.cpp` |
| FVC 호환 로컬 어댑터 | `fvc_adapter.cpp` |
| 고장 주입 테스트용 DLL | `fixture.cpp` |
| 초기 두 이미지 평가 도구 | `evaluator.cpp`, `worker.cpp` |

실행 흐름: `fp_gui → fp_plan → fp_bench → fp_task → 알고리즘 DLL → FRFXLL.dll`.
지표는 `fp_bench`, 저장된 정합 설명은 `fp_inspect`에서 만든다.
`FRFXLL`은 외부 특징 추출기이며 정합·평가·GUI는 이 프로젝트 코드다.
