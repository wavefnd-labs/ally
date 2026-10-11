# 녹음·32트랙 검증과 사용 조건

## 판정

실제 6시간 연속 녹음과 32트랙 30분 검사는 **미완료**입니다. 2026-10-11 20:52:59 KST경 컴퓨터를 밤새 켜두지 않는 사용자 조건에 따라 자체 검사 프로세스만 종료했습니다. 예약 추적은 PAUSED이며 검사 절전 방지 잠금도 해제했습니다. 자동으로 다시 시작하지 않습니다. 부분 녹음과 로그를 보존했습니다.

| 실행 | 마지막 관측 | 관측 카운터 | 판정 |
|---|---|---|---|
| `six-hour-20261011-final` | capture 관측 wall 1,530초, 73,298,432 frames | lost/input XRUN/output XRUN/stream miss 모두 0 | 중단. 6시간·종료 후 전체 PCM 검사를 완료하지 않음 |
| `load-32-final-20261011-attempt4` | capture 관측 wall 390초, 18,673,152 frames | lost/input XRUN/output XRUN/stream miss 모두 0; input/output RT priority 5, period 256 | 중단. 30분·종료 후 전체 PCM 검사를 완료하지 않음 |

첫 실행은 20:27:24 KST 시작, RealtimeKit 추가 이전 일반 스케줄링의 바이너리입니다. 두 번째 실행은 RealtimeKit 적용 이후 바이너리입니다. 관측한 구간과 해당 바이너리 구성을 구분합니다. supervisor의 `exit_code=-15`, `state=failed`는 강제 중단 기록이며 같은 디렉터리 `stop-reason.txt`에 사용자 사용 조건을 명시했습니다. 이 두 실행을 내부 오류 재현 또는 성공 판정으로 해석하지 않습니다.

원본 HTML과 OS 기본 창은 변경하지 않았습니다. 원본 및 저장소 HTML의 SHA-256은 `d9a81429bbb5c18dcee5d0cb71c6957241c65cbad2e862cf53858d169236d664`입니다.

## 실제 구성

3트랙은 실제 stereo 입력을 stereo/left/right 파일로 기록하고 native output와 PDC를 함께 처리합니다. 32트랙은 실제 **2채널 입력을 32개 stereo writer로 분배**하며 32채널 하드웨어 입력 검사가 아닙니다. 별도 60초/44.1 kHz WAV 32개, 독립 reader·HQ cache·worker 32개, EQ/컴프레서/딜레이/센드/연쇄 버스/PDC를 함께 재생합니다. 소스마다 캐시보다 큰 약 21 MB 파일을 사용합니다. OS 파일 캐시는 비활성화하지 않습니다.

완전한 6시간 기록 목표는 48 kHz의 1,036,800,000 capture frames입니다. stereo 약 8.29 GB와 mono 두 개 각각 약 4.15 GB를 씁니다. 32트랙 30분 목표는 약 22.12 GB입니다. 이번 중단 실행은 이 목표에 도달하지 않았습니다.

가속 stereo SSD 검사 완료: **1,036,800,000 frames**, **8,294,400,080 bytes**의 실제 RF64 파일을 기록하고 **모든 1,036,800,000 frames**를 예상 PCM과 비교했습니다. 기록·전체 읽기를 합친 실제 wall 시간은 **116.25초**, lost **0**, RF64 승격 및 6시간 길이 제한 통과입니다. 실제 6시간 driver/하드웨어 안정성 검증이 아닙니다. 성공한 인공 SSD 파일은 전체 비교와 SHA-256 기록 후 SSD 여유 공간을 복원하기 위해 삭제했습니다. 실장치/중단 파일은 보존했습니다.

원시 짧은 실행 로그·카운터·중단 사유·가속 결과/해시는 [증거 디렉터리](validation/2026-10-11/)에 기록합니다. 이번 수정의 GitHub CI는 로컬 검사와 구분하며 push 후 결과를 별도로 확인해야 합니다.

## 재현한 문제와 수정

1. `load-preflight-20261011-2010`: 90초 요청, 실제 약 91초. loss/input/output XRUN은 0이었지만 첫 60초 루프 이후 stream miss **7,391**건으로 실패했습니다. 모든 PCM과 경로 비교는 완료했으나 누락 조건을 통과하지 못했습니다. EOF에서 준비가 끝나는 문제를 고쳐 루프 시작 전용 슬롯과 순환 prefetch를 추가했습니다.
2. `load-32-final-20261011`: 빌드/sanitizer 동시 부하에서 stream miss **16,896**건을 재현하고 중단했습니다. 짧은 선행 캐시를 확대했습니다.
3. `load-32-final-20261011-attempt2`: 동시 디스크 부하에서 lost **426,752 frames**, stream miss **1,218,044**건을 재현하고 중단했습니다. 녹음 queue는 1024→8192 슬롯, 약 **64 MiB / 43.7초 backlog**로 확대했습니다. DiskStream은 **512슬롯 / 16 MiB**, 시작 256블록과 순환 256슬롯을 분리하고 **256블록 / 약 21.8초**를 선행 준비합니다. 메모리 비용이 증가했으며 32 stream 캐시는 약 512 MiB입니다.
4. `load-32-final-20261011-attempt3`: 위 버퍼 변경 뒤 loss/stream miss는 0이었지만 일반 스케줄링과 동시 부하에서 **output XRUN 1**을 재현했습니다. 선택적 RealtimeKit 연동을 추가하여 input/output worker 시작 시 RT 우선순위를 요청합니다. callback 중 IPC하지 않으며 실제 적용 priority를 보고합니다. 승격 실패·낮은 RTTIME 제한이면 일반/nice 정책으로 남습니다.

