# 독립 오디오 엔진

`Ally::Audio`는 C++23 공개 API와 최소 C ABI를 제공하며 Media/Project/UI에 의존하지 않습니다. 선택적 `Ally::Resampler`가 libsoxr를 숨깁니다.

## 준비와 실시간 경계

UI/문서 owner는 Plan을 검증하고 SPSC로 전달합니다. 32 track/4 bus/master와 최대 4096 note/128 audio event/32 source가 고정 배열에 준비됩니다. plan 큐와 DSP delay 메모리는 생성 단계에 할당합니다. source PCM/stream의 공유 소유권은 엔진 lifetime 동안 비실시간 registry가 유지하고 process에는 span/raw pointer만 전달합니다. borrowed source와 미등록 stream은 submit에서 거절합니다.

Engine::process는 유일 output owner에서 호출하며 allocation, file I/O, blocking lock, 공유 소유권 파괴를 하지 않습니다. DSP는 track instrument/audio clip/monitor → channel strip → gain → route/sends → 번호순 bus → master strip/gain → stereo output 순서입니다. bus는 더 높은 번호 또는 master에만 연결해 cycle을 거절합니다. 1초 stereo delay arena는 모든 strip에 미리 준비됩니다. 현재 단일 오디오 owner이며 범용 node graph compiler/parallel scheduler/latency compensation은 아직 없습니다.

## 악기와 믹싱

64 voices: sine/PolyBLEP saw/triangle synth, root key에 따른 보간 sampler, MIDI 36–51의 16키 one-shot drum sample map. sampler/drum 악기는 resident PCM을 사용합니다. voice stealing과 plan 교체의 crossfade는 미구현입니다.

strip은 balance pan, 고정 250 Hz/4 kHz crossover 3-band EQ, stereo-linked peak compressor, 10–1000 ms feedback delay입니다. EQ gain/threshold는 plan 적용 때 계산하며 compressor attack/release는 고정값입니다. UI는 cutoff/attack/release 또는 sampler root, EQ 3개 band, compressor threshold, delay wet/time, output/send/pan에 연결합니다. reverb/sidechain/parametric EQ/oversampling/LUFS는 미구현입니다. 실시간과 offline export는 같은 process를 호출합니다.

## 타이밍

Pattern 4 beat, song 최대 1024 beat. clip 반복을 note event로 확장하고 시작·끝 beat를 가장 가까운 sample로 각각 반올림합니다. MIDI는 callback의 monotonic engine clock timestamp를 SPSC에 넣고 block 내부 해당 sample에서 소비합니다. transport seek/loop와 독립적인 audioClock이 MIDI take duration을 보존합니다. timestamp extrapolation은 steady_clock을 사용하며 최대 50 ms로 제한합니다. OS MIDI hardware clock alignment와 출력/입력 round-trip latency 보정은 아직 없습니다. live voice는 최대 30초 보호 duration입니다.

## 스트리밍과 입력

SampleStream public interface의 read는 준비 cache만 읽어야 합니다. DAW의 DiskStream은 Media WavReader를 worker에서 호출하며 4096프레임×64 slot=2 MiB cache를 사용합니다. slot state/pin 원자 연산으로 RT read와 worker write를 분리합니다. miss는 무음과 frame counter로 드러냅니다. PCM budget 256 MiB와 stream lifetime 64개를 검사합니다. 엔진 destruction 이전에 native output/capture owner를 멈춰야 합니다.

Capture는 별도 native API로 열거/start/stop하며 CaptureSink에 interleaved float를 보냅니다. native producer는 monitor queue와 Recorder fixed SPSC에만 전달합니다. 디스크 writer는 DAW 계층에 있고 엔진은 파일을 알지 못합니다. GUI는 stereo/left/right 분기, public capture는 최대 8채널을 지원합니다. multi-device clock drift 보정은 미구현입니다.

## 네이티브 backend

Linux: ALSA output write worker와 capture read worker. native XRUN recover와 stop/drop/join. optional PipeWire registry로 실제 input node를 열거하되 캡처는 ALSA PipeWire PCM을 사용합니다. 직접 pw_stream/ASIO/JACK backend는 다음 단계입니다.

Windows: WASAPI shared output/capture, COM 소유 worker, event-driven render buffer와 capture packet GetBuffer/ReleaseBuffer. capture discontinuity를 계수합니다.

macOS: HAL AudioUnit output render/input callback. input은 EnableIO bus 1, output disable bus 0, 미리 준비한 AudioBufferList storage로 AudioUnitRender를 수행합니다. 현재 input hardware는 48 kHz여야 하며 다른 rate는 명시적으로 거절합니다.

buffer·output latency는 backend의 추정/보고값이며 실제 round-trip 측정이 아닙니다. Windows/macOS의 native compile/core CI와 실장치 실행 검증을 구분합니다.

seek/rewind는 필터·compressor 상태와 delay 유효 길이를 초기화합니다. delay 메모리를 콜백에서 대량 초기화하지 않고 새로 기록한 길이까지만 읽습니다. MIDI용 clock/monotonic anchor는 version으로 일관되게 읽으며 callback 갱신과 겹치면 최대 세 번 시도한 후 published clock으로 돌아갑니다.
