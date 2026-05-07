# StudySync 클라이언트 ↔ 메인서버 구조 비교

> **작성일**: 2026-05-06  
> **목적**: 클라이언트(MFC C++)와 메인서버(C++) 간 프로토콜·스키마·도메인 불일치 정리  
> **대상**: 서버 담당 조원에게 공유하여 합의점 도출

---

## 1. 통신 프로토콜

### 현재 상태

| 구분 | 클라이언트 (feature/taehyun) | 메인서버 (main) |
|------|---------------------------|----------------|
| 전송 방식 | **HTTP REST** (WinHTTP API) | **TCP 바이너리** (raw socket) |
| 포트 | 8000 | 9000 |
| 패킷 구조 | HTTP Request + JSON body | 4바이트 길이 헤더(big-endian) + JSON + 바이너리(선택) |
| 메시지 식별 | URL 경로 (`/auth/login`, `/auth/register`) | `protocol_no` 정수 필드 (ProtocolNo enum) |
| 인증 토큰 | JWT Bearer → HTTP Authorization 헤더 | 미정의 (TCP 세션 기반 추정) |

### 문제점

- 클라이언트가 HTTP 요청을 보내면 서버의 `PacketReader`가 인식할 수 없음
- 서버는 TCP 스트림에서 4바이트 헤더를 먼저 읽고, 그 길이만큼 JSON을 파싱하는 구조
- **서로 연결 자체가 불가능한 상태**

### 합의 필요 사항

- [ ] 클라이언트가 TCP 바이너리 프로토콜로 전환할지, 서버가 HTTP REST를 추가할지
- [ ] StudySync용 `protocol_no` 번호 대역 정의 (현재 100~199는 공장 MFC 클라이언트용)
- [ ] JWT 인증 vs TCP 세션 인증 방식 결정

---

## 2. 인증 (로그인 / 회원가입)

### 현재 상태

| 구분 | 클라이언트 | 메인서버 |
|------|-----------|---------|
| 로그인 식별자 | `email` | `username` |
| 회원가입 필드 | email, password, name | employee_id, username, password, role |
| 로그인 요청 | `POST /auth/login` (HTTP) | `protocol_no: 100` (LOGIN_REQ, TCP) |
| 로그인 응답 | `{success, token, user_id, message}` | `protocol_no: 101` (LOGIN_RES, TCP) |
| 회원가입 요청 | `POST /auth/register` (HTTP) | `protocol_no: 104` (REGISTER_REQ, TCP) |
| 회원가입 응답 | `{success, user_id, message}` | `protocol_no: 105` (REGISTER_RES, TCP) |
| 비밀번호 저장 | bcrypt 해시 (서버측) | bcrypt 해시 (서버측) ✅ 동일 |

### 문제점

- 클라이언트는 `email`로 로그인하지만 서버 DB의 `users` 테이블에는 `email` 컬럼이 없음
- 서버는 `employee_id`(사번)가 필수이나 클라이언트에는 해당 필드 없음
- 서버의 `role` 필드(Admin 등)가 StudySync에 필요한지 미정

### 합의 필요 사항

- [ ] 로그인 식별자: `email` vs `username` 통일
- [ ] `employee_id` 필드 필요 여부 (StudySync에서는 불필요할 수 있음)
- [ ] `role` 값 정의: Admin/User 외에 StudySync용 역할이 필요한지

---

## 3. DB 스키마 (ERD)

### 메인서버 현재 테이블 (Factory DB)

```
users         — employee_id, username, password_hash, role, last_login_at
inspections   — station_id, bottle_id, result(ok/ng), confidence, defect_type, image_path, heatmap_path
assemblies    — cap_ok, label_ok, fill_ok, yolo_detections(JSON), patchcore_score
models        — station_id, model_type(PatchCore/YOLO11), version, accuracy, file_path
bottles       — code, status(pending/...)
```

### 클라이언트가 기대하는 테이블 (StudySync ERD)

```
USER          — email, name, password_hash, created_at
SESSION       — user_id, start_time, end_time, duration
FOCUS_LOG     — session_id, timestamp, focus_score, ear(눈 깜빡임)
POSTURE_LOG   — session_id, timestamp, posture_type, confidence
POSTURE_EVENT — session_id, event_type, start_time, end_time, clip_ref
TRAIN_DATA    — label, image_path, uploaded_at
```

### 비교

