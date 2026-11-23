# V1 제출용 DLL

기존 V1의 FingerJet 추출·주변 배치·정합 기준과 점수 계산을 유지한 배포용 구현이다. 이전 원본은 [상위 폴더](../README.md)에 보존했다. 템플릿 저장 형식은 **v2**로 바뀌었으므로 등록·인증 템플릿을 모두 다시 생성한다.

## 평가 프로그램에 전달할 파일

같은 폴더에 다음 두 파일을 둔다. x64 Worker에는 `bin/x64`, 32비트 Worker에는 `bin/win32`를 사용한다.

- **fingerprint_algorithm_eval.dll**: 평가 대상으로 선택. 점수를 소수점 3자리로 반환.
- **FRFXLL.dll**: 별도로 교체 가능한 공개 FingerJet 추출 라이브러리.

`fingerprint_algorithm.dll`은 반올림 전 정밀 점수 비교용이다. 두 DLL의 추출·정합 규칙과 템플릿 형식은 같지만 결과 캐시는 DLL 해시별로 구분한다.

제출 DLL은 `KISA_Enroll`, `KISA_Attempt`, `KISA_Compare` 세 함수만 export한다. 입력 영상은 헤더 없는 8비트 흑백 픽셀이며, 과제 타입 **1=448×478, 2=300×300, 3=640×480, 모두 500ppi**다. 원본 PDF의 타입 중복 표기는 1·2·3으로 적용했다.

출력 템플릿 버퍼는 기존 규격대로 **1,000,000바이트**를 준비하고, 실제 반환 크기만 저장한다. 오류 메시지는 256바이트다. 반환값 1은 호출 성공이고, 유사도는 별도의 `double*` 출력이다. 유사도 0도 정상 결과일 수 있다. Windows x64/Win32와 정적 Microsoft CRT로 빌드하므로 별도 VC++ 재배포 패키지에 의존하지 않는다.

## 수정 내용

- 알고리즘 DLL 자신의 위치를 구하고 그 폴더의 FRFXLL을 명시적으로 로드한다. 호출자의 현재 폴더·PATH나 `LOAD_WITH_ALTERED_SEARCH_PATH`에 의존하지 않는다.
- 로딩은 추출 호출 안에서 수행하며 DllMain을 사용하지 않는다. 비표준 C++ 예외를 처리하는 동안에도 외부 DLL을 유지해 예외 정보가 사라지지 않게 한다.
- 실제 사용한 특징점·384차원 표현·방향장 셀만 저장한다. 551,512바이트 고정 구조체 두 개를 매 비교마다 복사하지 않는다. 숫자는 손실 없이 저장한다.
- 주변 배치의 반복 방향 가중치를 재사용하고, 점 사이 거리·각도를 비교 호출 안에서 캐시한다. 위치에서 탈락한 후보의 방향 계산을 생략한다. 일반 범위의 각도 정규화도 같은 결과를 내는 빠른 경로를 사용한다.
- 임계값·정렬 가설 수·반복 횟수·그래프 거리 기준은 줄이지 않았다. 가까운 이웃 그래프로 바꾸는 실험은 포함하지 않았다.
- 연구용 export·미사용 매처 분기를 제거하고 직접 사용하는 헤더를 명시했다.

FNV 체크섬은 형식·손상 검사용이며 암호화가 아니다. `KISA_Compare`에는 길이 인자가 없으므로 잘못된 포인터의 모든 객체 경계를 검증할 수는 없다. 헤더·계산된 길이·페이지 접근성·체크섬을 검사하되, 접근 위반 등의 Windows 충돌 격리는 평가 Worker의 역할이다. `catch (...)`가 모든 하드웨어 오류를 막는다고 주장하지 않는다.

## 오류 코드

| 코드 | 의미 |
|---|---|
| 100 | null 인자 |
| 101 | 지원하지 않는 iType |
| 102 / 103 | 읽을 수 없는 영상 / 쓸 수 없는 출력 버퍼 |
| 110 / 111 | FRFXLL 로드 실패 / 필요한 export 없음 |
| 120 / 121 / 122 | 추출기 오류 / 특징점 부족 / 잘못된 추출 결과 |
| 200 / 201 / 202 | 템플릿 값·길이 오류 / 구형·미지원 형식 / 체크섬 오류 |
| 900 / 901 / 999 | 메모리 부족 / 그 밖의 표준 C++ 예외 / 비표준 C++ 예외 |

세부 라이브러리 상태는 메시지에도 기록한다. 제공한 추출기의 내부 특징점 한도는 255지만, 256개 초과를 무조건 실패시키던 별도 조건은 제거했다. 비정상적인 개수에 대한 메모리 상한은 유지한다.

