# 기술 스택

## 확정

| 영역 | 확정 기술 | 첫 구현 상태 |
|---|---|---|
| 언어 | C++23 / C / 필요시 x86-64·AArch64 Assembly | C++23 모듈, C ABI 및 C 소비자 테스트; Assembly 없음 |
| 빌드 | CMake / Python x.py | 프리셋, 빌드/실행/테스트/포맷/정리/CPack |
| 창·입력 | SDL3 | 3.2.24 고정 소스 빌드 |
| 렌더링 | Skia | chrome/m124 고정 커밋, Ganesh OpenGL |
| UI | 자체 구현 | 사용자 Studio 04 HTML 명세, 네이티브 직접 그리기/hit-test/편집 |
| Audio / Media | 자체 엔진·프레임워크 | 독립 타깃과 실행 가능한 처리 경로 |
| FFT / SIMD / resampling | PFFFT / Highway / libsoxr | libsoxr 0.1.3 WAV 준비·상태형 HQ streaming·seek cache에 사용; PFFFT/Highway 직접 연동은 다음 단계 |
| MIDI 장치 | libremidi | 4.5.0 실제 소스 연결, 선택 포트 Note On/Off, 선택 채널 라우팅, MIDI take |
| MIDI 이벤트 / sequencer | 자체 구현 | channel message validation, beat→sample plan, monotonic timestamp/sample scheduling |

Qt/egui/JUCE/Sleepy/FFmpeg를 애플리케이션 의존성으로 사용하지 않습니다. Skia 구성에서도 ffmpeg 기본 비활성화를 유지합니다. SDL의 내부 GPU 모듈이 빌드되어도 UI는 SDL_GL API와 Skia Ganesh GL만 사용합니다.

## 아직 결정하지 않은 사항

프로젝트 오픈소스 라이선스, 플러그인 격리 전략, 프로젝트 장기 컨테이너, 실시간 graph worker scheduling, GPU의 플랫폼별 장기 Metal/Vulkan 전략, 백엔드 우선순위와 배포 패키지 정책은 미정입니다. 임시 텍스트 문서 형식, GL 3.3 desktop baseline과 ALSA 첫 출력은 이번 구현 결정이며 확정 스택을 대체하지 않습니다.

지원 대상은 Linux·Windows·macOS 데스크톱으로 한정합니다. 사용자 요청에 따라 모바일(Android·iOS/iPadOS), Web/WASM, BSD는 개발 범위에서 제외합니다.