| 메인서버 (현재) | StudySync (필요) | 관계 |
|---------------|-----------------|------|
| users | USER | 필드 불일치 (username vs email, employee_id 유무) |
| inspections | - | StudySync에 불필요 (공장 검사용) |
| assemblies | - | StudySync에 불필요 (조립 검사용) |
| models | - | StudySync에 불필요 (YOLO/PatchCore 모델 관리) |
| bottles | - | StudySync에 불필요 (용기 추적) |
| - | SESSION | 메인서버에 없음 |
| - | FOCUS_LOG | 메인서버에 없음 |
| - | POSTURE_LOG | 메인서버에 없음 |
| - | POSTURE_EVENT | 메인서버에 없음 |
| - | TRAIN_DATA | 메인서버에 없음 |

### 합의 필요 사항

- [ ] 서버 DB에 StudySync 테이블 추가 가능한지
- [ ] 기존 Factory 테이블을 유지할지, 제거할지
- [ ] `users` 테이블 스키마 통일 (어느 쪽에 맞출지)

---

## 4. 비즈니스 도메인

| 구분 | 메인서버 (현재) | 클라이언트 (StudySync) |
|------|---------------|---------------------|
| 목적 | 공장 제품 품질 검사 | 학습 집중도 관리 |
| AI 모델 | PatchCore (이상탐지), YOLO11 (객체감지) | MediaPipe (포즈 분석) |
| 데이터 | 제품 이미지 → 양품/불량 판정 | 웹캠 영상 → 자세/집중도 분석 |
| 워크플로우 | Station1(입고) → Station2(조립) → 결과 저장 | 세션 시작 → 실시간 분석 → 이벤트 클립 저장 |
| 제어 | 공장 라인 일시정지/재개 | 학습 타이머 + 알림 |

---

## 5. 서버 내부 구조 (참고)

메인서버가 현재 사용하는 핵심 구조:

```
main-server/
├── common/Protocol.h          ← ProtocolNo enum (100~1999), 패킷 구조 정의
├── include/
│   ├── core/event_bus.h       ← 내부 이벤트 버스 (pub/sub)
│   ├── handler/router.h       ← protocol_no → 도메인 이벤트 변환
│   ├── storage/
│   │   ├── connection_pool.h  ← MySQL 커넥션 풀
│   │   └── dao.h              ← 테이블별 DAO (InspectionDao, UserDao 등)
│   └── network/
│       └── packet_reader.h    ← TCP 스트림 → 4바이트 헤더 파싱
└── sql/schema.sql             ← Factory DB DDL
```

- `Router`가 TCP 패킷의 `protocol_no`를 읽어 `EventBus`로 분기
- `UserDao`가 `find_by_username()`, `insert()` 등 DB 접근 처리
- 서버는 nlohmann::json 없이 수동 JSON 파서 사용 (1-depth 평탄 JSON 전용)

---

## 6. 제안하는 합의 방향

### Option A: 서버를 StudySync에 맞게 확장

1. `schema.sql`에 StudySync 테이블 추가 (SESSION, FOCUS_LOG, POSTURE_LOG 등)
2. `users` 테이블에 `email` 컬럼 추가 또는 `username`을 email로 사용
3. `Protocol.h`에 StudySync용 `protocol_no` 추가 (예: 200~299)
4. 클라이언트는 WinHTTP 제거 → TCP 소켓 클라이언트로 전환
5. `Router`에 StudySync 핸들러 추가

### Option B: 서버에 HTTP REST 레이어 추가

1. 서버에 HTTP 엔드포인트 레이어 추가 (기존 TCP와 공존)
2. 클라이언트의 WinHTTP 코드 유지
3. DB에 StudySync 테이블 추가는 동일

### 권장: Option A

- 서버가 이미 TCP 기반으로 완성도 있게 구축됨
- 클라이언트가 TCP로 전환하는 것이 서버에 HTTP를 추가하는 것보다 작업량 적음
- MFC 클라이언트에서 TCP 소켓은 자연스러운 선택 (CAsyncSocket / Winsock2)

---

## 7. 당장 서버 담당자에게 요청할 것

1. **StudySync용 DB 테이블 추가 가능 여부** 확인
2. **`users` 테이블 스키마** 합의 (email 컬럼 추가 or username 사용)
3. **StudySync용 `protocol_no` 번호 대역** 할당 (200~299 제안)
4. **로그인/회원가입 패킷 JSON 필드** 확정
5. **세션/집중도/자세 데이터 수신용 프로토콜** 설계 협의 일정

---

*이 문서는 클라이언트 개발자(정태현) 기준으로 작성되었습니다.*
