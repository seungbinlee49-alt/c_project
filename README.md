# 지문 정합 알고리즘

현재 방식은 **[DMD CPU64 INT8](algorithm-dmd/README.md)** 이다. 기존 **[V1](algorithm-v1/submission/README.md)** 은 비교·호환성 기준으로 보존한다.

## 바로 사용

**[DMD 실행 패키지 다운로드](https://github.com/seungbinlee49-alt/c_project/releases/tag/dmd-cpu64-int8-20261128)** → 압축 해제 → `runtime/x64/fingerprint_dmd_cpu64.dll` 선택.

`runtime/x64` 폴더 전체가 필요하다. Windows x64 CPU용이며 GPU·Python 설치가 필요하지 않다. 이전 V1/DMD 실험판의 템플릿은 재사용하지 않는다.

| 내용 | 위치 |
|---|---|
| DMD 소스·구조·빌드 | [algorithm-dmd](algorithm-dmd/README.md) |
| EER 및 CPU 시간 | [results](algorithm-dmd/results/) |
| PDF 기준 점검 | [요구사항 점검](algorithm-dmd/docs/요구사항_점검.txt) |
| 라이브러리·모델 출처 | [NOTICE](algorithm-dmd/NOTICE.txt) |
| 기존 V1 소스·DLL | [algorithm-v1/submission](algorithm-v1/submission/README.md) |

DMD는 공개 사전학습 모델을 사용하고, 과제 DB로 재학습하지 않았다. CPU용 INT8 양자화 보정에는 외부 예시를 사용했다. 정합과 KISA 인터페이스는 C++ DLL 내부에서 처리한다.

최종 후보는 CPU 연속 인식 27/27회, 팀 평가 프로그램 연결 9/9회에서 5초 이내였다. 가우시안 σ30 조건의 3개 DB 평균 EER은 0.446429%이며, DB2 단독은 1.339286%다. 시험 조건·정확도 손실·실행 시간 편차는 [검증 상세](algorithm-dmd/docs/최종_검증결과.txt)를 참고한다.

원본 지문 DB와 생성 템플릿은 저장소에 포함하지 않는다. 기존 평가 프로그램은 [src](src/), 사용법은 [docs/usage.md](docs/usage.md)에 있다. 최종 발표 설명도 선택한 DMD 구현과 맞춰야 한다.
