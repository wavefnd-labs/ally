# Ally Studio 04 UI

화면의 시각적 기준은 사용자가 제공한 `ally-ui-studio-04.html`입니다. 원본 사본은 `assets/design/ally-ui-studio-04.html`에 보관하며 SHA-256은 `d9a81429bbb5c18dcee5d0cb71c6957241c65cbad2e862cf53858d169236d664`입니다. HTML/CSS를 디자인 명세로 읽고 SDL3·Skia 코드로 옮겼습니다. HTML이나 JavaScript를 애플리케이션에서 실행하지 않습니다.

## 디자인 수치

- 배경 #1e2022, 패널 #252729, 헤더 #292b2d, 경계 #3d4041, 보조 글자 #858a8b, 강조 #d8aa70.
- Inter 중심 11px 텍스트, 1px 경계, 사각 클립과 작은 조작 버튼. 아이콘은 자체 Skia 벡터입니다.
- 상단 58px, 브라우저 205px, 작업 툴바 33px, 타임라인 헤더 45px, 트랙 헤더 174px, 트랙 79px.
- 하단 도크 기본 310px, 탭 바 34px, 건반 56px, velocity 57px, 하단 상태 바 23px.
- 1100px 이하에서 브라우저 175px·트랙 헤더 148px. 최소 창 870×650 logical pixels.
- Analog Keys, Sub Bass, Drum Rack, Texture, Lead의 색과 초기 클립 위치·길이는 HTML과 같습니다.
- Piano Roll / Devices / Mixer 탭을 같은 도크에 표시합니다. 같은 3개 Devices 카드 구조에서 Instrument, EQ, Compressor/Delay의 실제 기능을 연결합니다. Mixer strip은 95px입니다.

기존 채널 랙 중심 화면과 스타일은 구현에서 제거했습니다. HTML의 임시 동작은 그대로 가져오지 않습니다. 피아노 건반의 누락된 C#4 행을 복구해 C5–C4의 13개 반음으로 구성합니다. 재생선·시간·velocity·파형·미터는 실제 모델/PCM/엔진 상태에서 계산합니다. Session·Automation·Reverb·사용자 Places·박자 변경·Master solo는 미지원이며 클릭/툴팁에서 이를 설명합니다. 카드의 미지원 상태도 표시합니다.

## OS 창

최소화·최대화/복원·닫기, 창 이동과 테두리 크기 조절은 OS 창 관리자가 담당합니다. SDL의 일반 bordered/resizable 창을 사용하며 앱에는 별도 창 제어 버튼, borderless 옵션, 자체 caption/hit-test를 두지 않습니다. OS의 제목 표시줄은 HTML 작업 공간 바깥에 표시됩니다. 네이티브 close-request 이벤트를 앱 종료와 장치 정리에 연결합니다.

Linux에서 libdecor 개발 파일이 없어 SDL의 Wayland 장식 지원을 빌드하지 못한 경우, DISPLAY가 제공되면 X11/XWayland를 우선해 OS 창 장식을 사용합니다. libdecor 개발 파일이 설치된 빌드에서는 SDL의 기본 Wayland 선택을 유지합니다. 사용자가 명시한 SDL_VIDEO_DRIVER/SDL_VIDEODRIVER 설정은 우선합니다. 현재 환경에서 X11 native title bar 37px를 확인했습니다. Windows/macOS는 기존 SDL 네이티브 창 경로를 유지하며 미검증입니다.

## 그래픽 연결

SDL3 OpenGL 3.3 core, stencil 8, double buffer, high-DPI 창을 사용합니다. SDL_GL_GetProcAddress의 GL 함수만 제공해 Skia GL interface를 만들고 Ganesh context를 생성합니다. 실제 framebuffer binding/sample/stencil을 조회해 GrBackendRenderTargets::MakeGL과 SkSurfaces::WrapBackendRenderTarget로 표면을 연결합니다. resize 시 surface를 재생성하며 logical/pixel 비율로 스케일합니다. flushAndSubmit 후 SDL_GL_SwapWindow를 호출합니다. SDL_GPU와 Skia의 자동 상호 운용을 가정하지 않습니다.

Inter가 설치된 Linux에서 화면을 검증했습니다. 다른 시스템에서는 font manager의 대체 글꼴을 사용하며 동일한 글자 폭은 아직 보장하지 않습니다. 텍스트 shaping/한국어 IME와 accessibility는 다음 단계입니다. 캡처는 GPU surface의 readPixels 결과입니다. offscreen llvmpipe 실행과 물리 GPU 실행을 구분합니다.

## 입력과 문서

매 프레임 등록한 logical-coordinate hit target에 SDL 입력을 연결합니다. 지원하는 조작은 노트 추가/이동/길이 변경/삭제, velocity, quantize, 건반 미리 듣기, 클립 추가/이동/길이 변경/삭제, 채널 선택/추가, mute/solo, 채널·Master gain, 신스 cutoff, BPM 텍스트 입력, 브라우저 검색/접기, 도크 전환/접기/분할, 확대/스크롤, 룰러 seek, loop/metronome, 저장/불러오기/WAV export입니다.

