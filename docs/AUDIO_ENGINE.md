# 독립 오디오 엔진

`Ally::Audio`는 C++23, stereo float32 interleaved 블록 처리, 64 고정 보이스의 최소 자체 신스입니다. sample rate 8–384 kHz를 검증하며 앱에서는 48 kHz를 사용합니다. 외부 DSP 타입은 API에 노출되지 않습니다.

## 공개 API

`Engine::submit(Plan)`은 최대 512개의 정렬된 sample-start/duration note와 8채널 gain/cutoff/envelope와 Master gain을 검증해 3개까지 대기하는 SPSC 큐에 복사합니다. `play`, `rewind`, `seek`, `looping`, `metronome`, `position`, `peak`는 lock-free atomic으로 통신합니다. `process(span<float>)`는 audio owner 한 곳에서만 호출합니다. `midi`는 별도 bounded SPSC 입력입니다. C ABI는 create/destroy/play/note/process를 제공합니다. 생성 단계 실패는 nullptr, MIDI enqueue 실패는 false/0입니다.

처리 도중 heap 할당, lock, 파일 접근은 없습니다. queue copy, bounded binary search, 64 voices의 합성과 1-pole cutoff/envelope/gain/pan/clamp를 수행합니다. 생성 단계에서 검증·복사한 immutable stereo PCM을 최대 32개 sample event가 반복 재생합니다. sample start/end/seek는 샘플 단위입니다. 파일 접근이나 PCM 교체는 process에서 수행하지 않습니다. offline 렌더러도 같은 process 코드를 실행합니다. attack/release envelope는 최소 클릭 억제이며 보이스 stealing/plan 교체는 아직 완전한 무클릭 전환을 보장하지 않습니다.

## 타임라인

Pattern 모드는 한 마디 4 beat, Song 모드는 8마디 32 beat로 컴파일됩니다. 자유 위치·길이 MIDI 클립의 시작 beat를 원본 패턴 이벤트에 더해 반복 확장합니다. 마지막 반복은 clip 길이에 맞춰 잘라냅니다. Variation 신스 노트는 한 옥타브 높습니다. 두 모드에서 최대 512 이벤트를 넘지 않도록 문서 검증을 수행합니다. beat는 60/BPM × sampleRate로 변환합니다. note/PCM 시작·끝 beat를 각각 가장 가까운 sample로 반올림하고 그 차이를 duration으로 사용합니다. 길이를 독립적으로 반올림해 끝 경계를 한 sample 넘는 것을 방지합니다. 블록 내부에서도 시작/끝 위치를 sample마다 처리합니다. loop를 켜면 loopEndFrame(기본 24 beat)에서 0으로 돌아오며 loop를 끄면 32 beat 끝에서 정지합니다. offline export는 loop를 끄고 전체 범위를 렌더링합니다. metronome은 준비한 beat sample 위치에서 짧은 click을 생성하며 export에는 포함하지 않습니다. tempo editing으로 plan을 교체하면 보이스가 초기화됩니다. 재생 중 편집의 crossfade는 다음 단계입니다.

라이브 MIDI는 callback에서 받은 이벤트를 다음 process 블록 시작에 적용합니다. 자체 Message의 sampleTime은 현재 transport sample 위치를 저장하는 기본 모델이며 하드웨어 timestamp→audio clock 동기화/블록 내부 offset 스케줄링은 미구현입니다. 최대 10초 보호 지속 시간이 있으며 overflow는 adapter에서 계수하고 아직 UI에 표시하지 않습니다.

## I/O

Linux backend는 ALSA API를 직접 호출합니다. 장치와 buffer는 시작 단계에 준비하며 별도 jthread가 process 이후 snd_pcm_writei를 실행합니다. ALSA default가 시스템 PipeWire로 라우팅될 수 있으나 자체 PipeWire backend를 구현한 것은 아닙니다. underrun recover는 I/O worker에서 실행합니다. 이는 하드 실시간 callback backend가 아니며 OS real-time priority/deadline과 xrun 미터는 다음 단계입니다.

Windows/macOS는 start에서 unsupported 오류를 제공합니다. WASAPI/ASIO/Core Audio/PipeWire/JACK은 계획입니다. 미지원/장치 접근 실패 시 UI에서 오류를 보고하고 offline preview로 작동합니다.

## 다음 확장

실시간 graph compiler/arena, multichannel planar buffers, mixer bus/sends, node latency compensation, streaming worker/ring buffer, recording writer, prepared DSP nodes, PFFFT FFT, Highway vector kernels, libsoxr resampling, plugin sandbox 및 공간 음향 순으로 확장합니다. C ABI의 v1 기능은 최소이며 graph ABI/호환 정책은 아직 확정하지 않았습니다.

건반 미리 듣기는 Engine::audition의 별도 UI SPSC queue(64 slots)를 사용합니다. 선택한 track의 자체 신스를 transport 정지 중에도 처리합니다. MIDI 입력과 미리 듣기의 Note Off 대상은 분리하며 process에서 할당이나 lock을 사용하지 않습니다. 현재 두 live 입력의 타이밍은 블록 경계 적용입니다.

믹서의 L/R peak는 각 샘플의 채널별 보이스 합산 및 master 출력에서 측정합니다. UI는 atomic snapshot만 읽습니다. 아직 peak hold/decay ballistics와 RMS/LUFS 계측은 없습니다.
