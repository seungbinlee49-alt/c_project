# 지문 정합 알고리즘 v1

최종 선택한 알고리즘의 **[소스·DLL·사용법은 algorithm-v1/submission/](algorithm-v1/submission/README.md)** 에 모았다.

**팀원 평가 프로그램용:** `algorithm-v1/submission/bin/x64/` 또는 `win32/`의 **fingerprint_algorithm_eval.dll + FRFXLL.dll**을 사용한다. 최종 점수를 소수점 3자리로 반환한다. 템플릿 형식이 v2로 바뀌었으므로 등록·인증 템플릿과 이전 비교 캐시를 다시 생성한다. 기존 V1은 비교 기준으로 보존했다.

## 알고리즘

1. 1/99 백분위 대비 정규화
2. FingerJetFX OSE 특징점 추출
3. 특징점 주변 배치를 8×8×6 숫자로 표현
4. 회전·이동과 점 사이 관계를 확인하는 그래프 정합
5. 제한된 겹침 보정과 원 영상 방향장 검증

추가 신경망 학습 없이 동작하는 C++17 / Windows DLL이다. 학습된 GNN은 아니다.

## 핵심 결과

| 개발 평가 | 결과 |
|---|---:|
| 3 DB × 6조건 평균 EER | 3.019% |
| 최악 조건 EER | 5.913% |
| 평균 FNMR @ FMR≤1% | 5.397% |

개발에 사용한 240장 기준의 조건별 평균이다. [전체 수치](algorithm-v1/results.csv)와 [미사용 영상 평가](algorithm-v1/external-validation.csv)를 제공한다. 미사용 영상의 고정 임계값 FMR은 1%를 초과했으며, 사람 단위 독립성·인증 성능은 확인하지 못했다.

기존 평가 프로그램은 [src/](src/), 실행 방법은 [docs/usage.md](docs/usage.md)에 있다. 기본 알고리즘 대신 최종 v1 DLL을 명시적으로 선택한다.
