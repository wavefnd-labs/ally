# 독립 멀티미디어 프레임워크

`Ally::Media`는 프로젝트/UI/audio 의존성이 없는 C++23 모듈입니다. PCM은 sampleRate/channels와 interleaved float vector로 표현합니다. I/O와 모든 vector 메모리는 비실시간 용도입니다.

## 현재 구현

RIFF little-endian WAV를 span으로 파싱하고 PCM unsigned8/signed16/signed24/signed32 및 IEEE float32를 float PCM으로 디코딩합니다. 최대 64채널, 8–384 kHz를 검사합니다. 기존 vector writer는 PCM16 WAV를 출력합니다. 별도 WavWriter는 mono/multichannel float32 WAV를 순차 기록합니다. 앱은 export와 CLI inspect에서 이를 사용합니다.

청크 길이는 부모 RIFF 경계 내부에 있어야 합니다. chunk padding, fmt/data 중복, byte rate/block alignment, partial frame, 비정상 sample을 검사합니다. 알 수 없는 청크는 유효 경계 안에서 건너뜁니다. data가 fmt보다 먼저 와도 처리합니다. 파일/입력 크기를 512 MiB로 제한하며 whole-file decode의 크기 제한입니다. 별도 WavReader는 RIFF 32-bit 범위 안에서 큰 파일을 순차/seek 디코딩합니다. 실패는 구체적인 std::runtime_error로 전달합니다.

순수 `decodeWav(span<const byte>)` 경계가 추후 libFuzzer/AFL harness 진입점입니다. 현재는 모든 유효 WAV prefix의 절단, 손상 header, odd chunk padding 및 4000 deterministic random malformed input을 테스트합니다. 이는 지속적 퍼징을 대신하지 않습니다.

## 미지원

RF64/ds64, AIFF/AIFC, WAVE_FORMAT_EXTENSIBLE, PCM endian 변환 general API, channel masks, metadata 유지, FLAC/MP3/Opus/AAC, 비디오, hardware acceleration은 미구현입니다. RF64/AIFF 입력을 RIFF인 것처럼 해석하지 않고 명시적으로 거절합니다.

## 다음 설계

seekable byte source와 streaming reader를 분리하고 bounds-checked chunk reader를 공통화합니다. RF64는 64-bit ds64와 chunk table, AIFF는 big-endian FORM/COMM/SSND와 80-bit sample rate를 엄격히 처리합니다. PCM scalar reference kernel에 Highway SIMD를 추가하고 비디오/codec registry를 독립 API로 확장합니다. FFmpeg 의존성은 도입하지 않습니다.

WAV 가져오기와 소스 추적은 Project 계층이 이 독립 decode API를 호출하여 수행합니다. mono/stereo 선택과 libsoxr sample-rate 변환은 준비 단계의 Audio/Project 조합이며 미디어 라이브러리가 UI/문서/오디오 엔진에 의존하지 않습니다. 파일 경로, 누락/변경 검사와 캐시 ownership도 Project 계층에 둡니다.

## 순차 I/O

WavReader는 fmt/data/RIFF/padding/byte-rate/frame alignment를 파일 크기와 비교해 검증하고 최대 65536-frame read만 할당합니다. 일반 PCM decoder와 같은 unsigned8/signed16/24/32/IEEE float32를 지원하며 data-before-fmt도 허용합니다. WavWriter는 append/finish로 RIFF size를 갱신하고 RIFF 32-bit 크기를 넘기기 전에 실패합니다. 1초마다 header를 checkpoint하며 앱에서 capture queue를 소비하는 disk worker와 streaming offline export가 사용합니다. malformed/truncated file, seek boundary, multi-track write 회귀를 검사합니다.

Project가 fingerprint·1024-bin waveform을 순차 스캔하고 작은 PCM에는 libsoxr HQ, 큰 source에는 48 kHz cache worker를 선택합니다. Media에는 UI/Project/Audio dependency가 없습니다. long stream의 rate conversion은 현재 선형 보간이고 연속 libsoxr DSP 처리는 향후 추가합니다. atomic completed file replacement는 Project의 플랫폼 file layer가 담당합니다.
