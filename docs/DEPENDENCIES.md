# 의존성 재현과 upstream 근거

| 의존성 | 고정 버전/소스 | 통합 방식 |
|---|---|---|
| SDL3 | release-3.2.24 | CMake static target SDL3::SDL3 |
| Skia | 03c4671cffded59a3bb00e18bfae3c2763424fc6 (chrome/m124) | upstream GN build, CMake imported Skia::Skia |
| libremidi | v4.5.0 | CMake target libremidi::libremidi, native MIDI backend |
| Linux I/O/fonts/GL | system ALSA, FreeType, fontconfig, OpenGL | system package discovery |

SDL archive SHA256: `66c374ee7f786abcd8592d18fc5b18b1e3f1c67e923867eee5f9c503cb253afd`.
libremidi archive SHA256: `2e884a4c826dd87157ee4fab8cd8c7b9dbbc1ddb804cb10ef0852094200724db`.
CMake FetchContent verifies these archives. Skia checkout is checked against the recorded commit by x.py. GN binary comes from Skia's pinned upstream fetch-gn script. No floating master dependency is added.

Skia is intentionally fixed to the version whose headers and link were compiled here. Newer Skia headers move some Ganesh paths; don't mix newer includes with this library. Source API verification includes GrGLAssembleInterface.h/GrBackendSurface.h, actual font manager exports, SDL3 bool-returning APIs and libremidi observer/input configuration.

- [SDL3 GL context contract](https://wiki.libsdl.org/SDL3/SDL_GL_CreateContext)
- [Skia official GN build instructions](https://skia.org/docs/user/build/)
- [Skia pinned source](https://skia.googlesource.com/skia/+/03c4671cffded59a3bb00e18bfae3c2763424fc6/)
- [libremidi v4.5.0](https://github.com/celtera/libremidi/tree/v4.5.0)

SDL uses zlib license, Skia BSD license and libremidi BSD-2-Clause; the exact upstream LICENSE/COPYING files remain in downloaded sources. System dependencies and Skia bundled skcms notices also require preservation during distribution. The project's own license is undecided. Local CPack output is a development artifact for this host, not a fully portable or publicly licensed product installer.

## WAV 가져오기와 네이티브 출력 추가

libsoxr는 [공식 0.1.3 소스](https://github.com/chirlu/soxr/tree/0.1.3)를 사용하며 archive SHA-256은 `db6ca1b1e8405c6ef92f8294fc123d910abf0a114003b3f0f13fa57a95fd62d0`입니다. CMake FetchContent 또는 x.py deps로 같은 버전을 준비합니다. 외부 타입을 숨긴 Ally::Resampler에서 stereo float32 고품질 변환을 수행합니다. scalar CR32/CR64 구성, OpenMP/LSR/AVFFT 비활성화이며 FFmpeg 의존성을 추가하지 않습니다. LGPL-2.1-or-later 원문은 패키지의 share/ally/licenses/libsoxr.txt에 포함합니다. 정적 연결 배포 시 재링크 및 소스 제공 의무를 충족하는 배포 정책은 프로젝트 라이선스 결정과 함께 다룹니다.

libsoxr 0.1.3의 CMake module 경로·이전 link-interface 속성을 통합 계층에서 보정합니다. upstream 파일은 수정하지 않습니다. Clang sanitizer의 함수 포인터 타입 검사는 upstream generic C dispatch에서 오류를 보고하므로 libsoxr 타깃에만 `-fno-sanitize=function`을 적용합니다. ASan 및 나머지 UBSan과 Ally 코드는 그대로 검사하며 이 제외 범위를 검증 문서에 기록합니다.

[SDL3 파일 대화상자](https://wiki.libsdl.org/SDL3/SDL_ShowOpenFileDialog), [WASAPI Initialize](https://learn.microsoft.com/en-us/windows/win32/api/audioclient/nf-audioclient-iaudioclient-initialize), [Core Audio HAL output](https://developer.apple.com/documentation/audiotoolbox/kaudiounitsubtype_haloutput)을 참조했습니다. native SDK 이외의 오디오 I/O 라이브러리를 사용하지 않습니다.

Linux PipeWire SDK는 선택적 native input discovery에만 사용합니다. pkg-config의 libpipewire-0.3 >= 0.3.44가 있으면 registry query를 빌드하고 설치한 AllyAudio config에서도 해당 시스템 dependency를 찾습니다. 없으면 ALSA hint input enumeration을 유지합니다. audio는 Media에 의존하지 않습니다. capture 구현은 [Microsoft GetBuffer](https://learn.microsoft.com/en-us/windows/win32/api/audioclient/nf-audioclient-iaudiocaptureclient-getbuffer), [Apple HAL input TN2091](https://developer-mdn.apple.com/library/archive/technotes/tn2091/_index.html) 및 실제 SDK 헤더를 기준으로 구성했습니다.
