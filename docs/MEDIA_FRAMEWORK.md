# 독립 멀티미디어 프레임워크

`Ally::Media`는 Project/UI/Audio에 의존하지 않는 C++23 모듈입니다. PCM은 rate/channels와 interleaved float로 표현하며 모든 I/O는 비실시간입니다. FFmpeg 의존성은 없습니다.

## WAV / RF64

span decoder와 seekable WavReader는 RIFF/WAVE 및 RF64의 단일 oversized data chunk를 읽습니다. RF64는 선행 ds64의 64-bit RIFF/data/sample count와 sentinel을 검사합니다. PCM unsigned8/signed16/24/32 및 float32, 1–64 channels, 8–384 kHz를 지원합니다. 청크 경계·padding·중복 fmt/data/ds64·byte rate/alignment·불완전 frame·비정상 float를 거절합니다. 알려지지 않은 bounded chunk는 건너뜁니다.

whole-file decode는 512 MiB 제한이며 streaming read는 최대 65536 frames만 준비합니다. RF64 크기는 파일 크기와 signed 64-bit seek 범위로 검사합니다. 여러 oversized chunk를 위한 ds64 table은 명시적으로 미지원입니다. `decodeWav(span)`은 추후 fuzz harness 진입점입니다.

## 장시간 기록

WavWriter는 float32를 순차 기록합니다. 시작 시 80-byte header에 JUNK 공간을 예약하며 32-bit RIFF 크기를 넘으면 같은 위치를 RF64/ds64로 전환합니다. 데이터 이동이나 파일 전체 복사가 없습니다. append당 최대 65536 frames, 변환 byte buffer는 생성 시 한 번 준비합니다. writer는 복사할 수 없습니다.

PCM을 flush한 뒤 1초 단위로 header size를 갱신하고 5초 단위 및 finish에서 POSIX fsync / Windows _commit을 수행합니다. 정상 finish는 close 오류까지 검사합니다. 디스크/flush/seek 오류를 예외로 보고하며 DAW는 `.partial.wav`를 보존합니다. fsync는 OS의 저장 완료 요청이고 정전·불량 저장장치까지 보증하지 않습니다.

Recorder worker는 8192 frames 단위로 각 route를 batch하며 낮은 입력량에서는 200 ms 단위로 flush합니다. callback queue overflow와 알려진 native gap은 위치를 유지한 무음으로 기록합니다. worker 오류를 UI에서 조기에 확인합니다. DAW source와 take는 현재 6시간 제한이지만 독립 Media writer 자체는 RIFF 4 GiB 제한을 넘는 RF64를 지원합니다.

## 중단 파일 복구

`recoverRecording(partial, newDestination)`은 Ally writer의 고정 header만 허용하고 원본을 변경하지 않습니다. 실제 파일 길이의 완전한 frame만 순차 읽고 float 검증을 거쳐 새 WAV/RF64에 기록합니다. checkpoint 이후의 데이터도 복원하고 마지막 불완전 frame은 제외합니다. 기존 목적지에는 기록하지 않습니다. 오류가 나면 원본과 미완료 목적지를 남깁니다. 녹음 중인 파일에는 실행하지 않아야 합니다.

앱 CLI는 `ally --recover-recording take.wav.partial.wav recovered.wav`를 제공합니다. 자동 take 복구 UI와 여러 트랙 복구 transaction은 아직 없습니다. 완료 파일의 atomic replacement는 Project 플랫폼 file layer가 담당합니다.

## 미지원과 다음 단계

AIFF/AIFC, WAVE_FORMAT_EXTENSIBLE/channel mask, RF64 ds64 table, metadata 보존, 일반 PCM endian API, FLAC/MP3/Opus/AAC, 비디오/하드웨어 가속은 미구현입니다. 다음 단계는 seekable byte source·공통 chunk reader, AIFF의 big-endian/80-bit rate 검증, 지속적 fuzz CI, 실제 DSP 경로에 필요한 Highway/PFFFT 통합입니다.

Project의 fingerprint/1024-bin 파형 스캔과 libsoxr HQ 캐시 변환은 독립 Media 위에서 조합합니다. 공개 API에 다른 라이브러리의 자료형을 노출하지 않습니다. 검증 근거는 VALIDATION.md에 기록합니다.
