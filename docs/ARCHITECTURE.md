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
Audio backend -> Linux ALSA / Windows WASAPI / macOS Core Audio -> Engine process()
```

`libs/audio`와 `libs/media`는 각각 독립 CMake 프로젝트입니다. audio는 현재 media에도 의존하지 않습니다. 오프라인 렌더링의 조합은 Application Core에서 수행합니다. media에서 프로젝트/UI/audio 의존성은 없습니다. UI가 내부 voice/backend 상태에 접근하지 않으며 Engine 공개 API로 transport/plan/미터를 전달합니다.

## 소유권과 스레드

메인 스레드가 문서와 UI, SDL 창, GL context를 소유합니다. 오디오 장치가 열리면 native worker/callback이 Engine::process의 유일 소비자입니다. offline 모드에서는 메인 루프가 유일 소비자가 됩니다. UI 계획 큐는 단일 producer/consumer이며 MIDI 큐는 libremidi callback 단일 producer와 audio 단일 consumer입니다. 계획 큐가 가득 차면 UI dirty 상태를 유지해 다음 프레임에 최신 문서를 재시도합니다. live MIDI 큐 drop은 adapter가 계수합니다. 건반 미리 듣기는 별도 UI SPSC queue를 사용하여 libremidi producer와 충돌하지 않습니다. MIDI note off와 미리 듣기 note off는 각각 자신의 보이스만 해제합니다.

Engine destruction 이전 backend worker 및 MIDI callback을 종료합니다. 파일 가져오기·문서 열기·저장·export는 std::async worker에서 snapshot을 대상으로 수행하고 결과만 메인 스레드에서 commit합니다. immutable PCM shared ownership으로 Undo/Redo와 worker snapshot의 대용량 복사를 피합니다. 파일과 vector 편집은 비실시간 경로입니다. 오프라인 export는 별도 Engine 인스턴스를 만들어 라이브 엔진 소유권과 충돌하지 않습니다.

## 현재 파일 배치

- `src/app`: CLI, SDL events, 장치/렌더러 lifecycle.
- `src/project`: 문서 유효성, 버전형 저장, 소스 준비, atomic 파일 교체/lease, DocumentSession 자동 저장과 독립 offline render 조합.
- `src/midi`: 자체 channel-message model, sample timeline take 및 libremidi adapter.
- `src/recording`: native CaptureSink, bounded queue와 다중 트랙 WAV writer.
- `src/graphics`: 실제 GL context/framebuffer와 Skia 연결.
- `src/ui/workspace`: 통합 패널 layout, 자체 controls와 piano editing.
- `libs/audio/src/core`: 샘플 이벤트, 보이스, 제한된 queue와 C ABI.
- `libs/audio/src/backend`: ALSA/WASAPI/Core Audio 및 명시적 unsupported backend.
- `libs/media/src/container`: 경계 검사를 갖춘 span 기반 RIFF/WAV parser/writer.
- `libs/media/src/io`: 순차 WavReader/WavWriter.
- `src/project/stream.cpp`: Audio/Media를 조합하는 비실시간 disk cache adapter.

sequencer compile은 현재 project.cpp에 작고 응집된 구현으로 존재합니다. 범용 tempo-map이 생길 때 src/sequencer로 이동합니다. 기본 신스는 엔진 검증용 자체 DSP이며 향후 instrument node로 분리합니다. 사용하지 않는 디렉터리는 확장용 예약 공간이며 구현 완료를 의미하지 않습니다.

## 확장 원칙

현재 문서는 최대 32트랙, 트랙마다 32개의 독립 4-beat 패턴과 최대 1,024-beat Arrangement의 자유 위치·길이 클립을 갖습니다. 클립은 패턴 ID 또는 오디오 소스 ID를 참조합니다. 초기 문서는 사용자 HTML의 5채널 배치를 사용합니다. 내장 Texture와 짧은 WAV는 immutable PCM, 긴 WAV는 고정 캐시 SampleStream 소스이며 파형 peak cache는 작업 단계에서 생성합니다. 소스 경로·원본 sample rate/frame 수·PCM fingerprint를 저장해 누락/변경 파일을 알립니다. 같은 클립을 유지하고 무음으로 복원하며 사용자가 Relink한 뒤 새 데이터를 채택합니다.

버전 6 저장은 독립 패턴·소스와 mixer/device/loop 상태를 포함하며 이전 버전 1–5를 읽습니다. 문서 크기는 1 MiB, 소스 count와 note/event count를 검증한 뒤 외부 파일을 엽니다. 가져오기·열기 실패는 기존 문서를 교체하지 않습니다. 파일 선택은 SDL3 OS dialog callback의 mailbox를 통해 메인 스레드로 전달합니다. atomic save는 same-directory 임시 파일 flush 후 OS replacement를 수행합니다. DocumentSession은 saved/dirty/recovery 상태를 추적하며 각 실행 인스턴스는 서로 다른 recovery 경로와 OS lease를 사용합니다. live lease가 있는 복구본은 건드리지 않습니다. Undo/Redo는 64개 snapshot이며 drag 한 번을 한 변경으로 취급합니다. 패턴 선택은 Pattern mode의 재생 계획과 저장한 activePattern을 바꾸므로 문서 이력을 남깁니다.

향후 command/transaction, tempo-map, automation, node graph를 추가합니다. graph compilation, buffer reservation, latency topology, DSP initialization은 control/worker 단계에서 끝내고 audio 단계는 준비된 plan만 실행합니다. plugin binary ABI는 내부 C++ 객체 주소를 노출하지 않습니다. 플랫폼 backend는 별도 파일/타깃으로 유지합니다. 모바일/Web/BSD는 지원 범위에서 제외합니다.

## 작곡·제작 연결

Project Track에 instrument/source/drum key map/record arm/input routing와 strip parameter, Project에 timeline/bus/master strip이 추가됩니다. Editing은 UI와 분리된 그룹/clipboard 변경 API입니다. MIDI Take는 libremidi에 의존하지 않는 clock-based 문서 recorder이며 libremidi Input은 포트·메시지만 전달합니다.

Recorder는 CaptureSink와 bounded SPSC/disk worker를 연결해 여러 armed 트랙 WAV를 동시에 생성하고 완료 파일만 importer에 보냅니다. Stream adapter는 독립 Media WavReader를 audio SampleStream으로 연결하며 엔진에서 파일·문서·외부 라이브러리 타입을 노출하지 않습니다. offline render는 별도 stream instance를 열어 live cache와 경쟁하지 않습니다.
