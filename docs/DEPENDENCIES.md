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
