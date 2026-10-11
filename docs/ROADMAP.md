# Ally 개발 단계

범위는 Linux·Windows·macOS 데스크톱입니다. Studio 04 HTML과 OS 기본 창을 유지합니다. 프로젝트 이름은 Ally이며 배포 라이선스는 미정입니다.

## 완료한 제작 기반

- C++23/CMake/x.py, 독립 Audio/Media와 C ABI, SDL3·Skia Ganesh GL 자체 UI, 통합 작업 공간.
- 독립 패턴, Arrangement/Piano Roll, 노트·클립 다중 선택/clipboard/group Undo, 32트랙·256마디 viewport, snap/zoom/scroll.
- 버전 6 문서와 이전 문서 변환, worker 저장/열기/export, OS 파일 선택기, 상대 소스 경로·누락/relink·자동 저장/복구.
- 채널별 synth/sampler/16-key drum machine, 저장되는 파라미터, libremidi 포트 선택·선택 채널 입력·sample timeline MIDI take.
- WAV PCM/float 디코딩·파형·source offset/loop, resident libsoxr HQ 변환, 긴 WAV의 고정 캐시/디스크 worker, 순차 float32 WAV export.
- ALSA/WASAPI/Core Audio native input/output 구현, 입력 arm/channel·monitor·동시 다중 트랙 녹음 writer와 실제 클립 가져오기.
- prepared track→bus→master 처리, pan/4 sends/forward bus routing, 기본 3-band EQ·peak compressor·delay. UI는 Bus 1 send를 노출하며 나머지 sends는 문서/API에서 지원합니다.

완료 표시는 해당 처리 경로가 구현되었다는 뜻입니다. Windows/macOS 실제 입출력·전체 GUI, 실제 MIDI 기기와 장시간 부하 검증은 별도이며, 검증 근거는 VALIDATION.md에 기록합니다.

## 다음 우선순위

1. 재생/녹음 안정성: continuous libsoxr HQ streaming, seek·다중 offset 캐시 scheduling, 장시간 xrun/underrun/CPU 부하 검사, writer 진행/취소·partial WAV 복구 UI.
2. 전문 편집: tempo/time-signature map, 가변 길이 패턴, loop range 편집, 오디오 fade/crossfade·time-stretch·take comping, Undo 메모리 예산과 command 기반 이력.
3. 오디오 정밀도: OS MIDI timestamp 보정, 실제 round-trip 측정/녹음 offset 보정, 입력/출력 clock drift, 무클릭 voice/plan 교체, callback 기반 Linux I/O.
4. 믹싱: parametric EQ·사용자 compressor timing·sidechain·reverb, parameter automation, 범용 node graph/buffer arena·지연 보정, bus meter와 전체 send UI.
5. 플러그인: CLAP ABI/scan/isolation부터 시작하고 VST3/LV2/Audio Units를 추가. 라이선스/배포 정책은 별도 결정.
6. Media: RF64/AIFF/WAVE extensible·channel mask·fuzz CI, FLAC/MP3/Opus/AAC. PFFFT/Highway는 실제 처리 경로가 생길 때 통합.
7. 라이브 퍼포먼스: clip launcher/quantized launch·live loop, MIDI Learn/MPE/UMP/MIDI 2.0·clock/time code, Launchpad Mk2/RC-505 실장치 연동, workspace 저장/docking.

Windows/macOS native SDK CI와 장치 없는 계약 검사는 계속 유지하고, 해당 OS 전체 SDL3/Skia GUI·실장치 검증을 추가합니다. 단계별 빌드·테스트·실행 증거를 남기며 전체 전문 DAW 완성으로 표현하지 않습니다.
