# 독립 오디오 엔진

`Ally::Audio`는 C++23 공개 API와 C ABI를 제공하며 Media/Project/UI에 의존하지 않습니다. 선택적 `Ally::Resampler`가 libsoxr 0.1.3의 자료형과 소유권을 숨깁니다.

## 실시간 경계

문서 owner가 검증한 Plan을 고정 SPSC에 전달합니다. 32 tracks / 4 buses / master, 4096 note / 128 audio event / 32 source를 준비합니다. process는 유일 output owner에서 호출하고 동적 할당, 파일 I/O, 블로킹 락, shared ownership 파괴를 하지 않습니다. PCM registry 256 MiB / stream lifetime 64개를 검사합니다. DSP·경로 정렬 ring은 생성 시 heap에 준비합니다. seek, rewind, plan 변경은 cursor와 유효 길이만 초기화하며 큰 메모리를 콜백에서 지우지 않습니다.

처리 순서는 instrument/audio clip/monitor → strip → gain → route/sends → 번호순 bus → master입니다. 버스는 더 높은 번호 또는 master로만 연결합니다. 범용 플러그인 graph compiler와 parallel scheduler는 아직 없습니다.

## 악기와 DSP

64 voices: sine/PolyBLEP saw/triangle synth, root key 기반 resident sampler, MIDI 36–51의 one-shot drum map. strip은 balance pan, 고정 250 Hz/4 kHz crossover 3-band EQ, stereo-linked peak compressor, feedback echo입니다. compressor attack/release는 고정값이고 `lookaheadFrames`는 실제 오디오를 지연시켜 미래 peak를 검출하는 옵션입니다. API 범위는 0–2048 samples; Devices UI는 48 kHz에서 0/2/5/10 ms를 선택합니다. echo의 시간은 예술적 효과이므로 처리 지연에 포함하지 않습니다. sidechain/reverb/LUFS/parametric EQ는 미구현입니다.

## 자동 경로 지연 보정

각 track의 처리 지연과 bus의 최장 입력 경로를 계산합니다. route와 send마다 짧은 경로에 차이만큼의 ring delay를 적용하고, bus 병렬 경로·직접 master 경로·metronome을 동일 샘플에 합칩니다. 동일 bus로 향하는 main route와 send는 한 번만 delay line을 진행합니다. master lookahead도 총 지연에 포함합니다. 현재 경로 보정 상한은 10240 samples, master 포함 총 상한은 12288 samples입니다. 정렬 storage는 약 15 MiB이며 길이에 따라 늘지 않습니다.

`planLatencyFrames`는 유효한 plan의 총 지연을 제공하고 잘못된 strip/route에는 0을 반환합니다. Engine은 실제 적용된 지연을 공개합니다. 재생 playhead는 이 지연을 반영합니다. offline export는 동일 Engine을 사용하고 총 지연만큼 뒤까지 처리한 뒤 앞의 padding을 제외하여 원래 시작 위치와 길이를 유지합니다. impulse 회귀가 track/direct/send/연쇄 bus/master의 합과 seek 이후 상태를 검사합니다. 플러그인 호스트가 없으므로 외부 CLAP/VST3 플러그인의 동적 지연 보정은 아직 검증 대상이 아닙니다.

## 스트리밍 HQ 변환

`StreamingResampler`는 worker용 상태형 stereo API입니다. process는 실제 consumed/produced frame 수를 반환합니다. 입력을 모두 소비한 뒤 별도 EOF drain을 반복하여 FIR tail을 얻으며 reset으로 독립 신호를 시작합니다. `bufferedFrames`는 내부 대기 길이이며 결과 WAV에 추가해야 하는 시작 offset이 아닙니다. 외부 soxr 타입은 공개 헤더에 없습니다.

