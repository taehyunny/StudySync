# StudySync Client ↔ AI Server TCP Protocol

작성자: 정태현 - 클라이언트

목적: 클라이언트가 AI 서버로 웹캠 프레임을 보내고, 같은 TCP 연결로 분석 결과를 받기 위한 약속입니다.

## 1. 책임 분리

- 클라이언트: 웹캠 프레임을 캡처하고, 최신 프레임만 샘플링해서 AI 서버로 전송합니다.
- AI 서버: JPEG 프레임을 받아 자세/집중도 분석을 수행하고 결과 JSON을 응답합니다.
- 메인 서버: 로그인, 세션, 이벤트 로그, Claim Check 메타데이터를 관리합니다.

## 2. 연결 방식

- 프로토콜: TCP
- 기본 AI 서버: `10.10.10.50:9100`
- 연결 정책: 연결 실패 시 클라이언트가 2초 간격으로 재연결합니다.
- 실시간 정책: 오래된 프레임은 버리고 최신 프레임만 전송합니다.

## 3. 패킷 구조

모든 패킷은 아래 형식을 사용합니다.

```text
[4-byte big-endian JSON length][JSON payload][optional binary payload]
```

- 4-byte header: JSON 길이만 포함합니다.
- JSON payload: `protocol_no`, `session_id`, `timestamp_ms` 등을 포함합니다.
- binary payload: `FRAME_PUSH`에서만 JPEG 바이트를 붙입니다.
- JPEG 크기: JSON의 `image_size` 값으로 판단합니다.

## 4. Protocol Numbers

| protocol_no | 방향 | 이름 | 설명 |
|---:|---|---|---|
| 2000 | Client → AI | FRAME_PUSH | JPEG 프레임 전송 |
| 2001 | AI → Client | ANALYSIS_RES | 자세/집중도 분석 결과 |

## 5. FRAME_PUSH

```json
{
  "protocol_no": 2000,
  "session_id": 1001,
  "timestamp_ms": 1746514205123,
  "timestamp": "2026-05-06T14:30:05+09:00",
  "image_format": "jpeg",
  "image_size": 15234
}
```

JSON 뒤에 `image_size` 바이트만큼 JPEG 데이터가 이어집니다.

## 6. ANALYSIS_RES

```json
{
  "protocol_no": 2001,
  "session_id": 1001,
  "timestamp_ms": 1746514205123,
  "focus_score": 85,
  "state": "focus",
  "is_absent": false,
  "is_drowsy": false,
  "neck_angle": 12.3,
  "shoulder_diff": 5.1,
  "posture_ok": true,
  "ear": 0.35,
  "guide": "",
  "latency_ms": 45
}
```

클라이언트는 이 결과를 `AnalysisResultBuffer`에 저장해서 렌더링 오버레이에 사용하고, `PostureEventDetector`에 공급해서 이벤트 발생 여부를 판단합니다.

## 7. 현재 클라이언트 구현

- 구현 클래스: `AiTcpClient`
- 프레임 입력: `CaptureThread::SendFrameBuffer`
- 결과 공유: `AnalysisResultBuffer`
- 이벤트 감지: `PostureEventDetector`
- 이벤트 저장/전송: `EventQueue` → `EventUploadThread`

## 8. 검증 질문

AI 서버 담당자는 아래 3가지를 먼저 맞추면 됩니다.

1. `9100` 포트에서 TCP listen을 하는가?
2. 4-byte big-endian JSON length를 먼저 읽고 JSON을 파싱하는가?
3. `image_size`만큼 JPEG 바이트를 읽은 뒤 `protocol_no: 2001` JSON을 같은 소켓으로 응답하는가?
