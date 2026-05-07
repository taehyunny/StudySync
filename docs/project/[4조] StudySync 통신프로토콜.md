# [4조] StudySync 통신 프로토콜

## 1. 통신 구조 개요

| 구간 | 프로토콜 | 형식 | 비고 |
|---|---|---|---|
| 클라이언트 → AI서버 | HTTP POST | multipart/form-data 또는 JSON | 5fps base64 프레임 전송 |
| AI서버 → 클라이언트 | HTTP 200 | application/json | 집중도, 자세, 가이드 반환 |
| 클라이언트 → 메인서버 | HTTP REST | application/json | JWT 인증 헤더 필수 |
| 메인서버 → 클라이언트 | HTTP 200 | application/json | 통계, 세션, 목표 데이터 반환 |

## 2. AI 서버 API

| 메서드 | 엔드포인트 | 요청 파라미터 | 응답 | 설명 |
|---|---|---|---|---|
| POST | `/analyze/frame` | `session_id`, `frame(base64)` | `focus_score`, `state`, `neck_angle`, `guide` | 5fps 프레임 분석 |
| POST | `/baseline/capture` | `session_id`, `frame` | `saved: true` | 기준 자세 저장 |
| POST | `/traindata` | `landmarks_json`, `label` | `id`, `saved` | 재학습 데이터 수집 |
| GET | `/traindata/count` | - | `{집중, 딴짓, 졸음, total}` | 라벨별 수집 현황 |
| POST | `/model/train` | - | `accuracy`, `status` | 재학습 실행 트리거 |

### 2.1 POST `/analyze/frame` 상세

클라이언트는 전체 웹캠 프레임을 전송하지 않고, 30fps 기준 6프레임마다 1장씩 약 5fps로 샘플링하여 AI 서버에 분석 요청을 보냅니다.

기본 정책:

- 클라이언트 화면 렌더링은 AI 응답을 기다리지 않습니다.
- AI 응답이 늦으면 오래된 프레임 분석 결과는 자연스럽게 의미가 줄어듭니다.
- 클라이언트는 항상 최신 프레임을 렌더링하고, 이벤트 판단은 수신된 최신 분석 결과 기준으로 수행합니다.
- 초기 구현은 JSON + base64 JPEG 방식으로 시작합니다.
- 성능 병목이 확인되면 multipart/form-data 또는 스트리밍 방식으로 전환할 수 있습니다.

#### 요청 예시

```http
POST /analyze/frame HTTP/1.1
Content-Type: application/json
X-StudySync-Client-Id: client-pc-001
X-StudySync-Session-Id: 342
```

```json
{
  "session_id": 342,
  "frame_id": 1204,
  "timestamp_ms": 1770000000000,
  "format": "jpeg",
  "frame_base64": "{base64_encoded_jpeg}"
}
```

#### 응답 예시

```json
{
  "code": "OK",
  "message": "analyzed",
  "session_id": 342,
  "frame_id": 1204,
  "timestamp_ms": 1770000000000,
  "focus_score": 82,
  "state": "focus",
  "neck_angle": 18.4,
  "shoulder_diff": 3.2,
  "ear": 0.31,
  "posture_ok": true,
  "drowsy": false,
  "absent": false,
  "guide": "정상 자세입니다",
  "landmarks": []
}
```

#### 클라이언트 처리 흐름

```text
CaptureThread
  → AiSendBuffer
      → AiAnalyzeThread
          → POST /analyze/frame
          → AnalysisResult 수신
          → PostureEventDetector.feed()
              → 이벤트 발생 시 EventQueue push
```

## 3. 메인서버 API