DiskStream은 Media WavReader와 이를 조합합니다. 4096 frames × 512 slots = 16 MiB cache, atomic pin으로 RT reader와 disk worker를 분리합니다. 시작 256블록은 루프 복귀용으로 유지하고 나머지 256슬롯은 순환합니다. 256블록(약 21.8초)을 선행 요청하며 EOF 근처에서는 시작 구간까지 순환 준비합니다. 다른 rate는 libsoxr HQ로 변환합니다. 임의 탐색은 rational rate의 위상 주기에 맞춘 시작점과 양쪽 8192-output-frame warmup을 사용하며 요청 구간만 cache에 저장합니다. 파일 전체 PCM을 만들지 않습니다. 48 kHz는 변환 없이 읽습니다. slot 충돌·느린 디스크의 miss는 무음과 counter로 표시하며 먼 여러 offset을 동시에 읽는 scheduling 개선은 남아 있습니다. offline owner는 process 밖에서 요청 창 전체의 prefetch를 기다립니다.

## 녹음과 시간 정보

CaptureSink → 약 64 MiB 고정 queue → DAW disk worker 경계를 사용합니다. native callback은 파일을 열거나 메모리를 할당하지 않습니다. 정확한 누락 frame 수가 있으면 `gap(frames)`를 전달해 duration을 보존하고 무음을 삽입합니다. 길이가 불명확한 gap은 `gap(0)`으로 표시하고 녹음을 중단하여 partial 파일을 보존합니다. ALSA recover를 조용히 이어 붙이지 않습니다.

입력 장치를 먼저 준비하고 capture gate와 transport를 시작합니다. 첫 capture의 audio clock과 packet 길이, transport anchor로 take 시작점을 계산합니다. 사용자가 선택한 native input/output 추정 지연과 실제 graph 지연을 차감하고 추가 manual offset을 적용합니다. 음수 시작은 파일을 바꾸지 않고 clip source offset으로 잘라냅니다. Input popup은 Estimated/Manual only와 ±1 ms offset, 입력 추정/PDC/queue/lost를 제공합니다. v7 문서에 설정을 저장합니다.

이는 추정값 기반 보정이며 물리 loopback을 자동 측정한 결과가 아닙니다. OS timestamp 정렬, 실제 ADC/DAC 왕복 측정과 서로 다른 입력/출력 장치의 clock drift 보정은 남아 있습니다. MIDI는 transport와 독립적인 monotonic audio clock으로 block 안 sample에 예약하며 OS MIDI hardware timestamp 보정은 미구현입니다.

## 네이티브 I/O

Linux: ALSA output/capture worker, stop/drop/join. optional PipeWire discovery와 ALSA PipeWire PCM. capture latency는 실제 설정된 buffer 크기 기반 추정입니다. direct pw_stream/JACK/ASIO는 미구현입니다.

선택적 `ALLY_AUDIO_RTKIT`는 libsystemd >=246이 있으면 input/output worker 시작 시에만 sd-bus로 RealtimeKit을 호출합니다. `process`와 capture 처리 중에는 IPC하지 않습니다. 서비스가 허용하는 범위에서 최대 priority 5의 RT 정책을 요청하고 실패하면 nice 우선순위를 요청합니다. D-Bus method timeout은 각 0.5초입니다. 서비스와 프로세스의 `RLIMIT_RTTIME`을 따르며 50ms 미만의 제한이면 RT 승격을 시도하지 않습니다. 시작 시 제한을 낮추는 것은 프로세스 전체에 영향을 줍니다. `BackendStats::realtimePriority`는 실제 SCHED_RR/FIFO 우선순위를 보고하며 0은 RT 미적용/미보고입니다. 서비스·권한이 없으면 정상 정책으로 남으며 성공으로 표시하지 않습니다. 이 선택 의존성의 자료형은 공개 API에 노출하지 않습니다.

Windows: WASAPI shared/event-driven worker. GetBuffer device position의 전진 차이를 gap으로 전달하며 역행 또는 길이가 불명확한 discontinuity를 중단합니다. [Microsoft의 GetBuffer 계약](https://learn.microsoft.com/en-us/windows/win32/api/audioclient/nf-audioclient-iaudiocaptureclient-getbuffer)을 따릅니다.

macOS: HAL AudioUnit input/output, 미리 준비된 callback storage. 유효 sample timestamp의 gap을 전달합니다. input hardware는 현재 48 kHz여야 합니다.

BackendStats는 inputLatencyMs와 outputLatencyMs를 구분합니다. 실제 Linux 장치 실행과 Windows/macOS native SDK CI를 구분하며, 수치와 한계는 VALIDATION.md에 기록합니다.
