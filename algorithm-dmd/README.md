# DMD CPU64 INT8

현재 지문 정합 구현이다. **Windows x64 CPU**에서 실행하며, 모델·실행 파일은 [릴리스](https://github.com/seungbinlee49-alt/c_project/releases/tag/dmd-cpu64-int8-20261128)에서 받는다. 기존 [V1](../algorithm-v1/submission/README.md)은 비교·호환성 기준으로 보존한다.

## 실행

1. 릴리스 ZIP을 풀고 `runtime/x64` 폴더 전체를 유지한다.
2. 평가 프로그램에서 `runtime/x64/fingerprint_dmd_cpu64.dll`을 선택한다.
3. 이전 V1/DMD 실험판 템플릿을 재사용하지 말고 새 평가를 시작한다.

GPU·Python 설치는 필요하지 않다. DLL 외에 `dmd-int8.onnx`, ONNX Runtime, FingerJet 등 부속 파일이 필요하다. 폴더와 파일 무결성은 릴리스의 `package.json`으로 확인한다. [ZIP SHA256](release.json)을 제공한다.

## 구조

대비 보정 → FingerJet 특징점 추출 → 품질순 최대 64개 → 원본에서 방향을 맞춘 128×128 패치 → DMD 특징·마스크 → Hungarian 대응 선택 → LSAR 관계 검증 → 0~1, 소수점 셋째 자리 유사도.

공개 사전학습 DMD 모델을 사용하고 과제 DB로 재학습하지 않았다. INT8 양자화 범위는 외부 CrossMatch 예시 16장·64패치로 보정했다. 그 외부 영상은 INT8의 독립 검증 자료로 간주하지 않는다. 실행은 C++17 DLL 안에서 처리하며 외부 추론 프로그램을 호출하지 않는다.

설정은 **CPU / INT8 / 64개 / 4스레드 / 배치 8**로 고정한다. 모델 SHA256을 검사하며, 다른 모델이나 이전 템플릿이 섞이면 거부한다.

## 검증 결과

같은 입력에서 실제 DLL이 추출한 템플릿을 비교한 3개 DB 평균 EER이다. 조건별 본인 70쌍·타인 720쌍, 실패 포함 FVC EER을 사용한다.

| 조건 | FP32·64개 | INT8·64개 |
|---|---:|---:|
| 원본 | 0% | 0% |
| 가우시안 σ30 | 0.069444% | 0.446429% |
| 새 시드의 픽셀별 σ0~45 | 0% | 0% |

INT8의 DB2 가우시안 EER은 **1.339286%**로 정확도 손실이 있다. [조건별 수치](results/accuracy.csv)를 제공한다. 새 잡음은 같은 손가락의 변형이며 독립 손가락 일반화의 증명은 아니다.

- 최종 CPU 연속 인식: **27/27회 5초 미만**, 최초 모델 로딩 포함 최대 **1.516초**.
- 팀 평가 프로그램 CLI: **9/9회**, 최대 **1.275초**.
- 이전 동일 수치 후보 시험에서는 각각 최대 **4.418초 / 4.672초**도 측정됐다. 다른 CPU·부하·동시 Worker 수에서의 보장은 아니다.
- 최종판 점수 회귀 7,110쌍 일치, 대표 18개 실제 추출 본문 동일, Worker 1/2 부분 평가 결과 동일, 오류·경계·소스 재빌드·설치/제거 검증을 수행했다.

[검증 상세](docs/최종_검증결과.txt) · [PDF 기준 점검](docs/요구사항_점검.txt) · [팀 평가 재현 설정](docs/평가_재현설정.json)

과제 PDF에는 함수당 250ms 또는 EER 1%의 합격선이 없다. 5초는 별도 운영 검증 목표다. 공개 라이브러리의 ‘단위 기능’ 범위에 대한 최종 해석은 교수자에게 있으며, 이 결과는 K-NBTC 인증이 아니다.

## 소스 빌드

MSVC C++ 도구와 Windows SDK가 있는 환경에서:

```powershell
python algorithm-dmd/verify.py
cmake -S algorithm-dmd -B algorithm-dmd/build -A x64
cmake --build algorithm-dmd/build --config Release
```

실행하려면 빌드한 DLL을 릴리스의 모델·부속 DLL과 같은 폴더에 둔다. `-DBUILD_FINGERJET_FROM_SOURCE=ON`으로 저장소의 FingerJet 소스도 빌드할 수 있다. Python은 개발용 파일 검증 도구에만 사용하며 제품 실행 의존성이 아니다.

## 코드와 출처

- [algorithm.cpp](src/algorithm.cpp): KISA API, 특징점·패치·CPU 추론, 템플릿과 오류 처리
- [matcher.h](src/matcher.h): 마스크 기반 유사도, Hungarian 대응, LSAR
- [model_hash.h](src/model_hash.h): 모델 SHA256 확인
- [NOTICE](NOTICE.txt): DMD 논문·공식 코드, FingerJet, ONNX Runtime 출처
- [발표 연결 요약](docs/발표_연결요약.txt): 기존 V1 발표와 달라진 설명

모델·런타임이 있는 약 48MB 패키지는 Git 이력 대신 릴리스에 보관한다. 원본 지문 DB·생성 템플릿·개인 로컬 경로가 든 실험 로그는 추가하지 않는다.
