# StudySync 클라이언트 - 메인 서버 구조 차이 분석

> 작성일: 2026-05-06  
> 대상: 정태현 클라이언트 파트 / 김혜윤 메인 서버 파트 / 팀 공유용  
> 목적: 현재 클라이언트 구조와 원격 `main`에 추가된 메인 서버 구조의 불일치 지점을 정리하고, 합의가 필요한 항목을 도출한다.

---

## 1. 결론 요약

현재 클라이언트와 메인 서버는 같은 StudySync 시스템으로 바로 연결되기 어렵다.

가장 큰 이유는 다음 세 가지다.

1. 클라이언트는 HTTP/JSONL 중심으로 설계되어 있고, 메인 서버는 TCP 바이너리 패킷 기반이다.
2. 클라이언트 도메인은 학습 집중도/자세 분석이고, 메인 서버 도메인은 공장 품질 검사에 가깝다.
3. 인증, DB 스키마, 이벤트 로그, 영상 클립 처리 방식이 서로 맞지 않는다.

따라서 클라이언트 코드를 계속 진행하기 전에 메인 서버 담당자와 프로토콜/DB/도메인 합의가 먼저 필요하다.

---

## 2. 통신 프로토콜 차이

| 항목 | 클라이언트 현재 구조 | 메인 서버 현재 구조 |
|---|---|---|
| 전송 방식 | HTTP REST / JSONL 예정 | TCP raw socket |
| 패킷 구조 | HTTP Header + JSON body | 4바이트 길이 헤더 + JSON payload + 선택적 바이너리 |
| 포트 | HTTP 기준 8000 계열로 가정 | TCP 9000 계열 |
| 메시지 구분 | URL path: `/auth/login`, `/events` 등 | `protocol_no` 정수 enum |
| 인증 | JWT Bearer Header 구상 | TCP 세션 기반으로 추정, JWT 없음 |
| 장애 처리 | Heartbeat / ReconnectPolicy 추가 중 | TCP 연결 상태 기반 처리 필요 |

### 문제점

클라이언트가 HTTP 요청을 보내도 메인 서버의 `PacketReader`는 이를 TCP 패킷으로 읽으려고 한다.  
즉 현재 상태에서는 연결 자체가 성립하지 않는다.

### 합의 필요

- 클라이언트가 TCP 프로토콜로 전환할지
- 메인 서버가 HTTP REST/JSONL 수신 API를 추가할지
- StudySync 전용 `protocol_no` 범위를 새로 배정할지
- 이벤트/로그/인증 패킷의 JSON 필드명을 어떻게 정할지

---

## 3. 인증 구조 차이

| 항목 | 클라이언트 | 메인 서버 |
|---|---|---|
| 로그인 식별자 | `email` | `username` |
| 회원가입 필드 | email, password, name | employee_id, username, password, role |
| 로그인 요청 | HTTP `POST /auth/login` | `ProtocolNo::LOGIN_REQ` |
| 로그인 응답 | token, user_id 예상 | `ProtocolNo::LOGIN_RES` |
| 비밀번호 저장 | 서버 측 hash 전제 | bcrypt hash 구현 |

### 문제점

클라이언트는 일반 서비스형 로그인 구조를 생각하고 있지만, 메인 서버는 사번/역할 기반의 공장 운영 계정 구조에 가깝다.

### 합의 필요

- StudySync에서 `email`을 사용할지, `username`을 사용할지
- `employee_id`가 StudySync에 필요한지
- `role`을 학생/관리자/운영자 등으로 재정의할지
- JWT를 사용할지, TCP 세션 인증으로 갈지

---

## 4. DB 스키마 차이

### 메인 서버 현재 DB 성격

메인 서버 DB는 공장 검사 시스템에 가깝다.

```text
users
inspections
assemblies
models
bottles
```

주요 데이터는 제품 검사 결과, 불량 유형, 조립 상태, AI 모델 버전, 병/용기 추적 등에 맞춰져 있다.

### 클라이언트가 필요로 하는 StudySync DB

StudySync 클라이언트는 다음 성격의 테이블이 필요하다.

```text
users
study_sessions
focus_logs
posture_logs
posture_events
local_clip_metadata
train_data
```

### 차이 요약

