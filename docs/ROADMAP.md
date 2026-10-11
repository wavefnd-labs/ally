# 개발 단계

## 1. 이번 기반 구현

독립 Audio/Media CMake, C ABI, WAV 경계 검사, 프로젝트 버전형 저장, sample sequencer, minimal synth, Linux ALSA, SDL3/Skia GL, 통합 UI, piano/gain/mute/transport/export, 코어 및 실제 GUI smoke. 사용자 Studio 04 명세의 5트랙·8마디 Arrangement, 자유 위치/길이 클립, 최대 8채널, 내장 Texture PCM 재생/파형, Undo/Redo, quantize, 건반 미리 듣기, 채널·Master mixer, cutoff, BPM 입력·검색, loop/metronome과 Piano Roll/Devices/Mixer 도크를 구현했습니다. 미지원 FX·녹음·Session은 지원 상태를 표시합니다.

## 2. 편집과 문서 안정성 — 최우선

**구현 완료: 독립 패턴을 만들고 Arrangement 클립에서 선택하는 작곡 흐름**입니다. 트랙의 패턴을 안정적인 ID로 분리했으며, 클립은 패턴 ID를 참조합니다. 패턴 생성·복제·이름 변경·선택과 Piano Roll 편집을 연결하고, 기존 버전 4 문서의 클립을 같은 트랙 패턴으로 이관합니다. 서로 다른 패턴의 편집 독립성, 반복 클립 재생, Undo/Redo와 저장 후 복원을 완료 조건으로 둡니다. Studio 04 디자인과 OS 창 장식은 유지합니다.

**구현 완료: 기존 WAV 디코더와 libsoxr를 활용한 파일 가져오기 및 오디오 소스 관리**입니다. 파일 선택, 실제 PCM 파형·재생, 소스 경로 및 누락 파일 오류, 프로젝트 복원까지 하나의 경로로 검증합니다. cross-platform 저장 교체, OS 파일 선택기, 파일 worker와 인스턴스별 autosave/recovery도 구현했습니다.

다음 작업: 범용 Undo command/transaction, tempo/time signature map, 다중 selection/copy, worker export와 progress/cancel. UI 입력 regression과 document migration tests를 확장합니다.

## 3. 실시간 엔진과 네이티브 플랫폼

prepared node graph와 buffer arena, DSP node boundary, MIDI timestamp/audio clock 정렬, overflow/지연 정밀 측정, PipeWire/JACK, stream playback 및 multi-channel recording. 장치/buffer 선택·기본 xrun/latency 지표, WASAPI shared와 Core Audio HAL, 세 OS native core CI를 추가했습니다. ALSA blocking worker를 callback 기반 backend들과 나란히 유지합니다. Windows/macOS 실장치와 전체 GUI 실행 검증은 별도로 필요합니다.

## 4. Media와 믹싱

RF64/AIFF, WAVE extensible/channel mask, incremental decoding, fuzz CI, resampling/libsoxr, FFT/PFFFT, Highway scalar/vector conformance, bus/sends/sidechain 및 latency compensation, EQ/compressor/delay/reverb.

## 5. 악기 / 플러그인

synth와 sampler/drum machine, CLAP부터 plugin ABI/scan/isolation을 구축하고 VST3/LV2/Audio Units를 추가합니다. 라이선스/배포 정책은 별도 결정합니다.

## 6. 데스크톱 라이브 퍼포먼스

clip launcher/quantized launch, loop recording, MIDI routing/Learn/MPE/UMP/MIDI2, clock/time code, Launchpad Mk2/RC-505 실제 장치 검증, 도킹 workspace 저장과 데스크톱 키보드/마우스 작업 흐름을 확장합니다. 모바일·Web·BSD 포팅은 범위에서 제외합니다.

각 단계는 빌드 성공, 기능 검사, 실제 실행 증거와 미지원 플랫폼 상태를 분리해 기록합니다. 단계 1을 제품 전체 완성으로 표현하지 않습니다.