## 빌드와 검증

저장소 루트에서 Visual Studio 2022 C++ 도구와 CMake를 사용한다. MFC와 Python은 알고리즘 실행에 필요하지 않다.

```powershell
cmake -S algorithm-v1/submission -B algorithm-v1/submission/build-x64 -A x64
cmake --build algorithm-v1/submission/build-x64 --config Release --parallel 2
ctest --test-dir algorithm-v1/submission/build-x64 -C Release --output-on-failure
python algorithm-v1/submission/tests/verify_runtime.py --build algorithm-v1/submission/build-x64/Release

cmake -S algorithm-v1/submission -B algorithm-v1/submission/build-win32 -A Win32
cmake --build algorithm-v1/submission/build-win32 --config Release --parallel 2
ctest --test-dir algorithm-v1/submission/build-win32 -C Release --output-on-failure
python algorithm-v1/submission/tests/verify_runtime.py --build algorithm-v1/submission/build-win32/Release
```

호출 예제는 일반 `LoadLibraryW`를 사용한다.

```powershell
./algorithm-v1/submission/build-x64/Release/fp_compare.exe ./algorithm-v1/submission/bin/x64/fingerprint_algorithm_eval.dll 2 enroll.raw attempt.raw
```

`fp_core_tests`는 원본 V1과 표현·방향장·점수를 대조하고, 가변 저장 복원·체크섬·가드 페이지를 검사한다. `verify_runtime.py`는 비관련 CWD/PATH, 한글·공백 경로, 세 export, 의존 파일 누락, 비표준 예외 주입을 새 프로세스에서 확인한다. 테스트용 throwing_extractor.dll은 배포 패키지에 포함하지 않는다.

## 설치 ZIP

```powershell
python algorithm-v1/submission/tools/package.py
```

생성 위치는 `algorithm-v1/submission/dist/v1-submission-20261123.zip`이다. ZIP을 푼 후 기본 사용자 폴더에 설치하거나 원하는 경로를 지정한다.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File ./install.ps1
# 또는
powershell -NoProfile -ExecutionPolicy Bypass -File ./install.ps1 -Destination "D:/FingerprintV1"
```

설치 경로의 `uninstall.ps1`로 제거한다. 설치 기록에 없는 파일과 사용자가 수정한 파일은 보존한다. 전체 팀원 GUI의 설치 프로그램과는 별개인 **알고리즘 모듈 설치 패키지**다. 실제 바이너리·명령줄 예제·라이선스·대응 소스를 포함하며, 시험용 지문이나 템플릿은 포함하지 않는다.

## 성능·허용 범위

2026-11-23, 같은 PC에서 확인한 결과다.

| 검증 | 결과 |
|---|---:|
| 18개 조건, 추출 / 비교 | 1,620 / 14,220회, 실패 0 |
| 평균 EER | 기존과 동일한 3.019% |
| 원본 대비 최대 점수 차이 | 1.05×10⁻¹⁷ 미만 |
| 템플릿 평균 / 최대 | 109,946 / 208,296바이트 |
| 동일 432회 순차 비교 평균 | 16.743 → 7.487ms, 약 2.24배 |
| 전체 회귀 x64 최대 등록 / 인증 / 비교 | 22.460 / 24.934 / 86.240ms |
| 팀원 CLI·Worker x64 / Win32 최대 비교 | 85.328 / 193.447ms |

팀원 저장소 `635f88b`에서 3개 DB·양쪽 비트 수로 추출 540회, 비교 4,740회와 HTML·DOCX 결과 생성을 확인했다. MFC GUI 자체를 빌드하거나 직접 조작한 검증은 포함하지 않는다. 라이브러리 로딩·오류·비표준 C++ 예외 주입 검사도 x64·Win32에서 통과했다.

수치 검증과 측정 결과는 [validation.json](results/validation.json)에 기록한다. 관찰한 실행 시간은 해당 PC·비트 수·동시 작업 수에 한정되며 모든 PC나 입력에서 250ms를 보장하지 않는다. 개발 데이터의 인식 성능과 독립 인증 성능도 구분한다.

FingerJet 활용 범위는 과제의 공개 라이브러리 ‘단위 기능’ 허용 문구에 대한 교수자 판단이 필요한 부분으로 남아 있다. 이번 수정은 그 사용 방식을 바꾸거나 승인됐다고 가정하지 않는다.

FingerJetFX OSE는 별도 DLL로 유지하고 [공유 원본 소스](../../third_party/FingerJetFXOSE-master/), COPYRIGHT·LGPL·NOTICE와 대응 소스를 함께 제공한다. 알고리즘 런타임은 C++17/Windows이며 Python은 검증·패키징 도구에만 사용한다.