| 메인 서버 테이블 | StudySync 필요 여부 | 비고 |
|---|---:|---|
| users | 일부 재사용 가능 | 필드 재정의 필요 |
| inspections | 불필요 | 공장 검사 결과용 |
| assemblies | 불필요 | 조립 검사 상세용 |
| models | 일부 참고 가능 | StudySync AI 모델 관리로 바꾸려면 재설계 필요 |
| bottles | 불필요 | 공장 제품 추적용 |
| study_sessions | 필요 | 현재 없음 |
| focus_logs | 필요 | 현재 없음 |
| posture_events | 필요 | 현재 없음 |
| train_data | Phase 2에서 필요 | 현재 없음 |

---

## 5. 도메인 차이

| 항목 | 메인 서버 현재 방향 | 클라이언트 현재 방향 |
|---|---|---|
| 목적 | 공장 품질 관리 | 학습 집중도/자세 관리 |
| 영상 의미 | 제품 검사 이미지 | 사용자 웹캠 프레임 |
| AI 모델 | PatchCore, YOLO 계열 | MediaPipe Pose / 졸음·자세 분석 |
| 이벤트 | 제품 불량, 라인 제어 | 자세 경고, 졸음, 이탈, 휴식 권장 |
| 저장 데이터 | 검사 결과/불량률 | 집중 시간/자세 로그/이벤트 클립 |
| 개인정보 리스크 | 제품 이미지 중심 | 얼굴/자세 영상 포함으로 높음 |

가장 중요한 차이는 개인정보 민감도다.  
StudySync는 사용자 웹캠 영상이 들어가기 때문에 메인 서버가 원본 영상을 중앙 저장하는 구조는 신중하게 검토해야 한다.

---

## 6. 이벤트 클립 / Claim Check 차이

클라이언트는 현재 다음 구조로 설계되어 있다.

```text
이벤트 발생
→ EventShadowBuffer snapshot
→ 로컬 이벤트 클립 저장
→ JSONL에는 clip_id / clip_access / expires_at_ms만 전송
→ 3일 뒤 로컬 자동 삭제
```

반면 메인 서버는 현재 StudySync 이벤트 클립을 받는 구조가 명확하지 않다.

### 클라이언트가 보내고 싶은 이벤트 JSONL 예시

```json
{"kind":"event","timestamp_ms":1000,"reason":"bad posture","frame_count":180,"clip_id":"local:event_1000","clip_ref":"event_clips/event_1000","clip_access":"local_only","clip_format":"jpeg_sequence","retention_days":3,"created_at_ms":1770000000000,"expires_at_ms":1770259200000}
```

### 합의 필요

- 메인 서버는 영상 원본을 저장할 것인가, 메타데이터만 저장할 것인가
- `clip_access: local_only` 이벤트를 허용할 것인가
- 사용자가 동의한 경우에만 영상 업로드를 허용할 것인가
- 이벤트 로그는 TCP 패킷으로 보낼지, HTTP JSONL로 받을지

---

## 7. 추가로 확인해야 할 차이

### 7.1 시간 기준

클라이언트는 `timestamp_ms` 기반으로 이벤트 클립을 자른다.  
메인 서버도 같은 시간 기준을 사용할지 정해야 한다.

합의 필요:

- epoch milliseconds 사용 여부
- 로컬 시간과 서버 시간 동기화 방식
- session_id 기준 시간 사용 여부

### 7.2 세션 모델

StudySync에는 학습 세션 개념이 필요하다.

```text
session_id
user_id
start_time
end_time
target_minutes
focus_score
```

현재 메인 서버 구조에는 학습 세션 모델이 없다.

### 7.3 알림 방향

클라이언트는 메인 서버가 알림을 내려줄 수도 있다고 가정했다.

예:

```text
Main Server → Client
휴식 권장
자세 경고
강제 휴식 알림
```

현재 메인 서버 프로토콜에는 StudySync 알림 타입이 없다.

### 7.4 실패 복구

클라이언트는 하트비트/재연결/로컬 저장을 추가했다.

메인 서버와 맞출 항목:

- 연결 끊김 시 재로그인 여부
- TCP 재연결 후 세션 복구 방식
- 중복 이벤트 방지를 위한 idempotency key 사용 여부
- 서버 ACK 패킷 필요 여부

---

## 8. 선택 가능한 통합 방향

## Option A. 클라이언트를 메인 서버 TCP 프로토콜에 맞춘다

### 내용

- WinHTTP 기반 API를 줄이고 TCP client를 구현한다.
- 4바이트 길이 헤더 + JSON payload 패킷을 만든다.
- StudySync용 `protocol_no`를 새로 정의한다.
- 로그인/이벤트/세션/로그를 모두 TCP 패킷으로 보낸다.

