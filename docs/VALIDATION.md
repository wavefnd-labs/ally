# Ally Studio 04 검증 기록

검증 날짜: 2026-10-10 (Asia/Seoul). Linux에서 수행한 결과이며 Windows/macOS 검증으로 확대하지 않습니다.

## 환경과 디자인 출처

Fedora Linux x86-64, Clang 21.1.8, GCC 15.3.1, CMake 3.31.11, Python 3.14.7, SDL3 3.2.24, Skia chrome/m124 고정 소스, libremidi 4.5.0을 사용했습니다. 물리 GPU는 NVIDIA GeForce RTX 2060이며 offscreen GL은 Mesa llvmpipe입니다. 네이티브 출력은 자체 ALSA backend의 48 kHz stereo float32 경로입니다.

사용자가 제공한 HTML의 CSS·레이아웃을 읽어 네이티브 UI로 옮겼습니다. HTML은 브라우저에서 실행하지 않았으며 원본 렌더와의 pixel diff는 수행하지 않았습니다. 아래 화면은 실제 SDL3·Skia GPU surface에서 얻었습니다. 기존 UI 캡처는 새 캡처로 교체했습니다.

- `images/workspace.png`: 1440×900 기본 Piano Roll, 실제 NVIDIA GL/ALSA, 정지 상태.
- `images/devices.png`: 같은 크기의 Devices 도크.
- `images/mixer.png`: 실제 재생 중 Mixer, 관측 출력 peak 0.212819. Texture 채널의 PCM 출력 미터도 확인했습니다.

## 수행 결과

| 검사 | 결과 |
|---|---|
| Release / Debug 앱 빌드 | 성공, 자체 소스 warning/error 없음 |
| Release / Debug CTest | 각각 4/4 통과 |
| ASan / UBSan / LeakSanitizer 코어 | 4/4 통과 (샌드박스 밖 실행) |
| 독립 libs/audio GCC 빌드 / CTest | 성공, 2/2 통과 |
| 독립 libs/media CTest | 1/1 통과 |
| 1440×900 / 870×650 GUI smoke | 두 크기에서 통과 |
| 실제 데스크톱 실행 | NVIDIA GL, ALSA, Piano Roll/Devices/Mixer 모두 실행·캡처 |
| 장치 없는 환경 | ALSA와 MIDI 오류를 보고하며 GUI 계속 실행 |
| WAV export / inspect | 48 kHz, 2 channels, 743226 frames (124 BPM / 8마디) |
| clang-format 검사 | 통과 |
| x.py package --release | ally-0.1.0-Linux.zip 생성 성공 |
| 압축 해제 패키지 | 디자인 원본/새 캡처 일치, executable mode, GUI smoke와 WAV export/inspect 통과 |

코어 검사는 문서 v4 roundtrip, v2/v3 migration, 자유 길이 clip 반복/offset/variation, 정렬된 이벤트, 용량/손상 문서 거절, 여러 BPM에서 clip 끝 sample 경계 일치, offline WAV와 MIDI message validation을 포함합니다. 오디오 검사는 sample 시작/끝, pause/rewind/seek/non-loop 종료, live MIDI와 UI audition 분리, track/master peak, PCM 반복과 stereo/master gain, cutoff 적용, metronome, queue 포화와 100000 SPSC 전송을 확인합니다. note/PCM/click/filter 처리 블록에서 operator new를 금지해 동적 할당이 없는지도 검사했습니다. 미디어 parser는 기존 prefix 절단·손상 청크·4000 malformed 입력 검사를 통과했습니다.

GUI smoke는 실제 hit target에서 노트 추가/이동/길이 변경/삭제와 velocity, clip 추가/이동/길이 변경/삭제, Undo/Redo, BPM 텍스트 입력, 검색 필터, 채널 추가, 채널/Master fader, cutoff, mute/solo, loop/metronome, snap, 도크 접기/브라우저 전환, 미지원 조작이 문서를 바꾸지 않는 것과 SDL Space transport를 확인합니다.

검증 중 기호 글꼴의 누락 glyph를 확인해 벡터 아이콘으로 교체했습니다. 장치가 없는 샌드박스에서 libremidi 기본 observer가 ALSA Raw fallback의 noexcept 열거 경로로 종료하는 문제를 발견했습니다. 플랫폼의 MIDI backend를 명시적으로 선택하도록 수정했고, 같은 환경에서 오류를 반환하고 GUI를 유지하는 것을 확인했습니다. 실제 MIDI 입력 포트가 없으므로 하드웨어 Note On/Off/hotplug는 검증하지 않았습니다.

## 재현

```bash
python3 x.py build --release
ctest --preset release
ctest --preset debug
ctest --preset asan
python3 x.py fmt --check
SDL_VIDEODRIVER=offscreen ./build/release/ally --smoke
SDL_VIDEODRIVER=offscreen ./build/release/ally --smoke --window-size 870 650
./build/release/ally --frames 12 --screenshot /tmp/ally.bmp
./build/release/ally --view devices --frames 12 --screenshot /tmp/devices.bmp
./build/release/ally --view mixer --play --frames 210 --screenshot /tmp/mixer.bmp
./build/release/ally --render /tmp/ally.wav
./build/release/ally --inspect /tmp/ally.wav
```

## 현재 한계

Windows/macOS 실행·네이티브 출력, 실제 MIDI 장치, 장시간 부하/xrun/latency·physical loopback, 다중 모니터 DPI와 다른 시스템 글꼴의 동일한 metrics는 검증하지 않았습니다. MIDI는 block 경계 적용이고 plan 교체 시 보이스를 초기화합니다. 최대 8채널·4-beat source pattern·32-beat Arrangement·512 prepared notes·32 내장 PCM events의 제한이 있습니다. RF64/AIFF, 임의 audio import/streaming, 녹음, Session/Automation, FX/plugin, bus graph, 여러 독립 패턴, 범용 docking은 미구현입니다. 저장/내보내기 고정 경로와 UI 스레드 I/O, Windows 기존 파일 교체는 다음 단계입니다. 프로젝트 배포 라이선스는 미정입니다.

## OS 제목 표시줄 복구

앱 내부 창 버튼과 borderless/caption 구현을 제거했습니다. Linux libdecor 개발 파일이 없는 환경에서 X11/XWayland의 OS 창 장식을 사용합니다. 실제 NVIDIA GL/ALSA 실행에서 SDL_GetWindowBordersSize가 native title bar 37px를 반환했습니다. OS close-request 이벤트의 종료 경로와 최소 870×650 GUI smoke를 통과했습니다. 창 장식은 Skia framebuffer 밖에 있으므로 기존 내용 영역 PNG에는 OS 제목 표시줄이 포함되지 않습니다.

X11 검증에서 발견한 Skia GL 함수 연결의 실패도 수정했습니다. GLX resolver가 EGL 함수 이름에 잘못된 dispatch stub을 반환할 수 있어 GL entry point만 공급합니다. 수정 후 X11과 Wayland의 실제 GPU 실행을 확인했습니다. OS 버튼의 배치는 시스템 설정에 따르며 전역 OS 설정은 변경하지 않습니다.
