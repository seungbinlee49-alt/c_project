# 로컬 실행 패키지

이 문서는 실제 제출용 인증서나 합격 확인서가 아니다. 외부 제출은 하지 않는다.

Windows x64 평가 프로그램과 Win32 호환 빌드, 각각의 특징점 추출 DLL, 직접 구현한 매칭 DLL 및 소스를 함께 제공한다.

- bin/x64/fp_gui.exe: Windows 평가 GUI. 빌드 완료, 실제 클릭 흐름 미검증.
- bin/x64/fp_bench.exe: 검증된 배치 엔진.
- bin/x64/fp_protocol.exe: 노출 데이터와 후보 검증 데이터의 중복 감사.
- bin/x64/fp_calibrate.exe: 개발 임계값 고정 및 고정 임계값 평가.
- bin/x64/fp_inspect.exe: 저장된 특징점 v2 시험의 대응점·회전·이동 진단. 같은 폴더의 inspect-assets를 유지한다.
- bin/win32/enroll.exe 및 match.exe: 공개 FVC FV 프로토콜의 로컬 호환 어댑터.
- fingerprint_minutiae.dll: KISA 인터페이스 및 자체 정합.
- FRFXLL.dll: 별도로 빌드한 특징점 추출 라이브러리. 같은 폴더에 둔다.

VC++ 런타임이 필요한 빌드이다. 현재 개발 PC에서 확인했으며, 개발 도구가 없는 깨끗한 PC에서의 검증은 남아 있다. Microsoft의 공식 런타임 안내는 https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist 이다. 운영체제 구성이나 런타임 설치를 이 프로젝트가 자동으로 변경하지 않는다.

## 빌드 및 검증

source/CMakeLists.txt가 있는 폴더에서 Visual Studio 2022와 CMake로 빌드한다.

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
cmake -S . -B build-win32 -A Win32
cmake --build build-win32 --config Release
```

데이터는 패키지에 포함하지 않는다. 프로젝트에 제공된 각 DB를 data/에 준비하면 tests/smoke.ps1, tests/batch.ps1과 tests/acceptance.ps1을 실행할 수 있다. 노출 목록은 개인정보가 포함된 로컬 경로를 보관하므로 실행 패키지에서 제외했다. acceptance.ps1의 노출 검사는 로컬 개발 폴더에서 사용한다.

## 외부 규격을 참고한 어댑터

```powershell
./enroll.exe image.tif image.bin enroll-log.txt
./match.exe first.bin second.bin match-log.txt
```

공개 규격대로 로그 한 행을 append한다. 실행 실패는 FAIL 행으로 남긴다. 입력 이미지는 500dpi, 8비트 회색조로 해석하며 지원되는 컨테이너에서 픽셀을 읽는다. 표준 문서에 나온 두 실행 파일 이름과 인자·점수 범위를 로컬에서 확인했지만 외부 서비스의 승인 또는 실제 호환성 시험을 받은 것은 아니다.

이 패키지는 설치 마법사가 있는 설치 프로그램이 아니다. 소스와 로컬 실행 파일을 검토하고 재현하기 위한 배포 후보이다.