### 장점

- 메인 서버의 현재 구조를 크게 바꾸지 않아도 된다.
- 기존 `PacketReader`, `Router`, `EventBus` 구조를 활용할 수 있다.

### 단점

- 클라이언트 통신 계층을 많이 바꿔야 한다.
- JSONL/HTTP 기반 로그 설계와 달라진다.
- 영상/이벤트 대량 처리 시 TCP 패킷 정책을 더 세밀하게 정해야 한다.

## Option B. 메인 서버에 StudySync HTTP/JSONL API를 추가한다

### 내용

- 메인 서버가 기존 TCP 구조를 유지하면서 별도 HTTP API를 추가한다.
- 클라이언트는 현재 WinHTTP/JSONL 설계를 유지한다.
- 이벤트/로그/인증 API를 REST 형태로 정의한다.

### 장점

- 클라이언트 현재 구조와 잘 맞는다.
- JSONL 이벤트 로그/Claim Check 설계를 유지하기 쉽다.
- 프론트/외부 도구와 연동하기 쉽다.

### 단점

- 메인 서버에 HTTP 서버 계층을 새로 붙여야 한다.
- 서버 담당자의 작업량이 늘어날 수 있다.

## Option C. 중간 게이트웨이를 둔다

### 내용

```text
Client HTTP/JSONL
→ Gateway
→ Main Server TCP packet
```

### 장점

- 클라이언트와 메인 서버 변경을 줄일 수 있다.
- 프로토콜 변환을 한 곳에 격리할 수 있다.

### 단점

- 배포 구성 요소가 하나 늘어난다.
- 팀 프로젝트 규모에서는 과할 수 있다.

---

## 9. 권장 방향

현 단계에서는 다음 순서를 권장한다.

1. 서버 담당자와 StudySync 도메인으로 갈지, 기존 공장 QC 서버를 유지할지 먼저 합의한다.
2. StudySync로 확정한다면 DB 스키마를 먼저 맞춘다.
3. 그 다음 통신 방식을 TCP로 통일할지 HTTP/JSONL로 통일할지 정한다.
4. 개인정보 이슈 때문에 영상 원본은 기본적으로 클라이언트 로컬 저장으로 두고, 메인 서버에는 이벤트 메타데이터만 저장한다.
5. 사용자가 명시적으로 동의한 경우에만 영상 업로드 옵션을 둔다.

객관적으로 보면, 현재 클라이언트 설계와 가장 잘 맞는 방향은 **Option B: 메인 서버에 StudySync HTTP/JSONL API 추가**다.  
하지만 메인 서버가 이미 TCP 기반으로 많이 구현되어 있다면, 팀 작업량을 줄이기 위해 **Option A: 클라이언트 TCP 전환**도 현실적인 선택지다.

---

## 10. 서버 담당자에게 바로 물어볼 질문

1. 현재 메인 서버는 StudySync용으로 바꿀 예정인가, 아니면 공장 QC 서버 기반을 유지하는가?
2. 클라이언트 통신 방식은 TCP로 맞추는 것이 맞는가?
3. StudySync용 `protocol_no` 범위를 새로 배정할 수 있는가?
4. `users` 테이블은 email 기반으로 바꿀 수 있는가?
5. `study_sessions`, `focus_logs`, `posture_events` 테이블을 추가할 수 있는가?
6. 사용자 영상 클립은 중앙 서버에 저장하지 않고 `local_only` 메타데이터만 받을 수 있는가?
7. 이벤트 수신 후 서버 ACK가 필요한가?
8. 재연결 후 중복 이벤트를 막기 위한 `event_id` 또는 `idempotency_key`를 사용할 수 있는가?

---

## 11. 현재 클라이언트 측 영향

현재 클라이언트에서 이미 구현된 것:

- Direct2D 렌더링 파이프라인
- CaptureThread / RenderThread 분리
- EventShadowBuffer
- Local Claim Check 저장
- 3일 로컬 클립 자동 삭제
- Heartbeat / ReconnectPolicy 기본 구조
- JSONL 이벤트 메타데이터 생성 구조

아직 서버 합의 후 바뀔 가능성이 큰 것:

- `HttpJsonlLogSink`
- `WinHttpClient`
- `AuthApi`
- 로그인 필드 모델
- 이벤트 업로드 프로토콜
- 서버 알림 수신 방식

따라서 다음 작업은 코드 구현보다 **프로토콜 합의**가 우선이다.

