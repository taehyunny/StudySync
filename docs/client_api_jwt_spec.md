# StudySync 클라이언트 ↔ 메인서버 API / JWT 명세

> 작성일: 2026-05-06  
> 작성자: 정태현 (클라이언트)  
> 목적: 메인서버 HTTP 서버 구현 시 클라이언트와 맞춰야 할 스펙 정의

---

## 1. 공통 사항

### 베이스 URL
```
http://10.10.10.100:8080
```

### 인증 방식
- 로그인/회원가입 제외한 모든 요청에 JWT 필수
- 헤더: `Authorization: Bearer <token>`
- 알고리즘: **HS256**

### 공통 응답 포맷
```json
{ "code": <HTTP 상태코드>, "message": "설명" }
```
성공 시 추가 필드 포함, 실패 시 `message`에 오류 설명.

### 시간 포맷
ISO8601: `"2026-05-06T14:30:00+09:00"`

---

## 2. JWT 페이로드 (서버가 발급 시 포함할 클레임)

```json
{
  "sub":   "42",
  "email": "user@example.com",
  "name":  "홍길동",
  "role":  "user",
  "iat":   1746500000,
  "exp":   1746586400
}
```

| 필드 | 타입 | 설명 |
|------|------|------|
| `sub` | string | users.id (DB PK, 문자열) |
| `email` | string | 로그인 이메일 |
| `name` | string | 표시 이름 |
| `role` | string | `"user"` 고정 |
| `iat` | int | 발급 시각 (Unix timestamp) |
| `exp` | int | 만료 시각 (권장: iat + 86400, 24시간) |

> 클라이언트는 토큰을 `%APPDATA%/StudySync/token.dat`에 저장하며,  
> 만료 시 재로그인 후 새 토큰으로 교체합니다.

---

## 3. API 엔드포인트 명세

### 3-1. 회원가입

```
POST /auth/register
Content-Type: application/json
```

**Request Body**
```json
{
  "email":    "user@example.com",
  "password": "plaintext_password",
  "name":     "홍길동"
}
```

**Response**
```json
// 201 Created (성공)
{ "code": 201, "message": "ok", "user_id": 42 }

// 409 Conflict (이메일 중복)
{ "code": 409, "message": "email already exists" }

// 400 Bad Request (필드 누락/형식 오류)
{ "code": 400, "message": "invalid email format" }
```

---

### 3-2. 로그인

```
POST /auth/login
Content-Type: application/json
```

**Request Body**
```json
{
  "email":    "user@example.com",
  "password": "plaintext_password"
}
```

**Response**
```json
// 200 OK (성공)
{
  "code":    200,
  "token":   "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9...",
  "user_id": 42,
  "name":    "홍길동"
}

// 401 Unauthorized (이메일/비밀번호 불일치)
{ "code": 401, "message": "invalid credentials" }
```

---

### 3-3. 학습 목표 설정

```
POST /goal
Authorization: Bearer <token>
Content-Type: application/json
```

**Request Body**
```json
{
  "daily_goal_min":    120,
  "rest_interval_min": 50,
  "rest_duration_min": 10
}
```

**Response**
```json
// 200 OK
{ "code": 200, "message": "ok" }
```

---

### 3-4. 학습 목표 조회

```
GET /goal
Authorization: Bearer <token>
```

**Response**
```json
// 200 OK
{
  "code": 200,
  "daily_goal_min":    120,
  "rest_interval_min": 50,
  "rest_duration_min": 10,
  "updated_at": "2026-05-06T10:00:00+09:00"
}

// 404 (목표 미설정)
{ "code": 404, "message": "goal not set" }
```

---

### 3-5. 세션 시작

```
POST /session/start
Authorization: Bearer <token>
Content-Type: application/json
```

**Request Body**
```json
{
  "start_time": "2026-05-06T14:30:00+09:00"
}
```

**Response**
```json
// 200 OK
{ "code": 200, "session_id": 1001 }
```

> **중요**: 클라이언트는 받은 `session_id`를 AI서버로 프레임 전송 시  
> JSON body에 포함하여 전달합니다. AI서버는 이 값을 TCP 패킷에 실어  
> 메인서버 focus_logs / posture_logs 테이블에 기록합니다.

