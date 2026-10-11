# Ally 녹음·스트리밍·지연 보정 검증

2026-10-11 (Asia/Seoul). 이번 검사는 구현된 경로와 관측한 부하를 검증하며 수시간 무손실, 모든 장치/플러그인 또는 물리 왕복 지연의 정확도를 보증하지 않습니다.

## 환경과 디자인

Fedora Linux x86-64, Clang 21.1.8 / GCC 15.3.1, CMake 3.31.11, Python 3.14.7, SDL3 3.2.24, Skia m124 고정 소스, libremidi 4.5.0, libsoxr 0.1.3, NVIDIA RTX 2060. ALSA/PipeWire 입력 `pipewire:NODE=alsa_input.pci-0000_0b_00.3.analog-stereo`, ALSA default output, 48 kHz / 256 requested frames를 사용했습니다.

Studio 04 원본 HTML의 SHA-256은 `d9a81429bbb5c18dcee5d0cb71c6957241c65cbad2e862cf53858d169236d664`로 그대로입니다. 기본 palette·패널 배치·치수와 초기 5트랙/8마디를 유지합니다. SDL3/Skia 직접 그리기이며 HTML runtime과 자체 OS caption/control이 없습니다. 실제 X11 창은 OS title bar 37px를 반환했습니다. GPU surface 캡처에는 OS 장식이 포함되지 않습니다. HTML과 pixel diff는 수행하지 않았습니다.

## 로컬 빌드와 실행

| 검사 | 결과 |
|---|---|
| Debug / Release 전체 빌드 | 성공 |
| Debug / Release CTest | 각각 11/11 통과 |
| ASan / UBSan / LeakSanitizer | 11/11 통과, 누수 검사 포함 샌드박스 밖 실행 |
| 독립 Audio + HQ resampler, GCC Release | 5/5 통과, 로컬 고정 libsoxr 소스 사용 |
| 독립 Media, GCC Release | 2/2 통과 |
| 설치 SDK 외부 소비자 | Audio/Media/StreamingResampler/PDC 링크·실행 통과 |
| MinGW Windows 교차 빌드 | native WASAPI capture와 Audio 타깃 컴파일/링크 성공; 실행하지 않음 |
| 실제 NVIDIA GUI smoke | 1440×900 / 870×650에서 기존 편집·새 보정/룩어헤드·SDL Space·OS close-request 통과 |
| format / whitespace / doctor | 통과 |

libsoxr upstream의 C dispatch에는 기존 Clang function-type instrumentation 제외만 유지하고 다른 sanitizer와 Ally 소스의 검사는 활성화했습니다. 독립 resampler 구성은 CMake의 `FETCHCONTENT_SOURCE_DIR_SOXR_SOURCE`로 같은 공개 0.1.3 소스를 지정해 오프라인 검증했습니다.

## 실제 녹음과 고장 복구

