# 메인서버 요청 사항: POSTURE_EVENT ERD 및 `/events` API 추가

> 작성일: 2026-05-06  
> 요청자: 정태현 - 클라이언트  
> 대상: 김혜윤 - 메인서버  
> 목적: 클라이언트 로컬 Claim Check 구조를 메인서버 DB/API와 맞추기 위한 추가 요청

---

## 1. 왜 추가가 필요한가

현재 StudySync 클라이언트는 이벤트가 발생하면 원본 영상을 메인서버에 업로드하지 않습니다.

개인정보 보호를 위해 이벤트 클립은 클라이언트 로컬에만 3일간 저장하고, 메인서버에는 다음과 같은 메타데이터만 보냅니다.

```text
이벤트 발생 시각
이벤트 타입
이벤트 사유
로컬 클립 식별자
로컬 보관 기간
만료 시각
```

따라서 `focus_logs`, `posture_logs`만으로는 이벤트를 표현하기 어렵습니다.

`posture_logs`는 주기적으로 저장되는 자세 수치 로그이고, `posture_events`는 특정 사건을 기록하는 테이블입니다.

---

## 2. 추가 요청 테이블

### `posture_events`

```sql
CREATE TABLE posture_events (
  id BIGINT AUTO_INCREMENT PRIMARY KEY,
  event_id VARCHAR(128) UNIQUE NOT NULL,
  session_id BIGINT NOT NULL,
  event_type VARCHAR(32) NOT NULL,
  severity VARCHAR(20) DEFAULT 'warning',
  reason TEXT NULL,
  ts DATETIME NOT NULL,
  timestamp_ms BIGINT NOT NULL,

  clip_id VARCHAR(128) NULL,
  clip_access VARCHAR(32) NOT NULL DEFAULT 'local_only',
  clip_ref TEXT NULL,
  clip_format VARCHAR(32) NULL,
  frame_count INT DEFAULT 0,
  retention_days INT DEFAULT 3,
  expires_at_ms BIGINT NULL,

  created_at DATETIME DEFAULT CURRENT_TIMESTAMP,

  INDEX idx_session_ts (session_id, timestamp_ms),
  INDEX idx_event_type (event_type),
  FOREIGN KEY (session_id) REFERENCES sessions(id) ON DELETE CASCADE
);
```

### 필드 의미

| 필드 | 의미 |
|---|---|
| `event_id` | 클라이언트가 생성하는 중복 방지 ID |
| `session_id` | 이벤트가 발생한 학습 세션 |
| `event_type` | bad_posture / drowsy / absent / rest_required |
| `severity` | info / warning / critical |
| `reason` | 이벤트 발생 사유 |
| `ts` | ISO8601 또는 서버 변환 DATETIME |
| `timestamp_ms` | 프레임 매칭 기준 epoch milliseconds |
| `clip_id` | 클라이언트 로컬 클립 ID |
| `clip_access` | none / local_only / uploaded_url |
| `clip_ref` | local_only이면 로컬 참조값, uploaded_url이면 URL |
| `clip_format` | jpeg_sequence / mp4 / none |
| `frame_count` | 로컬 이벤트 클립 프레임 수 |
| `retention_days` | 클라이언트 로컬 보관 일수 |
| `expires_at_ms` | 클라이언트 로컬 클립 만료 시각 |

---

## 3. 추가 요청 API

## POST `/events`

클라이언트가 이벤트 메타데이터를 메인서버에 저장합니다.

### Headers

```http
Authorization: Bearer {access_token}
Content-Type: application/json
X-StudySync-Client-Id: {client_id}
X-StudySync-Session-Id: {session_id}
X-Idempotency-Key: {event_id}
```

### Request

```json
{
  "event_id": "event_342_1770000000000_bad_posture",
  "session_id": 342,
  "event_type": "bad_posture",
  "severity": "warning",
  "reason": "neck_angle over threshold",
  "timestamp": "2026-05-06T14:30:00+09:00",
  "timestamp_ms": 1770000000000,
  "clip_id": "local:event_1770000000000",
  "clip_access": "local_only",
  "clip_ref": "local:event_1770000000000",
  "clip_format": "jpeg_sequence",
  "frame_count": 180,
  "retention_days": 3,
  "expires_at_ms": 1770259200000
}
```

### Success Response

```json
{
  "code": "OK",
  "message": "event metadata saved",
  "event_id": "event_342_1770000000000_bad_posture",
  "saved": true
}
```

### Duplicate Response

같은 `event_id`가 재전송될 수 있으므로 서버는 `event_id`를 UNIQUE로 보고 멱등 처리해야 합니다.

```json
{
  "code": "DUPLICATE_EVENT",
  "message": "event already exists",
  "event_id": "event_342_1770000000000_bad_posture",
  "saved": false
}
```

---

## 4. 이벤트 타입

| event_type | 설명 |
|---|---|
| `bad_posture` | 목 각도/어깨 기울기 기준 초과 |
| `drowsy` | 졸음 판단 |
| `absent` | 자리이탈 |
| `rest_required` | 휴식 권장 또는 과집중 경고 |

---

## 5. clip_access 정책

| 값 | 의미 | 메인서버 영상 조회 |
|---|---|---|
| `none` | 클립 없음 | 불가 |
| `local_only` | 클립은 클라이언트 로컬에만 3일 보관 | 불가 |
| `uploaded_url` | 사용자가 동의한 경우 서버/스토리지에 업로드 | 가능 |

초기 버전은 `local_only`만 사용합니다.

`uploaded_url`은 개인정보 동의 UI와 별도 업로드 API가 생긴 뒤 확장합니다.

---

## 6. 서버 구현 시 주의점

1. `event_id`는 반드시 UNIQUE 처리합니다.
2. `clip_access = local_only`인 경우 서버는 영상을 조회하려고 하면 안 됩니다.
3. 메인서버는 영상 원본을 저장하지 않고, 메타데이터만 저장합니다.
4. JWT의 `user_id`와 `session_id` 소유자가 일치하는지 검증해야 합니다.
5. `timestamp_ms`는 프레임/이벤트 매칭용이므로 유지해야 합니다.
6. 화면 표시용 시간은 `timestamp` 또는 DB의 `ts`를 사용합니다.
7. 추후 `uploaded_url`을 추가할 경우 사용자 동의 여부를 별도 필드나 테이블로 관리해야 합니다.

---

## 7. 클라이언트 현재 구현 상태

클라이언트에는 다음 구조가 이미 들어가 있습니다.

```text
EventShadowBuffer
→ PostureEventDetector
→ EventQueue
→ LocalClaimCheckClipStore
→ JsonlBatchUploader
```

현재 클라이언트는 이벤트 발생 시 다음 메타데이터를 만들 수 있습니다.

```text
clip_id
clip_access = local_only
clip_format = jpeg_sequence
frame_count
retention_days = 3
expires_at_ms
```

메인서버의 `/events` API가 준비되면 클라이언트의 `HttpJsonlLogSink` 또는 `EventApi`에서 해당 API로 연결하면 됩니다.

