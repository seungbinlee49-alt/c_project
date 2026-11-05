# 데이터 준비와 첫 실행

수업에서 받은 자료의 데이터 사용 조건에 따라 다음 세 DB를 로컬에 준비한다.
이 Git 레포는 수업 PDF·ZIP·지문 DB를 배포하지 않는다.

```text
data/
  FVC2000_DB3_B/  101_1.tif ... 110_8.tif
  FVC2002_DB3_B/  101_1.tif ... 110_8.tif
  FVC2004_DB1_B/  101_1.tif ... 110_8.tif
```

| iType | 폴더 | 크기 |
|---|---|---|
| 1 | FVC2000_DB3_B | 448 × 478 |
| 2 | FVC2002_DB3_B | 300 × 300 |
| 3 | FVC2004_DB1_B | 640 × 480 |

과제 PDF의 타입 `1, 2, 2`는 교수님 설명에 따라 `1, 2, 3`으로 해석한다.
이름은 `지문ID_촬영순번` 형식이다. TIFF/BMP/PNG와 해당 크기의 8비트 RAW를 지원한다.

루트 폴더에서 실행한다. 출력 목록·폴더는 기존 파일을 덮어쓰지 않는다.

```powershell
New-Item -ItemType Directory -Force experiments | Out-Null
./build/Release/fp_plan.exe ./data/FVC2000_DB3_B 1 ./experiments/my-plan.tsv 20261103 first all all
./build/Release/fp_bench.exe run ./experiments/my-plan.tsv ./build/Release/fingerprint_graph.dll ./experiments/my-run 5000 4 clean 20261103
```

10개 지문 × 8장 DB에서 위 계획은 본인 70쌍 + 타인 720쌍을 만든다.
등록은 첫 이미지, 본인·타인 비교는 모두 선택한다. 자기 자신과의 비교는 제외한다.
잡음 시험에는 `clean` 대신 `gaussian15`, `gaussian30`, `impulse2`, `blur1`, `brightness`를 쓴다.
버전 간 비교 시 계획·데이터·잡음 이름·잡음 시드를 동일하게 맞춘다.

```powershell
# 등록 이미지 2장, 각 등록당 본인 3장, 다른 지문마다 타인 1장: 비복원 추출
./build/Release/fp_plan.exe ./data/FVC2000_DB3_B 1 ./experiments/random-plan.tsv 42 random:2 random:3 random:1
# 지정된 촬영 순번을 사용
./build/Release/fp_plan.exe ./data/FVC2000_DB3_B 1 ./experiments/selected-plan.tsv 42 indices:1,3 all first
```

지문 데이터가 없어도 빌드, CTest, `tests/planner_test.py`, 공개 결과 무결성 검사는 실행할 수 있다.
`smoke.ps1`에는 DB별 ZIP이 있는 폴더를 `-DataRoot`로 지정한다.
`batch.ps1`·`acceptance.ps1`은 실제 DB가 필요하며, 노출 감사는 별도 로컬 inventory도 필요하다.
레포에 포함된 결과는 이미 본 개발 데이터의 측정치이므로 새 검증 데이터로 취급하지 않는다.
