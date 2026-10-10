# 독립 멀티미디어 프레임워크

`Ally::Media`는 프로젝트/UI/audio 의존성이 없는 C++23 모듈입니다. PCM은 sampleRate/channels와 interleaved float vector로 표현합니다. I/O와 모든 vector 메모리는 비실시간 용도입니다.

## 현재 구현

RIFF little-endian WAV를 span으로 파싱하고 PCM unsigned8/signed16/signed24/signed32 및 IEEE float32를 float PCM으로 디코딩합니다. 최대 64채널, 8–384 kHz를 검사합니다. writer는 stereo를 포함한 PCM16 WAV를 출력합니다. 앱은 export와 CLI inspect에서 이를 사용합니다.

청크 길이는 부모 RIFF 경계 내부에 있어야 합니다. chunk padding, fmt/data 중복, byte rate/block alignment, partial frame, 비정상 sample을 검사합니다. 알 수 없는 청크는 유효 경계 안에서 건너뜁니다. data가 fmt보다 먼저 와도 처리합니다. 파일/입력 크기를 512 MiB로 제한하며 큰 파일 streaming은 아직 없습니다. 실패는 구체적인 std::runtime_error로 전달합니다.

순수 `decodeWav(span<const byte>)` 경계가 추후 libFuzzer/AFL harness 진입점입니다. 현재는 모든 유효 WAV prefix의 절단, 손상 header, odd chunk padding 및 4000 deterministic random malformed input을 테스트합니다. 이는 지속적 퍼징을 대신하지 않습니다.

## 미지원

RF64/ds64, AIFF/AIFC, WAVE_FORMAT_EXTENSIBLE, PCM endian 변환 general API, channel masks, metadata 유지, streaming I/O, FLAC/MP3/Opus/AAC, 비디오, hardware acceleration은 미구현입니다. RF64/AIFF 입력을 RIFF인 것처럼 해석하지 않고 명시적으로 거절합니다.

## 다음 설계

seekable byte source와 streaming reader를 분리하고 bounds-checked chunk reader를 공통화합니다. RF64는 64-bit ds64와 chunk table, AIFF는 big-endian FORM/COMM/SSND와 80-bit sample rate를 엄격히 처리합니다. PCM scalar reference kernel에 Highway SIMD를 추가하고 비디오/codec registry를 독립 API로 확장합니다. FFmpeg 의존성은 도입하지 않습니다.