| 메서드 | 엔드포인트 | 요청 파라미터 | 응답 | 설명 |
|---|---|---|---|---|
| POST | `/auth/register` | `email`, `password`, `name` | `user_id`, `message` | 회원가입 |
| POST | `/auth/login` | `email`, `password` | `access_token`, `user_id` | 로그인/JWT 발급 |
| POST | `/goal` | `daily_goal_min`, `rest_interval_min`, `rest_duration_min` | `saved: true` | 목표 저장/수정 |
| GET | `/goal` | - | 목표 설정 JSON | 목표 조회 |
| POST | `/session/start` | `date` | `session_id`, `start_time` | 세션 시작 |
| POST | `/session/end` | `session_id` | `focus_min`, `avg_focus`, `goal_achieved` | 세션 종료 |
| POST | `/focus` | `session_id`, `focus_score`, `state`, `is_absent`, `is_drowsy` | `saved: true` | 집중도 저장 |
| POST | `/posture` | `session_id`, `neck_angle`, `shoulder_diff`, `posture_ok`, `vs_baseline` | `saved: true` | 자세 저장 |
| POST | `/events` | 이벤트 메타데이터 JSON | `event_id`, `saved: true` | 자세/졸음/이탈 이벤트 저장 |
| GET | `/stats/today` | - | `focus_min`, `avg_focus`, `warning_count`, `goal_progress` | 오늘 통계 |
| GET | `/stats/hourly` | `date=YYYY-MM-DD` | `[{hour, avg_focus}]` | 시간대별 집중도 |
| GET | `/stats/pattern` | - | `avg_focus_duration`, `best_hour`, `weekly_avg` | 휴식 패턴 분석 |
| GET | `/stats/weekly` | - | `[{date, focus_min, avg_focus}]` | 주간 리포트 |

## 4. 공통 인증 헤더

```http
Authorization: Bearer {access_token}
Content-Type: application/json
X-StudySync-Client-Id: {client_id}
X-StudySync-Session-Id: {session_id}
X-Idempotency-Key: {event_id}
```

## 5. 이벤트 API 상세

### 5.1 POST `/events`

클라이언트에서 자세 경고, 졸음, 자리이탈, 휴식 권장 같은 이벤트가 발생했을 때 메인서버에 메타데이터만 저장합니다.

기본 정책:

- 영상 원본은 메인서버에 저장하지 않습니다.
- 기본 `clip_access`는 `local_only`입니다.
- `local_only` 이벤트는 클라이언트 로컬에만 영상 클립이 있으며, 메인서버는 영상을 직접 조회하지 않습니다.
- 클라이언트는 로컬 이벤트 클립을 기본 3일간 보관하고 자동 삭제합니다.
- 같은 이벤트가 재전송되어도 중복 저장되지 않도록 `event_id`를 UNIQUE로 처리합니다.

### 요청 예시

```http
POST /events HTTP/1.1
Authorization: Bearer {access_token}
Content-Type: application/json
X-StudySync-Client-Id: client-pc-001
X-StudySync-Session-Id: 342
X-Idempotency-Key: event_342_1770000000000_bad_posture
```

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

### 응답 예시

```json
{
  "code": "OK",
  "message": "event metadata saved",
  "event_id": "event_342_1770000000000_bad_posture",
  "saved": true
}
```

### 중복 이벤트 응답 예시

같은 `event_id`가 이미 저장되어 있다면 서버는 새 row를 만들지 않고 기존 이벤트를 인정합니다.

```json
{
  "code": "DUPLICATE_EVENT",
  "message": "event already exists",
  "event_id": "event_342_1770000000000_bad_posture",
  "saved": false
}
```

### 이벤트 타입

| event_type | 설명 |
|---|---|
| `bad_posture` | 목 각도/어깨 기울기 등 자세 기준 초과 |
| `drowsy` | EAR 기준 또는 졸음 판단 |
| `absent` | 자리이탈 |
| `rest_required` | 설정된 휴식 주기 또는 과집중 휴식 권장 |

### clip_access 정책

| clip_access | 의미 | 메인서버 영상 조회 가능 여부 |
|---|---|---|
| `none` | 영상 클립 없음 | 불가 |
| `local_only` | 클립은 클라이언트 로컬에만 존재 | 불가 |
| `uploaded_url` | 사용자 동의 후 서버/스토리지에 업로드된 URL | 가능 |

초기 구현은 `local_only`만 사용합니다. `uploaded_url`은 사용자 동의 기반 업로드 기능이 추가될 때 별도 API로 확장합니다.

## 6. 오류 코드

| HTTP 코드 | 오류 코드 | 설명 |
|---|---|---|
| 400 | `INVALID_REQUEST` | 요청 파라미터 누락 또는 형식 오류 |
| 401 | `UNAUTHORIZED` | JWT 토큰 없음 또는 만료 |
| 404 | `NOT_FOUND` | 세션 또는 리소스 없음 |
| 409 | `DUPLICATE_EMAIL` | 이미 사용 중인 이메일 |
| 409 | `DUPLICATE_EVENT` | 이미 저장된 이벤트 ID |
| 500 | `INTERNAL_ERROR` | 서버 내부 오류 또는 AI 분석 실패 |