UI → Project::prepare/compile → Engine::submit 공개 인터페이스를 사용합니다. 계획 큐가 차면 최신 문서를 다음 프레임에 재시도합니다. 용량을 초과한 편집은 commit 시 원본으로 복구합니다. Undo/Redo는 최대 64개 문서 snapshot이며 drag 하나를 한 변경으로 기록합니다. 트랙 선택·검색·뷰 변경은 이력을 만들지 않습니다. 패턴 선택은 Pattern mode 재생 및 저장 상태를 바꾸므로 이력에 포함됩니다. UI는 엔진 내부 voice/backend에 접근하지 않습니다.

`--smoke`는 실제 hit target에서 편집 후 문서 상태를 검사합니다. `--view piano|devices|mixer`, `--window-size`, `--frames`, `--screenshot`으로 화면을 검증합니다. 전체 redraw 구조이며 보이지 않는 클립/파형은 cull합니다. 노트·텍스트의 대규모 캐시, 범용 docking tree, workspace 저장, 범용 node별 invalidation cache와 사용자 keybinding은 미구현입니다.

## 작곡·파일·오디오 설정 연결

기존 도크 제목을 패턴 selector로 사용하며 같은 palette·작은 control의 menu에서 New/Duplicate/Rename/Assign을 제공합니다. 8개씩 페이지를 전환하여 32패턴을 선택할 수 있습니다. MIDI 클립 선택은 그 패턴을 Piano Roll에 표시합니다. 오디오 클립 선택 시 같은 도크에 source waveform·Start(seconds)·Loop·Relink를 표시합니다. Samples와 Ctrl+I, WAV drop은 선택 트랙의 마지막 클립 뒤에 파일을 배치합니다. 프로젝트 및 export에는 SDL3 OS 파일 선택기를 사용합니다. 대화상자 callback은 문서에 접근하지 않고 mailbox에 선택 경로만 전달합니다.

파일 worker가 진행 중이면 편집 입력을 잠시 막고 상태를 표시합니다. 성공한 snapshot만 채택하며 오류는 기존 문서를 보존합니다. Ctrl+Shift+S, 변경 표시, close Save/Discard/Cancel과 30초 자동 저장 및 시작 시 복구 선택을 제공합니다. 종료 선택을 취소하거나 저장 실패 시 창은 계속 열립니다.

상태 바의 오디오 상태 또는 Ctrl+,에서 장치 UID/ID와 128/256/512/1024 frame buffer를 요청합니다. 실제 buffer, output 지연 추정치, native 오류, xrun 지표·처리 frames·최대 process 시간을 표시합니다. UI는 Backend 공개 인터페이스를 통한 애플리케이션 요청만 전달합니다. `.github/workflows/ci.yml`은 native backend와 문서 코어를 세 OS에서 검사하지만 GPU 창과 실제 소리를 검증하는 GUI CI는 아직 포함하지 않습니다.

## 작곡과 녹음 확장

Editing model은 note/clip selection과 clipboard를 소유하고 group transform의 이동 한계를 전체 선택에서 계산합니다. Shift toggle/marquee, Ctrl+A/C/X/V/D, Delete와 화살표 입력이 실제 문서 transaction에 연결됩니다. move 전체를 drag 이전 snapshot에서 계산해 cumulative drift를 피하며 commit 하나가 Undo 하나입니다. 복사된 MIDI clip을 다른 트랙에 붙일 때 원본 트랙+pattern ID로 패턴 복제를 구분합니다.

기본 8마디 viewport는 유지하고 Arrangement +8 bars, zoom 및 horizontal scroll로 최대 256마디를 편집합니다. offscreen clip과 파형 pixel은 cull합니다. 32트랙 색상은 기존 8색 palette를 반복합니다.

Instrument 제목으로 Synth/Sampler/Drum을 바꾸며 source/waveform, sampler root, envelope, 16 drum key의 sample을 선택합니다. 같은 Devices 카드에서 EQ/Compressor/Delay, 같은 Mixer strip에서 pan/output/send와 추가 bus를 조작합니다.

Record arm과 상단 Record/R, Shift+R MIDI take, Stop, input monitor가 실제 controller callback에 연결됩니다. 녹음 중 문서 편집은 잠시 막고 stop/record는 계속 동작합니다. 파일/입력 장치 실패를 상태에 표시합니다. Ctrl+, popup의 Input/MIDI에서 8개씩 페이지를 넘겨 포트를 선택하며 MIDI drop 수를 표시합니다. 녹음 중 파일 drop도 거절합니다. 입력에는 native OS save dialog를 take 경로에 사용합니다. `.partial.wav` 자동 복구 및 별도 take editor는 미구현입니다.
