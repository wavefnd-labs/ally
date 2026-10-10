# 시스템 구조

## 계층과 의존 방향

```text
Application (SDL 이벤트 루프, lifecycle)
    -> UI workspace (자체 hit-test/layout/input)
    -> Project / MIDI / Sequencer 모델 (ally_core)
    -> Ally::Audio (공개 Engine/Plan/Backend 인터페이스)
    -> Ally::Media (오프라인 WAV I/O)
Graphics renderer -> SDL3 OpenGL context + Skia Ganesh
MIDI adapter -> libremidi -> Engine MIDI queue
Audio backend -> Linux ALSA -> Engine process()
```

`libs/audio`와 `libs/media`는 각각 독립 CMake 프로젝트입니다. audio는 현재 media에도 의존하지 않습니다. 오프라인 렌더링의 조합은 Application Core에서 수행합니다. media에서 프로젝트/UI/audio 의존성은 없습니다. UI가 내부 voice/backend 상태에 접근하지 않으며 Engine 공개 API로 transport/plan/미터를 전달합니다.

## 소유권과 스레드

메인 스레드가 문서와 UI, SDL 창, GL context를 소유합니다. 오디오 장치가 열리면 ALSA worker가 Engine::process의 유일 소비자입니다. offline 모드에서는 메인 루프가 유일 소비자가 됩니다. UI 계획 큐는 단일 producer/consumer이며 MIDI 큐는 libremidi callback 단일 producer와 audio 단일 consumer입니다. 계획 큐가 가득 차면 UI dirty 상태를 유지해 다음 프레임에 최신 문서를 재시도합니다. live MIDI 큐 drop은 adapter가 계수합니다. 건반 미리 듣기는 별도 UI SPSC queue를 사용하여 libremidi producer와 충돌하지 않습니다. MIDI note off와 미리 듣기 note off는 각각 자신의 보이스만 해제합니다.

Engine destruction 이전 backend worker 및 MIDI callback을 종료합니다. 파일과 vector 편집은 비실시간 경로입니다. 오프라인 export는 별도 Engine 인스턴스를 만들어 라이브 엔진 소유권과 충돌하지 않습니다.

## 현재 파일 배치

- `src/app`: CLI, SDL events, 장치/렌더러 lifecycle.
- `src/project`: 문서 유효성, 버전형 저장, 독립 offline render 조합.
- `src/midi`: 자체 channel-message model 및 libremidi adapter.
- `src/graphics`: 실제 GL context/framebuffer와 Skia 연결.
- `src/ui/workspace`: 통합 패널 layout, 자체 controls와 piano editing.
- `libs/audio/src/core`: 샘플 이벤트, 보이스, 제한된 queue와 C ABI.
- `libs/audio/src/backend`: ALSA 및 명시적 unsupported backend.
- `libs/media/src/container`: 경계 검사를 갖춘 span 기반 RIFF/WAV parser/writer.

sequencer compile은 현재 project.cpp에 작고 응집된 구현으로 존재합니다. 범용 tempo-map이 생길 때 src/sequencer로 이동합니다. 기본 신스는 엔진 검증용 자체 DSP이며 향후 instrument node로 분리합니다. 사용하지 않는 디렉터리는 확장용 예약 공간이며 구현 완료를 의미하지 않습니다.

## 확장 원칙

현재 문서는 최대 8채널과 채널별 4-beat 패턴, 32-beat Arrangement의 자유 위치·길이 클립을 갖습니다. 초기 문서는 사용자 HTML의 5채널 배치를 사용합니다. MIDI 클립은 소스 패턴을 반복하고 Variation은 신스 노트를 한 옥타브 올립니다. 레인과 instrument/mixer 채널은 1:1이며 여러 독립 패턴과 임의 소스/레인 연결은 다음 단계입니다. 내장 Texture PCM은 준비 단계에서 생성하고 엔진이 immutable 복사본을 소유합니다. UI도 같은 PCM에서 파형을 계산합니다. 외부 WAV 파일 import는 지원하지 않습니다.

버전 4 저장은 클립과 mixer/device/loop 상태를 포함하며 이전 버전 1–3을 읽습니다. UI snapshot 이력으로 Undo/Redo를 제공하고 drag 한 번을 한 변경으로 취급합니다. 향후 command/transaction, tempo-map, automation, node graph를 추가합니다. graph compilation, buffer reservation, latency topology, DSP initialization은 control/worker 단계에서 끝내고 audio 단계는 준비된 plan만 실행합니다. plugin binary ABI는 내부 C++ 객체 주소를 노출하지 않습니다. 플랫폼 backend는 별도 파일/타깃으로 유지합니다. 모바일/Web/BSD는 지원 범위에서 제외합니다.