- 실제 HDD(`/mnt/hdd/ally/build/soak`)에 stereo/left/right 3트랙을 두 번 각각 300초 연속 기록했습니다. 첫 run 14,399,232 frames, 두 번째 run 14,398,464 frames. 두 run 모두 queue lost 0 / output XRUN 0이며 세 파일의 길이와 마지막 frame 읽기가 일치했습니다. 자동 fixture는 검사 후 제거했습니다.
- 두 번째 run은 GUI·build/test 부하와 함께 진행했습니다. queue peak 25,088 frames; 최대 관측 output process wall time 6.19828 ms. 이는 256-frame period(5.333 ms)보다 큽니다. 해당 버퍼링에서 XRUN은 없었지만 모든 부하에서 deadline을 지킨다는 근거로 해석하지 않습니다. RT thread scheduling·callback Linux backend와 더 긴 부하 검사가 다음 단계입니다.
- GUI에서 65초를 지정한 native record/import probe: 3,167,488 capture frames, lost 0, queue peak 37,888. current 32-beat timeline을 넘겨 계속 기록하고 긴 stream WAV를 Arrangement에 가져왔습니다. 보정 시작은 -0.0882208 beat이며 음수 구간을 source offset으로 제외했습니다. output XRUN 0, 최대 process 1.48997 ms. 진단용 UI timer의 요청 시간과 정확한 PCM frame 길이를 구분합니다.
- 8트랙 가속 과부하: 각 2,560,000 frames 파일, 2,273,536 lost frames, queue peak 261,888. 모든 파일의 실제 무음 frame 수가 loss counter와 같았고 마지막 손실까지 길이를 보존했습니다. 이는 53.3초 상당의 입력을 빠르게 투입한 고의 과부하 검사이며 실시간 53.3초 녹음이 아닙니다.
- 새 native GUI 녹음 프로세스만 강제 종료한 뒤 CLI로 실제 partial을 복구했습니다. 49,152 complete frames를 읽고 원본 partial의 SHA-256 불변을 확인했습니다. 합성 fixture도 checkpoint 이후 PCM, incomplete final frame, 기존 목적지 거절을 검사합니다.
- RF64 전체 쓰기: sparse shortcut 없이 536,936,447 stereo frames / 4,295,491,656 bytes를 기록했습니다. RIFF→RF64 전환과 ds64 크기, 끝 sample .125 읽기가 통과했습니다. 이 경계 fixture는 `/tmp`의 tmpfs에 기록했으며 실제 녹음 soak는 HDD였습니다.

실제 입력 데이터와 모듈 경로를 검증했지만 외부 마이크 음질과 physical loopback은 측정하지 않았습니다. 전체 6시간 take/32트랙 hardware soak·정전·디스크 공간 소진 검사는 수행하지 않았습니다.

## HQ 변환과 지연 검사

- 상태형 StreamingResampler의 작은 input/output chunks, EOF drain, reset: 8/44.1/48/96/192/384 kHz→48 kHz에서 전체 libsoxr HQ 기준과 frame count·sample을 비교했습니다.
- 실제 DiskStream의 비순차 seek/cache boundary/EOF, 8/44.1/96/384/44.101 kHz. 44.101 kHz에서는 rational phase 주기 이후 먼 탐색도 검사했습니다. 전체 HQ 기준 대비 최대 sample 차이 4.54485e-7; 96 kHz source에는 32 kHz stopband tone을 사용했습니다.
- 연속 HQ의 96→48 kHz, 32 kHz tone interior RMS 2.82364e-8. stopband suppression을 실제 출력으로 검사했습니다.
- track/direct/send/연쇄 bus/master를 통과하는 impulse를 120번째 frame 하나로 합쳤으며 동일 bus route+send의 중복 delay 진행을 검사했습니다. seek/rewind 후 이전 delay tail이 사라지고 invalid latency를 거절합니다.
- offline export는 시작 padding을 제거하고 원래 첫 sample과 timeline frame count를 유지했습니다. 입력 packet/clock·추정 native input/output·PDC·manual offset의 위치 계산, v6→v7와 새 설정 저장/복원을 검사했습니다.
- Engine::process의 새 룩어헤드/PDC/seek 경로와 기존 DiskStream::read/Recorder::capture에서 operator new 금지 검사를 통과했습니다. worker 할당은 비실시간입니다.

## 데스크톱 CI와 패키지

현재 변경의 Linux/Windows/macOS native core 및 sanitizer CI 결과, 최종 ZIP 검증은 아래에 완료 후 기록합니다. 전체 Windows/macOS SDL3/Skia GUI·실장치 I/O 검증은 포함하지 않습니다.

## 남은 한계

물리 loopback 자동 측정, 장치별 calibration, 입력/출력 clock drift, OS MIDI timestamp 정렬, 외부 플러그인의 동적 latency는 남아 있습니다. 추정/수동 녹음 위치 보정과 내부 graph PDC의 범위를 구분합니다. plan 교체는 DSP history를 reset하며 무클릭 crossfade는 미구현입니다. 먼 여러 stream offset의 cache 충돌, 여러 트랙 finalize transaction과 자동 partial 복구 UI도 다음 단계입니다.