각 실패 파일과 로그를 보존했습니다. `six-hour-20261011-2010`은 수정 전 기준 실행 약 16분에서 최종 큐 버전 검사를 위해 중단했으며 이 또한 6시간 완료가 아닙니다.

## 완료된 짧은 검사

RealtimeKit 최초 native probe `rt-priority-probe-20261011`: 요청 10초, 실제 wall 11.0001초, capture **527,104 frames**, 3개 파일 전체 읽기 **1,581,312 frames**. lost/input/output XRUN/stream miss 0, input/output RT priority 5, period 256, process 최대 .820303 ms로 통과했습니다.

최종 Release 전체 CTest **11/11 (3.08초)**. RealtimeKit 버전 Debug **11/11 (26.05초)**, ASan/UBSan/LeakSanitizer **11/11 (88.07초)**, 독립 Audio/HQ **5/5**, 설치 SDK 소비자 검사를 통과했습니다. tiny RTTIME 보호 후 Release **11/11 (3.02초)**, Debug **11/11 (22.24초)**, ASan/UBSan/LeakSanitizer **11/11 (76.50초)**, 독립 Audio/HQ **5/5 (.36초)** 및 installed SDK 소비자도 통과했습니다. 실제 GPU smoke는 X11/NVIDIA, OS 기본 title bar 37 px에서 통과했습니다. 별도 자체 프로세스에서 일반 RTTIME 조건은 실제 RT priority 5, RTTIME=0은 priority 0으로 안전한 미승격을 확인했습니다. 가속 데이터 전용 CLI는 별도 실행 결과로 구분합니다. Cache loop 회귀는 캐시보다 큰 44.1 kHz 소스와 513블록/48 kHz tail→head 슬롯 충돌을 검사합니다. 비실시간 전체 HQ 준비가 sanitizer에서 5초를 초과해 실패했던 제한은 30초로 조정하여 전체 재검사를 통과했습니다. RT callback은 준비를 기다리지 않습니다.

8트랙 가속 과부하에서는 2,560,000 capture frames 중 371,968 lost frames가 모든 파일에서 정확한 무음으로 보존되는 것을 검사했습니다. 이는 손실 0 검사와 구분합니다.

최종 짧은 32트랙 native 검사 `short-32-complete-20261011`: 요청 90초, 실제 wall **91.0001초**, capture **4,364,032 frames**, 전체 readback **139,649,024 frames**. lost/input/output XRUN/stream miss 모두 **0**, input/output 실제 RT priority **5**, period **256**, queue peak **408,064 frames**, process 최대 **2.93682 ms**로 통과했습니다. native source 준비 및 종료 후 전체 readback 시간을 capture wall 시간과 구분합니다.

## 가속 데이터 검사와 실시간 검사의 차이

`stability_tests --accelerated`는 인공 stereo PCM의 6시간 분량을 실제 Recorder→worker→WAV/RF64 경로로 기록하고 stereo 파일의 모든 PCM을 예상값과 비교합니다. sparse 파일을 사용하지 않습니다. 첫 HDD 3파일 가속 시도는 오디오 길이에 따른 수많은 durable checkpoint로 인해 짧은 검증 목적에 맞지 않아 중단했고 부분 파일과 사유를 보존했습니다. 짧은 stereo 데이터 재검사는 SSD에서 수행하므로 HDD 장시간 안정성 근거로 사용하지 않습니다. 생산자는 테스트 전용 backpressure로 worker를 기다리므로 실제 hardware callback deadline·6시간 드라이버 동작·장시간 온도/메모리 변화를 검증하지 않습니다. 실제 6시간 연속 녹음 검증으로 보고하지 않습니다. 별도 `result.txt`에는 `accelerated_only=1`과 실제 wall 시간을 기록합니다.

## 재현

```sh
python3 x.py build --release
# 짧은 실제 장치 검사. source 준비와 전체 readback 시간이 추가됩니다.
python3 tools/soak.py --seconds 90 --tracks 32 \
    --device 'input-device-id' --directory build/soak/new-short-32
# stereo 약 8.29 GB를 실제 기록·읽는 가속 데이터 검사
./build/release/stability_tests --accelerated ./build/soak
```

입력 ID는 실제 장치에 맞게 바꿉니다. 충분한 실제 여유 공간이 없으면 시작을 거절합니다. `soak.py` 결과 디렉터리는 새 경로여야 하며 기존 파일을 덮어쓰지 않습니다. native 실행은 지정한 초만큼 실제 capture frames를 기다립니다. 21600초를 지정하면 실제 6시간이 필요합니다. 현재 컴퓨터에서 자동으로 예약하거나 절전 차단과 함께 시작하지 않습니다.

Native 최종 성공 판정은 요청 capture 길이, 모든 파일의 메타데이터·전체 PCM 경로 비교, lost/input/output XRUN/stream miss 0을 모두 요구합니다. process 최대 wall 시간은 별도 관측값이며 XRUN 0이 모든 callback deadline을 보장하지 않습니다. supervisor는 바이너리 SHA-256, source patch 및 이후 도구 버전에서는 untracked source snapshot, PID/UTC/RSS/여유 공간/종료 코드를 기록합니다.

## 미검증

실제 6시간 연속 녹음, 32트랙 30분 완주, Windows/macOS 실장치, 32채널 하드웨어 입력, 물리 loopback, 정전·디스크 공간 소진 및 장기간 clock drift는 미검증입니다. 짧은/가속 검사나 기존 CI를 이 범위의 통과로 사용하지 않습니다.