---

### 3-6. 세션 종료

```
POST /session/end
Authorization: Bearer <token>
Content-Type: application/json
```

**Request Body**
```json
{
  "session_id": 1001,
  "end_time":   "2026-05-06T16:00:00+09:00"
}
```

**Response**
```json
// 200 OK (서버가 focus_logs 집계 후 반환)
{
  "code":           200,
  "focus_min":      72,
  "avg_focus":      0.83,
  "goal_achieved":  true
}
```

---

### 3-7. 오늘 통계

```
GET /stats/today
Authorization: Bearer <token>
```

**Response**
```json
// 200 OK
{
  "code":           200,
  "focus_min":      90,
  "avg_focus":      0.78,
  "warning_count":  3,
  "goal_progress":  0.75
}
```

---

### 3-8. 시간대별 집중도

```
GET /stats/hourly?date=2026-05-06
Authorization: Bearer <token>
```

**Response**
```json
// 200 OK
{
  "code": 200,
  "data": [
    { "hour": 9,  "avg_focus": 0.82 },
    { "hour": 10, "avg_focus": 0.75 },
    { "hour": 14, "avg_focus": 0.90 }
  ]
}
```

---

### 3-9. 집중 패턴 분석

```
GET /stats/pattern
Authorization: Bearer <token>
```

**Response**
```json
// 200 OK
{
  "code":                  200,
  "avg_focus_duration":    47,
  "best_hour":             10,
  "weekly_avg":            0.76
}
```

---

### 3-10. 주간 통계

```
GET /stats/weekly
Authorization: Bearer <token>
```

**Response**
```json
// 200 OK
{
  "code": 200,
  "data": [
    { "date": "2026-04-30", "focus_min": 85,  "avg_focus": 0.80 },
    { "date": "2026-05-01", "focus_min": 120, "avg_focus": 0.75 },
    { "date": "2026-05-06", "focus_min": 90,  "avg_focus": 0.78 }
  ]
}
```

---

## 4. AI서버 HTTP API (클라 → AI)

```
POST /analyze/frame
Content-Type: application/json
```

**Request Body**
```json
{
  "session_id": 1001,
  "image":      "<base64 JPEG>"
}
```

**Response**
```json
{
  "focus_score":    0.85,
  "state":          "집중",
  "is_absent":      false,
  "is_drowsy":      false,
  "neck_angle":     12.3,
  "shoulder_diff":  5.1,
  "posture_ok":     true,
  "vs_baseline":    0.92,
  "latency_ms":     45
}
```

---

## 5. 오류 코드 표

| code | 의미 | 사용 예 |
|------|------|---------|
| 200 | 성공 | 일반 성공 |
| 201 | 생성됨 | 회원가입 |
| 400 | 잘못된 요청 | 필드 누락, 형식 오류 |
| 401 | 인증 실패 | 잘못된 비밀번호, 만료된 JWT |
| 403 | 권한 없음 | 다른 유저 리소스 접근 |
| 404 | 없음 | 목표 미설정, 세션 없음 |
| 409 | 충돌 | 이메일 중복 |
| 500 | 서버 오류 | DB 오류 등 |

---

## 6. TCP 프로토콜 (AI서버 → 메인서버, 참고용)

클라이언트 직접 관여 없음. AI서버가 분석 후 메인서버 TCP 9000으로 전송:

```
FOCUS_LOG_PUSH   = 1000  // 5fps 집중도 로그
FOCUS_LOG_ACK    = 1001
POSTURE_LOG_PUSH = 1002  // 자세 분석 로그
POSTURE_LOG_ACK  = 1003
```

TCP 패킷 JSON 예시 (AI → 메인):
```json
{
  "protocol_no": 1000,
  "session_id":  1001,
  "ts":          "2026-05-06T14:30:05+09:00",
  "focus_score": 0.85,
  "state":       "집중",
  "is_absent":   false,
  "is_drowsy":   false,
  "latency_ms":  45
}
```

---

*이 문서는 클라이언트(정태현) 기준으로 작성. 서버 구현 시 이 스펙에 맞춰 응답 형식 통일 요청.*
