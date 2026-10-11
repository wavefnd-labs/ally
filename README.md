# Ally — DAW 개발 기반

C++23으로 만드는 Linux·Windows·macOS 데스크톱 전용 전문 DAW의 첫 구현입니다. 프로젝트 이름은 **Ally**입니다. 프로젝트 오픈소스 라이선스는 미정입니다. 아직 전문 DAW의 전체 기능을 제공하는 제품은 아닙니다.

개발 저장소: [wavefnd-labs/ally](https://github.com/wavefnd-labs/ally). 저장소의 접근 공개 여부와 오픈소스 배포 라이선스는 별도로 관리합니다.

## 현재 동작하는 경로

사용자 제공 Studio 04 HTML을 시각적 명세로 사용한 SDL3·Skia 네이티브 작업 공간입니다. 이전 UI 디자인은 제거했습니다. [디자인 원본 사본](assets/design/ally-ui-studio-04.html)은 참고 자료이며 앱이 HTML/JavaScript를 실행하지 않습니다. 최소화·최대화·닫기는 OS 제목 표시줄을 사용합니다.

기본 화면은 5트랙, 8마디 Arrangement, Piano Roll / Devices / Mixer 도크입니다. 트랙마다 독립적인 4-beat 패턴을 생성·복제·이름 변경하고 MIDI 클립에 연결합니다. 패턴 선택 메뉴는 기존 도크 제목 위치에 있습니다. 노트·velocity·클립 편집과 Undo/Redo가 연결되어 있으며 자체 64보이스 신스로 재생합니다.

Samples 또는 Ctrl+I로 mono/stereo WAV를 가져옵니다. PCM 8/16/24/32-bit 및 float32, 8–384 kHz 소스를 디코딩하고 libsoxr로 48 kHz stereo PCM을 준비해 실제 파형·재생에 연결합니다. 선택한 오디오 클립의 도크에서 시작 오프셋·Loop·Relink를 조작하고 Arrangement 오른쪽 끝으로 길이를 조절합니다. 내장 Texture도 별도 소스 ID를 사용합니다. 전체 파일을 메모리에 준비하는 방식이며 streaming/time-stretch는 미구현입니다.

최대 8트랙, 트랙당 32패턴·32클립, 재생 모드별 512 note events, 32 PCM 클립/소스입니다. 가져오기 파일 64 MiB, 준비한 PCM 소스 64 MiB·문서 합계 128 MiB 제한을 검사합니다. 엔진의 준비 버퍼는 소유권 안전을 위해 세션 종료까지 유지하며 합계 256 MiB를 넘으면 명확한 오류를 반환합니다. 노트는 MIDI 0–127, 템포는 20–300 BPM입니다. 기본 loop는 1–6마디이며 loop를 끄면 8마디 끝에서 정지합니다. Export는 8마디 전체를 렌더링합니다.

문서 버전 5는 패턴 ID·클립 참조·오디오 파일 경로 및 PCM fingerprint를 저장하며 버전 1–4를 변환합니다. 소스 경로는 프로젝트 기준 상대 경로를 우선합니다. 누락/변경 파일은 클립을 유지한 채 오류와 무음 상태로 복원하며 Relink로 다시 연결합니다. 해당 상태의 export는 실패를 보고합니다. 외부 오디오 파일을 프로젝트 폴더에 자동 복사하지는 않습니다.

Linux ALSA, Windows WASAPI shared, macOS Core Audio HAL 네이티브 출력이 구현되어 있습니다. 상태 바 또는 Ctrl+,에서 장치·요청 buffer 크기를 선택하고 실제 buffer/지연 추정치, xrun 지표와 처리 시간 최대값을 확인합니다. Linux 실제 출력과 Windows 크로스 빌드를 검증했으며 Windows/macOS 실장치 출력은 별도로 검증해야 합니다.

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
- 트랙 제목/브라우저 악기 클릭: 채널 선택. Arrangement 빈 곳 클릭: 4-beat 클립 추가. 클립 드래그/끝 드래그/오른쪽 클릭: 이동/길이 변경/삭제. 추가 버튼은 최대 8채널까지 지원합니다. 트랙 영역 휠: 세로 스크롤.
- F5: Piano Roll, F9: Mixer, F8: 브라우저 표시 전환. 도크 탭: Piano Roll / Devices / Mixer. 분할선 드래그 또는 접기 버튼으로 높이를 바꿉니다.
- Mixer: 채널·Master fader와 mute, 채널 solo, 실제 peak. 휠로 strip을 가로 스크롤합니다. Devices: Cutoff 노브를 수직으로 드래그합니다.
- BPM 클릭 후 숫자 입력·Enter로 적용, Escape 취소. 브라우저 Search에 입력해 목록을 필터링합니다. Snap 1/16·1/8, +/- 확대, loop/metronome 버튼을 지원합니다.
- Ctrl+Z: Undo, Ctrl+Shift+Z 또는 Ctrl+Y: Redo. 최대 64개 문서 변경 이력.
- 도크의 패턴 이름 클릭: 독립 패턴 선택·New·Duplicate·Rename·선택한 MIDI 클립에 Assign. 해당 패턴을 참조하는 클립끼리는 편집을 공유하며 다른 패턴에는 영향을 주지 않습니다.
- Samples/Ctrl+I: 선택한 트랙 마지막 클립 뒤에 WAV 가져오기. WAV·Ally 프로젝트 파일을 창에 드롭할 수도 있습니다.
- Ctrl+S/Save: 현재 프로젝트 저장; 처음에는 OS 파일 선택기. Ctrl+Shift+S: 다른 이름으로 저장. Ctrl+O/Projects: OS 파일 선택기로 열기. Ctrl+E/Export: 파일 선택 후 48 kHz stereo PCM16 export.
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
./build/debug/ally --view mixer
SDL_VIDEODRIVER=offscreen ./build/debug/ally --smoke --screenshot /tmp/workspace.bmp
```

`--offline`은 장치 없이 재생 위치를 시뮬레이션하며 소리가 나지 않습니다. GUI도 SDL3/Skia GL로 렌더링합니다. 오디오 장치 개방 실패 시 오류를 표시하고 이 모드로 작동합니다. `--smoke`는 실제 렌더된 hit target으로 노트·velocity·클립 편집, Undo/Redo, 채널·Master fader, cutoff, mute/solo, BPM 입력, 검색, 채널 추가, loop/metronome·독립 패턴 생성/복제/이름 변경/할당·WAV import/trim/loop·파일 worker save/open/export·오디오 설정과 SDL 키보드 재생을 검사합니다. GUI smoke의 파일 선택 결과는 테스트 경로로 주입하며 OS 파일 선택기의 사람 조작 검증으로 해석하지 않습니다. EGL/OpenGL 드라이버가 없는 환경에서는 실패합니다.

## 품질과 상태

`docs/VALIDATION.md`에 실제 검증 결과를 기록합니다. `.github/workflows/ci.yml`은 Linux/Windows/macOS 문서·네이티브 백엔드 코어와 독립 모듈을 빌드·테스트하고 Linux sanitizer를 검사합니다. GUI 및 실장치 검증을 대신하지 않습니다. ASan/UBSan은 `cmake --preset asan`으로 구성합니다. ptrace 제한 환경에서 LeakSanitizer가 실패하면 `ASAN_OPTIONS=detect_leaks=0 ctest --preset asan`을 사용하며 누수 검증으로 해석하지 않습니다.

RF64/AIFF, 스트리밍, 녹음, 플러그인, 자동화, Session/클립 런처, Res/Drive/EQ/Reverb, 범용 도킹은 미구현입니다. UI에서 지원하지 않는 조작은 미지원 상태를 표시합니다. libremidi는 첫 MIDI 입력 포트를 열어 Note On/Off를 연결하지만 MIDI 기록/샘플 정확한 라이브 타임스탬프 보정은 아직 없습니다. Launchpad Mk2/RC-505 실제 기기는 검증하지 않았습니다.

기술 근거와 각 라이브러리 라이선스는 [의존성 문서](docs/DEPENDENCIES.md), 구조는 [아키텍처](docs/ARCHITECTURE.md)를 참고합니다. 배포 라이선스는 결정 전이며 로컬 패키지 생성은 공개 배포 승인이나 라이선스 확정을 의미하지 않습니다.

현재 환경에서 실제 NVIDIA RTX 2060 GL 렌더링과 ALSA 출력, Debug/Release·ASan/UBSan/LeakSanitizer 코어 검사를 통과했습니다. [화면 캡처](docs/images/workspace.png)는 실제 애플리케이션 GPU surface에서 추출한 이미지입니다.
