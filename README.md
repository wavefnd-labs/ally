# Ally — DAW 개발 기반

C++23으로 만드는 Linux·Windows·macOS 데스크톱 전용 전문 DAW의 첫 구현입니다. 프로젝트 이름은 **Ally**입니다. 프로젝트 오픈소스 라이선스는 미정입니다. 아직 전문 DAW의 전체 기능을 제공하는 제품은 아닙니다.

개발 저장소: [wavefnd-labs/ally](https://github.com/wavefnd-labs/ally). 저장소의 접근 공개 여부와 오픈소스 배포 라이선스는 별도로 관리합니다.

## 현재 동작하는 경로

사용자 제공 Studio 04 HTML을 시각적 명세로 사용한 SDL3·Skia 네이티브 작업 공간입니다. 이전 UI 디자인은 제거했습니다. [디자인 원본 사본](assets/design/ally-ui-studio-04.html)은 참고 자료이며 앱이 HTML/JavaScript를 실행하지 않습니다. 최소화·최대화·닫기는 OS 제목 표시줄을 사용합니다.

기본 화면은 HTML과 같은 5트랙·8마디 Arrangement 및 Piano Roll / Devices / Mixer 도크입니다. 실제 문서는 최대 32트랙·32,768마디로 확장하며, 트랙당 독립 4-beat 패턴 32개·클립 128개를 저장합니다. 노트·클립 다중 선택, 그룹 이동/크기 변경/삭제, 복사·붙여넣기·복제, 한 번의 Undo로 그룹 편집 복원, 시간축 확대/이동과 트랙 세로 스크롤을 지원합니다. 초기 화면의 색·치수·OS 창 장식은 유지했습니다.

Devices에는 채널별 Synth / Sampler / Drum machine 선택과 저장되는 파라미터를 연결했습니다. 신스는 sine·PolyBLEP saw·triangle, 샘플러는 root key 기준 피치 재생, 드럼 머신은 MIDI 36–51의 16개 키에 서로 다른 resident WAV 소스를 배정하는 one-shot입니다. 키보드 미리 듣기와 libremidi 입력을 선택 채널에 라우팅합니다. MIDI 입력 장치를 선택하고 Shift+R로 MIDI take를 녹음하면 4-beat 패턴과 Arrangement 클립으로 나눠 저장합니다.

Samples / Ctrl+I / WAV drop으로 mono/stereo PCM 8/16/24/32-bit 및 float32 WAV를 가져옵니다. 8–384 kHz 파일의 실제 파형, source offset·Loop·Relink를 제공합니다. 작은 소스는 libsoxr로 48 kHz PCM을 준비합니다. 48 kHz 환산 2,097,152프레임(약 43.7초)을 넘으면 전체 PCM 대신 2 MiB 고정 캐시와 디스크 worker로 스트리밍합니다. 다른 sample rate도 libsoxr HQ 스트리밍으로 변환하며 탐색에서는 위상 정렬과 FIR warmup으로 블록 경계를 유지합니다. 파일 해시와 파형 분석은 가져오기 worker가 순차 스캔합니다.

트랙 Arm → 상단 Record / R → OS 저장 위치 선택 → Stop으로 실제 네이티브 입력을 녹음합니다. 여러 armed 트랙을 동시에 stereo/left/right 입력으로 녹음하고 각 WAV를 해당 트랙의 클립으로 가져옵니다. `take.wav`를 선택하면 `take-track1.wav` 등으로 만들며 기존 파일은 덮어쓰지 않습니다. Devices Monitor는 선택 트랙의 믹싱 경로로 입력을 보냅니다. 캡처 콜백은 고정 SPSC 큐만 쓰고 worker가 8192프레임 단위로 기록합니다. 1초 header checkpoint, 5초 disk sync, 4 GiB 초과 시 RF64 전환과 worker 오류 조기 중단을 지원합니다. 현재 DAW take 길이 상한은 6시간이고 기존 Arrangement 끝에 도달해도 녹음을 계속합니다. 녹음/파일 작업 중 문서 편집을 잠시 막습니다.

Mixer는 채널·4개 버스·Master, 팬·출력 경로·Bus 1 send·bus volume을 제공합니다. Devices의 3-band EQ, peak compressor, delay는 실제 DSP입니다. 룩어헤드 0/2/5/10 ms를 선택하면 track/send/bus/master 경로를 샘플 단위로 자동 정렬합니다. 버스는 이후 번호의 버스 또는 Master로만 연결해 순환을 거절합니다. 실시간 재생과 float32 WAV export는 같은 Engine::process 경로를 사용합니다. Export도 순차 writer를 사용하며 큰 PCM 벡터를 만들지 않고 처리 지연을 앞에서 제거하여 원래 시작/길이를 유지합니다.

한 재생 계획은 최대 4,096 note events·128 audio events·32 sources, 저장된 notes 합계 32,768개로 제한됩니다. resident PCM은 문서 128 MiB·엔진 lifetime 256 MiB, 엔진 stream lifetime 64개입니다. WAV/RF64 소스 길이는 6시간까지 검사합니다. RF64 ds64 table과 여러 oversized chunk는 미지원입니다. 노트는 MIDI 0–127, 템포는 20–300 BPM입니다. 기본 loop 1–6마디를 유지하며 비루프 재생과 export는 문서 전체 timeline 길이를 사용합니다.

문서 버전 7은 룩어헤드·녹음 자동 지연 추정 보정·추가 offset과 악기·드럼 키 배정·입력 arm/channel·timeline·믹서/버스/FX를 저장하고 버전 1–6을 변환합니다. 오디오 경로는 프로젝트 기준 상대 경로를 우선합니다. 누락/변경 파일은 클립을 유지한 채 오류와 무음으로 복원하며 해당 export는 실패합니다. 외부 WAV를 프로젝트 폴더에 자동 복사하지 않습니다.

Linux ALSA, Windows WASAPI shared, macOS Core Audio HAL의 출력·입력을 구현했습니다. Ctrl+,의 Input / MIDI에서 장치를 선택합니다. PipeWire 개발 SDK가 있으면 Linux 입력 목록에 실제 Audio/Source 노드가 추가되며 해당 장치는 ALSA PipeWire PCM을 통해 캡처합니다. 직접 PipeWire audio callback 구현은 아직 아닙니다. Linux 실제 입출력/녹음 경로를 검증했으며 Windows/macOS 전체 GUI·실장치 입출력은 별도 검증 대상입니다. macOS 캡처는 현재 장치를 Audio MIDI Setup에서 48 kHz로 구성해야 합니다.

![Ally Studio 04 실제 화면](docs/images/workspace.png)

[Devices](docs/images/devices.png) · [Mixer](docs/images/mixer.png)

## Linux 빌드

Python 3.12 이상(표준 라이브러리만 사용), CMake 3.25 이상, Ninja, Clang/Clang++ 16 이상, Git, pkg-config, ALSA/OpenGL/FreeType/fontconfig 개발 패키지가 필요합니다. SDL3 빌드에 필요한 X11/Wayland/EGL 개발 패키지는 사용 환경에 맞게 설치합니다. 네이티브 Wayland 제목 표시줄에는 libdecor 개발 파일도 필요합니다. 없으면 X11/XWayland를 우선해 OS 창 장식을 사용합니다. 첫 다운로드에는 인터넷이 필요하며 이후 로컬 의존성으로 빌드할 수 있습니다.

```bash
python3 x.py doctor
python3 x.py deps
python3 x.py build
python3 x.py run
python3 x.py test
python3 x.py fmt --check
python3 x.py clean
python3 x.py build --release
python3 x.py package --release
```

Skia 공식 소스의 고정 커밋과 upstream GN으로 라이브러리를 만들고, CMake 타깃에서 연결합니다. GN은 Skia의 자체 빌드 도구이며 DAW/오디오/미디어 빌드는 CMake가 담당합니다. 소스 및 라이브러리는 `third_party/`에 생성되고 제품 소스와 구분됩니다. libsoxr 0.1.3은 실제 파일 준비 경로에 연결합니다. PFFFT/Highway는 아직 사용하는 DSP가 없어 직접 연결하지 않습니다. libsoxr 구성은 외부 FFmpeg와 OpenMP를 사용하지 않습니다.

CMake 단독 실행도 가능합니다.

```bash
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
./build/debug/ally
```

의존성을 먼저 준비해야 합니다. 시스템 SDL3가 있더라도 초기 검증은 고정 소스 버전을 사용합니다. Windows/macOS에서는 CMake 단독 구성에 해당 플랫폼용 동일 Skia 소스/라이브러리를 지정합니다. Linux용 GN 자동 빌드와 Clang 프리셋은 Windows 빌드를 대신하지 않습니다.

```bash
cmake -S . -B build/native -G Ninja -DCMAKE_BUILD_TYPE=Debug \
    -DSKIA_ROOT=/path/to/skia -DSKIA_LIBRARY=/path/to/libskia.a
cmake --build build/native
```

Windows는 호환 MSVC/clang-cl ABI와 `.lib` 경로가 필요합니다. macOS는 CoreText font manager와 GL Ganesh가 포함되어야 합니다. 전체 GUI의 Windows/macOS 빌드·실행과 실장치 출력은 현재 Linux 환경에서 직접 검증하지 않았습니다. 네이티브 백엔드와 문서 코어는 GitHub Actions의 Linux/Windows/macOS 빌드·테스트를 통과했습니다. 오프라인 렌더링 코어는 플랫폼 UI에 의존하지 않습니다.

## 독립 모듈 빌드

```bash
cmake -S libs/audio -B build/audio -G Ninja
cmake --build build/audio
ctest --test-dir build/audio --output-on-failure
cmake -S libs/media -B build/media -G Ninja
cmake --build build/media
ctest --test-dir build/media --output-on-failure
```

오디오 모듈은 미디어/SDL/Skia/libremidi 없이 빌드됩니다. `-DALLY_AUDIO_RESAMPLER=ON`으로 선택적인 `Ally::Resampler`와 libsoxr를 추가할 수 있으며 기본 `Ally::Audio` 처리에는 필요하지 않습니다. ALSA 없는 코어 빌드는 `-DALLY_AUDIO_ALSA=OFF`로 구성하며 I/O 미지원 상태를 반환합니다. 미디어 모듈은 외부 의존성이 없습니다.

## 조작

- Space: 재생/일시정지, Escape: 정지 후 처음으로 이동. 타임라인 룰러 클릭: seek.
- 피아노 롤 클릭: snap 위치에 노트 추가. 드래그: 이동. 오른쪽 끝 드래그: 길이 변경. 오른쪽 클릭: 삭제. velocity 막대 클릭: 세기 변경. Q: quantize.
- 건반을 누르는 동안 선택 채널의 신스를 미리 듣습니다. Ctrl+휠: 음역 이동, 휠: 시간 확대, Shift+휠: 시간축 이동.
- 트랙 제목/브라우저 악기 클릭: 채널 선택. Arrangement 빈 곳 클릭: 4-beat 클립 추가. 클립 드래그/끝 드래그/오른쪽 클릭: 이동/길이 변경/삭제. 추가 버튼은 최대 32채널까지 지원합니다. 트랙 영역 휠: 세로 스크롤.
- F5: Piano Roll, F9: Mixer, F8: 브라우저 표시 전환. 도크 탭: Piano Roll / Devices / Mixer. 분할선 드래그 또는 접기 버튼으로 높이를 바꿉니다.
- Mixer: 채널·Master fader와 mute, 채널 solo, 실제 peak. 휠로 strip을 가로 스크롤합니다. Devices: 악기 제목으로 종류를 전환하고 각 노브를 수직으로 드래그합니다. 샘플러 Root / Attack / Release, EQ Low / Mid / High, compressor threshold / delay Wet / Time이 실제 처리에 연결됩니다.
- BPM 클릭 후 숫자 입력·Enter로 적용, Escape 취소. 브라우저 Search에 입력해 목록을 필터링합니다. Snap 1/16·1/8, +/- 확대, loop/metronome 버튼을 지원합니다.
- Shift+클릭: 노트/클립 선택 추가·해제, Shift+빈 곳 드래그: 선택 사각형. 선택 항목은 함께 이동·resize·삭제됩니다.
- Ctrl+A/C/X/V/D: 전체 선택/복사/잘라내기/붙여넣기/복제. 붙여넣기는 마지막 editor 클릭의 snap 위치를 사용합니다. 다른 트랙/문서로 MIDI 클립을 붙이면 패턴도 독립 복제되고 오디오 클립은 소스도 유지합니다. Delete: 선택 삭제. 방향키: 시간/음높이 이동, Alt+좌우: 길이 변경.
- Arrangement `+8 bars`: timeline 확장. 상단 +/-: Arrangement 확대·축소, Shift+휠: 해당 editor 가로 이동. Ctrl+N: 빈 프로젝트(미저장 변경 확인).
- 트랙의 Record 표시: Arm. R: armed 오디오 녹음 시작/종료. Shift+R: 선택 트랙 MIDI take 시작/종료. Stop: take 확정. 입력 장치를 준비한 뒤 transport를 시작하고 첫 callback/packet 및 input/output 추정 지연·PDC로 take 위치를 보정합니다. Ctrl+, → Input에서 Estimated/Manual only 및 ±1 ms 추가 offset을 선택합니다. 실제 loopback 자동 측정은 아직 없으며 추정 보정으로 표시합니다.
- Mixer Pan은 수직 드래그, Master/Bus 출력과 Send는 클릭해 변경합니다. 버스 strip은 가로 스크롤로 접근합니다.
- Ctrl+Z: Undo, Ctrl+Shift+Z 또는 Ctrl+Y: Redo. 최대 64개 문서 변경 이력.
- 도크의 패턴 이름 클릭: 독립 패턴 선택·New·Duplicate·Rename·선택한 MIDI 클립에 Assign. 해당 패턴을 참조하는 클립끼리는 편집을 공유하며 다른 패턴에는 영향을 주지 않습니다.
- Samples/Ctrl+I: 선택한 트랙 마지막 클립 뒤에 WAV 가져오기. WAV·Ally 프로젝트 파일을 창에 드롭할 수도 있습니다.
- Ctrl+S/Save: 현재 프로젝트 저장; 처음에는 OS 파일 선택기. Ctrl+Shift+S: 다른 이름으로 저장. Ctrl+O/Projects: OS 파일 선택기로 열기. Ctrl+E/Export: 파일 선택 후 48 kHz stereo float32 export.
- Ctrl+, 또는 상태 바의 오디오 상태 클릭: 장치·buffer 설정과 진단. buffer 변경은 백엔드를 정지·join한 다음 다시 시작합니다.

파일 가져오기·열기·저장·export는 별도 작업 스레드에서 수행합니다. 작업 중 문서 입력을 잠시 막으며 실패 시 원래 문서를 보존합니다. 동일 디렉터리의 임시 파일을 flush한 뒤 POSIX rename 또는 Windows MoveFileExW로 기존 파일을 교체합니다. 변경 표시와 종료 시 Save/Discard/Cancel을 제공합니다.

변경 문서는 30초 간격으로 SDL 사용자 데이터 경로의 `recovery/`에 저장합니다. 인스턴스마다 별도 파일 및 OS 파일 잠금을 사용해 다른 실행 중인 세션과 충돌하지 않습니다. 다음 실행 때 종료된 세션의 복구본을 Recover/Discard/Cancel로 선택하며 복구본은 새 미저장 문서로 열어 원본을 자동 덮어쓰지 않습니다. `--no-recovery`는 복구 스캔과 자동 저장을 비활성화하며 `--smoke`/`--frames` 검증 실행에는 복구 파일을 만들지 않습니다.

```bash
./build/debug/ally --render /tmp/pattern.wav
./build/debug/ally --inspect /tmp/pattern.wav
./build/debug/ally --project session.ally --render /tmp/session.wav
./build/debug/ally --list-devices
./build/debug/ally --device default --buffer 256 --play
./build/debug/ally --offline
# 실제 입력 장치가 필요하며 지정한 경로에 녹음 WAV를 생성합니다.
./build/debug/ally --record-probe /tmp/take.wav --input-device "input-device-id" --frames 90
# 기존 timeline 끝을 넘는 실제 GUI 녹음/가져오기 검사
./build/release/ally --record-probe /tmp/long-take.wav --input-device "input-device-id" --record-seconds 65
# 원본 partial을 유지하고 완전한 PCM frame을 새 파일로 복구
./build/release/ally --recover-recording /tmp/take.wav.partial.wav /tmp/recovered.wav
# 선택적 실제 장치 5분 soak 및 고의 큐 과부하/4 GiB 경계 검사
./build/release/stability_tests --native 300 "input-device-id" ./build/soak
./build/release/stability_tests --pressure
./build/release/libs/media/media_stream_tests --rf64-write
./build/debug/ally --view mixer
SDL_VIDEODRIVER=offscreen ./build/debug/ally --smoke --screenshot /tmp/workspace.bmp
```

`--offline`은 장치 없이 재생 위치를 시뮬레이션하며 소리가 나지 않습니다. GUI도 SDL3/Skia GL로 렌더링합니다. 오디오 장치 개방 실패 시 오류를 표시하고 이 모드로 작동합니다. `--smoke`는 실제 렌더된 hit target으로 노트·velocity·클립 편집, Undo/Redo, 채널·Master fader, cutoff, mute/solo, BPM 입력, 검색, 채널 추가, loop/metronome·독립 패턴 생성/복제/이름 변경/할당·WAV import/trim/loop·파일 worker save/open/export·오디오 설정과 SDL 키보드 재생을 검사합니다. 다중 선택·clipboard·timeline 확장·악기 전환·EQ/bus·Arm·MIDI take의 UI 경로도 검사합니다. GUI smoke의 파일 선택 결과는 테스트 경로로 주입하며 OS 파일 선택기의 사람 조작 검증으로 해석하지 않습니다. EGL/OpenGL 드라이버가 없는 환경에서는 실패합니다.

## 품질과 상태

`docs/VALIDATION.md`에 실제 검증 결과를 기록합니다. `.github/workflows/ci.yml`은 Linux/Windows/macOS 문서·네이티브 백엔드 코어와 독립 모듈을 빌드·테스트하고 Linux sanitizer를 검사합니다. GUI 및 실장치 검증을 대신하지 않습니다. ASan/UBSan은 `cmake --preset asan`으로 구성합니다. ptrace 제한 환경에서 LeakSanitizer가 실패하면 `ASAN_OPTIONS=detect_leaks=0 ctest --preset asan`을 사용하며 누수 검증으로 해석하지 않습니다.

AIFF, RF64 ds64 table, 플러그인, 자동화, Session/클립 런처, resonance/drive/reverb, 범용 도킹은 미구현입니다. UI에서 지원하지 않는 조작은 미지원 상태를 표시합니다. libremidi 입력 선택·Note On/Off 라우팅·MIDI take는 구현했으며, Launchpad Mk2/RC-505 등 실제 MIDI 기기는 검증하지 않았습니다.

기술 근거와 각 라이브러리 라이선스는 [의존성 문서](docs/DEPENDENCIES.md), 구조는 [아키텍처](docs/ARCHITECTURE.md)를 참고합니다. 배포 라이선스는 결정 전이며 로컬 패키지 생성은 공개 배포 승인이나 라이선스 확정을 의미하지 않습니다.

현재 환경에서 실제 NVIDIA RTX 2060 GL 렌더링과 ALSA 출력, Debug/Release·ASan/UBSan/LeakSanitizer 코어 검사를 통과했습니다. [화면 캡처](docs/images/workspace.png)는 실제 애플리케이션 GPU surface에서 추출한 이미지입니다.

## 이번 구현의 한계

EQ는 고정 250 Hz/4 kHz crossover의 3-band tone EQ이고 compressor는 고정 attack/release의 stereo-linked peak detector입니다. parametric EQ·LUFS·reverb·sidechain·플러그인·time-stretch·take comping·보이스/plan 교체 crossfade는 다음 단계입니다. MIDI timestamp는 monotonic audio clock을 기준으로 블록 안에서 스케줄링하지만 OS MIDI 하드웨어 clock 보정과 물리 왕복 지연 자동 측정은 아직 없습니다. 오디오 녹음은 추정 input/output 지연과 graph 지연 및 manual offset을 사용합니다. GUI 입력 채널은 stereo/left/right이며 public capture API는 최대 8채널을 받습니다. 입력과 출력이 서로 다른 장치일 때 clock drift 보정은 미구현입니다.

스트림 캐시를 준비하지 못한 RT 프레임은 무음으로 출력하고 설정 화면에 miss frame 수를 표시합니다. seek나 동시 다수의 먼 offset에서 읽을 때 캐시 miss가 날 수 있습니다. offline export는 별도 stream 캐시를 사용하고 process 바깥에서 prefetch를 기다려 재생 캐시와 경쟁하지 않습니다. recording disk queue overflow는 길이를 유지한 무음으로 채우며 lost frame 수를 보고합니다. WASAPI/Core Audio의 정확한 frame gap은 무음으로 보존하고 길이를 모르는 discontinuity는 중단·partial 보존합니다. ALSA XRUN은 누락 길이를 정확히 알 수 없어 take를 중단합니다.

녹음은 1초 header 갱신·5초 disk sync와 finish 동기화를 수행하고 `.partial.wav`를 남겨 writer 오류·비정상 종료 시 데이터를 보존합니다. 이 파일은 위 CLI로 복구할 수 있으며 자동 take 복구 UI는 아직 없습니다. 원본 스냅샷/파일은 오류 시 보존하며 스트림/PCM lifetime 예산 소진 시 재시작이 필요합니다. 검증 범위와 실제 결과는 [VALIDATION](docs/VALIDATION.md)에 기록합니다.
