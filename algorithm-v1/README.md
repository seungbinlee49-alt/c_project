# 최종 알고리즘 v1

선택 버전: `algorithm-v1-20261114`. `src/`와 `bin/`은 선택 당시 배포본 그대로다.

## 알고리즘

1. 1/99 백분위 대비 정규화
2. FingerJetFX OSE 특징점 추출
3. 특징점 주변 배치를 공간 8×8칸·상대 방향 6구간의 384개 숫자로 표현
4. 회전·이동과 점 사이 거리·방향 관계를 확인하는 그래프 정합
5. 제한된 겹침 보정과 원 영상 방향장 검증

주변 배치 표현을 **기술자(descriptor)** 라고 한다. 추가 신경망 학습이나 GNN 없이 고정된 기하 규칙을 계산한다. 겹침 보정은 최대 2배로 제한한다. 가장 높은 기하 점수의 정렬 하나를 선택한 뒤, 원 영상 방향장의 불일치에 따라 0~1 계수를 곱한다. 점수는 확률이 아니다.

## 코드

| 파일 | 역할 |
|---|---|
| [photometric.h](src/photometric.h) | 대비 정규화 |
| [minutiae_algorithm.cpp](src/minutiae_algorithm.cpp) | FingerJet 추출과 DLL API |
| [minutiae.h](src/minutiae.h) | 특징점·주변 배치 표현·강체 정렬 |
| [minutiae_graph.h](src/minutiae_graph.h) | 거리·방향 관계의 일관성 평가 |
| [minutiae_overlap.h](src/minutiae_overlap.h) | 특징점 볼록껍질을 이용한 겹침 보정 |
| [minutiae_orientation.h](src/minutiae_orientation.h) | 원 영상의 방향 격자와 검증 |
| [api.h](src/api.h) | 입력 크기와 호출 타입 |
| [bin/](bin/) | x64·Win32별 선택 DLL 두 개 |
| [examples/compare.cpp](examples/compare.cpp) | 실제 DLL 호출 예제 |

## 사용

평가 프로그램과 같은 아키텍처의 `bin/x64/` 또는 `bin/win32/`에서 **fingerprint_algorithm.dll과 FRFXLL.dll을 함께** 사용한다. 해당 아키텍처의 Microsoft Visual C++ 2015-2022 런타임이 필요하다. 지문 DB·영상·템플릿은 포함하지 않는다.

| 함수 | 입력 | 출력 |
|---|---|---|
| `KISA_Enroll` | 타입, 등록 영상 | 등록 템플릿 |
| `KISA_Attempt` | 타입, 인증 영상 | 인증 템플릿 |
| `KISA_Compare` | 두 템플릿 | 원시 `double` 유사도 |

- 입력: 8비트 그레이스케일, 500ppi. 타입 1=448×478, 2=300×300, 3=640×480.
- 템플릿 출력 버퍼: **1,000,000바이트**. 실제 템플릿: **551,512바이트**. 오류 메시지 버퍼: 256바이트.
- 성공 반환값은 1이다. 오류 코드와 반환 크기도 확인한다. 유사도 0은 정상 비교 결과일 수 있다.
- 같은 모델로 등록·인증 템플릿을 모두 생성한다. 이전 형식과 섞지 않고 캐시는 두 DLL의 해시로 구분한다.
- 점수가 높을수록 유사하다. DLL은 점수를 반올림하거나 임계값으로 분류하지 않는다.

## 빌드

Visual Studio 2022의 C++ 도구와 CMake 3.20 이상을 사용한다. 저장소 루트에서 실행한다.

```powershell
cmake -S algorithm-v1 -B algorithm-v1/build-x64 -A x64
cmake --build algorithm-v1/build-x64 --config Release

# 32비트는 별도 빌드 폴더 사용
cmake -S algorithm-v1 -B algorithm-v1/build-win32 -A Win32
cmake --build algorithm-v1/build-win32 --config Release
```

호출 예제는 헤더 없이 픽셀만 저장한 RAW 파일 두 개를 받는다.

```powershell
./algorithm-v1/build-x64/Release/fp_compare.exe ./algorithm-v1/bin/x64/fingerprint_algorithm.dll 2 enroll.raw attempt.raw
```

## 결과

DB별 10지문 × 8촬영. 조건별 동일인 70쌍·타인 720쌍이다. 등록은 첫 원본 촬영이며 인증에만 노이즈를 적용한다.

| 원본 조건 | EER |
|---|---:|
| FVC2000 DB3 B | 1.409% |
| FVC2002 DB3 B | 1.548% |
| FVC2004 DB1 B | 4.226% |

원본·Gaussian15·impulse2%·Gaussian30·밝기·blur1의 **18조건 평균 EER은 3.019%**, 최악은 5.913%, 평균 FNMR@FMR≤1%는 5.397%다. 실패를 제외하지 않았으며, 평균은 조건별 지표의 산술 평균이다. [전체 18조건](results.csv)

미사용 FVC2000 DB1·DB2의 160장을 별도 평가했다. 원본 EER은 1.409%, 4.296%였지만 개발 고정 임계값의 FMR은 2.361%, 2.500%로 1%를 넘었다. 기존 자료와 사람 단위 분리는 입증하지 못했다. 독립 인증이나 최적 성능을 주장하지 않는다. [외부 6조건](external-validation.csv)

## 원본 확인과 라이선스

`python algorithm-v1/verify.py`로 [manifest.json](manifest.json)의 선택 소스·DLL 해시를 확인한다. 재빌드한 DLL의 파일 해시는 달라질 수 있다.

FingerJetFX OSE는 [공유 소스](../third_party/FingerJetFXOSE-master/)를 별도 `FRFXLL.dll`로 빌드한다. [저작권](../third_party/FingerJetFXOSE-master/COPYRIGHT.txt), [LGPL](../third_party/FingerJetFXOSE-master/COPYING.LESSER.txt), [NOTICE](../NOTICE.md)를 참고한다.
