# Ally 제작 기능 검증 기록

검증 날짜: 2026-10-11 (Asia/Seoul). Linux 실행과 native SDK CI를 구분합니다. 전체 전문 DAW 완성이나 장시간 안정성 검증을 의미하지 않습니다.

## 환경과 디자인

Fedora Linux x86-64, Clang 21.1.8, GCC 15.3.1, CMake 3.31.11, Python 3.14.7, SDL3 3.2.24, Skia chrome/m124 고정 소스, libremidi 4.5.0, libsoxr 0.1.3. 실제 GPU는 NVIDIA GeForce RTX 2060입니다. 자체 ALSA input/output의 48 kHz float 경로를 사용했습니다.

사용자 제공 Studio 04 HTML은 수정하지 않았습니다. SHA-256은 `d9a81429bbb5c18dcee5d0cb71c6957241c65cbad2e862cf53858d169236d664`입니다. HTML은 시각적 명세이며 앱은 SDL3/Skia 네이티브입니다. 원본 HTML과 pixel diff는 수행하지 않았습니다. palette·기본 panel 배치/치수를 유지하며 기존 control에 실제 기능을 연결했습니다. `images/workspace.png`, `devices.png`, `mixer.png`는 이번 Release 앱의 실제 GPU surface 캡처입니다.

네이티브 X11/XWayland에서 SDL_GetWindowBordersSize가 OS title bar 37px를 반환했습니다. 앱 내부 minimize/maximize/close 버튼 및 borderless/caption은 없습니다. OS close-request 경로를 GUI smoke로 확인했습니다. GPU framebuffer 캡처는 OS 제목 표시줄을 포함하지 않습니다.

## Linux 최종 결과

| 검사 | 결과 |
|---|---|
| Debug / Release 전체 빌드 | 성공, 자체 소스 warning/error 없음 |
| Debug CTest | 8/8 통과, 11.29초 |
| Release CTest | 8/8 통과, 0.96초 |
| ASan / UBSan / LeakSanitizer | 8/8 통과, 36.35초, 샌드박스 밖 실행 |
| 독립 Audio GCC Release / CTest | 성공, 4/4 통과 |
| 독립 Media GCC Release / CTest | 성공, 1/1 통과 |
| Release GUI smoke 1440×900 / 870×650 | 실제 NVIDIA GL, 두 크기 모두 통과 |
| 실제 native audio record→WAV→track clip | 29,440프레임, 녹음 큐 손실 0, 48 kHz stereo WAV 검사 통과 |
| 녹음 probe 중 출력 | 85,248프레임 처리, buffer 256, xrun 0, 최대 process 0.217461 ms |
| Mixer 실제 재생 | peak 0.265195, 173,056프레임 처리, xrun 0, 최대 process 0.313373 ms |
| OS 기본 창 | X11 title bar 37px, close-request smoke 통과 |
| clang-format / git diff whitespace | 통과 |

위 최대 처리 시간은 해당 짧은 실행에서 관측한 값이며 최악 부하 보증이 아닙니다. output latency 21.3333 ms는 backend 추정값으로, physical loopback으로 측정한 round-trip latency가 아닙니다. 입력 장치는 `pipewire:NODE=alsa_input.pci-0000_0b_00.3.analog-stereo`이며 실제 PipeWire Audio/Source discovery와 ALSA PipeWire PCM으로 열었습니다. 연결된 외부 마이크의 음질이나 audible loopback은 검증하지 않았습니다.

## 검사한 동작

- Editing: 노트/클립 다중 선택, 그룹 clamp/이동/resize, clipboard·duplicate·삭제, cross-track 패턴 복제, 한 번의 Undo, timeline 확장·줌/스크롤, 16트랙 문서 검사.
- Instruments/MIDI: instrument/drum map·FX v6 저장/복원, sampler octave 재생, drum one-shot, 블록 안 128번째 샘플 MIDI 시작, clock 증가, 4-beat 경계 MIDI take 분할.
- Audio/Media: 약 45.8초 WAV를 작은 block으로 생성·읽기, stream cache 경계 4095/4096, 전체 PCM 없이 소스 준비/문서 복원·trim export, 손상 파일 거절. capture→fixed SPSC→동시 left/right/stereo 3개 WAV 내용·길이 검사.
- DSP: pan/bus gain/send의 정확한 샘플 값, low EQ·compressor 감쇠, 80샘플 delay 위치, seek/rewind tail 초기화, cyclic bus route 거절.
- RT: Engine::process 기존 heap 금지 검사와 실제 DiskStream::read/Recorder::capture에 operator new를 금지한 검사를 통과. 디스크 worker의 비실시간 할당은 허용합니다.
- 기존 회귀: pattern 독립성·문서 이전 버전 변환, WAV sample-rate 변환·source offset/loop/relink/누락, atomic 파일 교체·실패 후 원본 보존, recovery/lease, MIDI 파서와 SPSC, C ABI, malformed RIFF 입력.
- GUI: 실제 hit target으로 기존 편집/파일 worker 외에도 그룹 selection/clipboard·+8 bars·악기 전환·EQ/bus delay·Arm·MIDI take를 검사. 파일 선택 결과와 MIDI 이벤트는 fixture로 주입하며 OS 파일 선택기의 사람 조작이나 실제 MIDI 장치 검증으로 해석하지 않습니다.

## 플랫폼·패키지 검증

이전 native SDK CI는 [이 실행](https://github.com/wavefnd-labs/ally/actions/runs/38108195208)에서 Linux/Windows/macOS 코어 6/6·독립 Audio 3/3·Media 1/1 및 sanitizer 6/6을 통과했습니다. 이번 capture·production 경로의 CI 결과는 아래 최종 기록에 별도로 추가합니다.

WASAPI capture를 포함한 MinGW 독립 오디오 크로스 빌드/링크는 통과했습니다. Windows 실행, Apple HAL 실제 하드웨어와 SDL3/Skia 전체 GUI는 해당 OS에서 아직 검증하지 않았습니다. CI의 native backend 검사는 실제 SDK 빌드와 장치 없는 invalid-config/stop 계약을 검사하며 physical I/O를 대신하지 않습니다.

libsoxr 0.1.3 generic C dispatch의 Clang function-pointer 타입 검사만 해당 타깃에서 제외했습니다. ASan/나머지 UBSan 및 Ally 코드는 유지합니다. 샌드박스 ptrace 제한 때문에 LeakSanitizer는 제한 밖에서 실행했습니다.

## 발견한 문제와 한계

- 기본 Linux capture PCM이 ENOENT로 열리지 않는 환경에서 실제 PipeWire source를 열거해 명시적으로 선택하도록 연결했습니다. 직접 PipeWire/JACK callback backend는 아직 없습니다.
- stream worker가 이미 준비된 슬롯을 반복해서 writing으로 전환하여 RT miss를 만들던 문제를 수정했습니다. seek·동시 먼 offset·디스크 지연에서 miss가 날 수 있으며 miss 프레임 수를 표시하고 무음으로 처리합니다.
- 긴 WAV sample-rate 변환은 선형 보간입니다. libsoxr continuous HQ streaming·진행/취소·정밀 캐시 scheduling은 다음 작업입니다.
- 녹음 queue overflow는 길이를 보존한 무음으로 채우고 lost 수를 보고합니다. native capture discontinuity의 정확한 손실 길이·input/output drift·round-trip 보정은 미구현입니다. partial WAV는 1초마다 header를 갱신하며 자동 recovery UI는 없습니다.
- `/dev/snd/seq` 및 MIDI 입력 포트가 없어 실제 MIDI 장비·hotplug는 검증하지 못했습니다. OS MIDI timestamp 보정과 MPE/UMP/MIDI 2.0/CC automation도 다음 단계입니다.
- macOS capture 장치는 현재 48 kHz로 구성해야 합니다. 다른 sample rate에서는 명확한 오류를 반환합니다.
- 기본 DSP는 tone EQ/peak compressor/delay입니다. parametric EQ·sidechain·reverb·generic node graph·latency compensation·plan/voice crossfade·플러그인·RF64/AIFF·clip launcher·time-stretch는 미구현입니다.
- Windows/macOS 전체 GUI와 실제 input/output, 장시간 부하, 다중 모니터 DPI·다른 글꼴 metrics·OS dialog 사람 조작은 별도 검증 대상입니다. 라이선스는 미정이며 모바일/Web/BSD는 제외합니다.

## 재현

```bash
python3 x.py build
python3 x.py build --release
ctest --preset debug
ctest --preset release
cmake --preset asan
cmake --build --preset asan
ctest --preset asan
python3 x.py fmt --check
./build/release/ally --smoke
./build/release/ally --smoke --window-size 870 650
./build/release/ally --view mixer --play --frames 210 --screenshot /tmp/mixer.bmp
./build/release/ally --record-probe /tmp/take.wav \
  --input-device 'pipewire:NODE=your-input-node' --frames 90
./build/release/ally --inspect /tmp/take-track1.wav
python3 x.py package --release
```

녹음 probe는 실제 입력 장치를 열고 지정한 경로에 WAV를 만듭니다. 해당 파일은 덮어쓰지 않으며 재실행에는 새로운 경로가 필요합니다.
