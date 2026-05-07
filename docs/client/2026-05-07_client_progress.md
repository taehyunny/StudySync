# StudySync 클라이언트 구현 진행 현황

작성일: 2026-05-07  
브랜치: feature/taehyun  
작성자: 정태현 (클라이언트 담당)

---

## 현재 상태 요약

메인서버 통합 테스트 전 항목 통과 확인 (2026-05-07).  
클라이언트 핵심 블로커 2개 해소 + SessionApi 버그 수정 완료.  
데이터 흐름 파이프라인 연결 완성. AI 서버 없이 메인서버 실 연동 테스트 가능 상태.

---

## 완료된 작업 목록

### ✅ 아키텍처 / 렌더링

| 항목 | 내용 |
|------|------|
| MFC 프로젝트 구조 | CWinApp → CMainFrame → CStudySyncClientView 계층 완성 |
| Direct2D 렌더링 파이프라인 | CaptureThread → RingBuffer → RenderThread → D2DRenderer → HWND |
| OverlayPainter HUD | 집중도 바, 상태 점, 자세, 목각도, EAR 바, 어깨차 — DirectWrite 패널 |
| AnalysisResultBuffer | AiTcpClient(쓰기) ↔ D2DRenderer(읽기) 스레드 안전 공유 버퍼 |

### ✅ 네트워크 — AI 서버 TCP

| 항목 | 내용 |
|------|------|
| AiTcpClient | 프레임 전송(2000) + 분석결과 수신(2001) 구현 완료 |
| 프로토콜 | 4-byte BE 헤더 + JSON + JPEG binary |
| 자동 재연결 | 연결 끊김 시 2초 후 재시도 루프 |
| ZMQ 제거 | ZmqSendThread, ZmqRecvThread, AiAnalyzeThread 전면 교체 |

### ✅ 네트워크 — 메인서버 HTTP

| 항목 | 내용 |
|------|------|
| WinHttpClient | GET / POST JSON / POST NDJSON 지원, JWT Bearer 자동 주입 |
| AuthApi | /auth/login, /auth/register — access_token 우선 파싱, detail fallback |
| SessionApi | /session/start → session_id, /session/end → focus_min/avg_focus/goal_achieved |
| TokenStore | %APPDATA%/StudySync/token.dat 저장/복원 |

### ✅ 로그 파이프라인 (B1 + B2 — 2026-05-07 완료)

| 항목 | 내용 |
|------|------|
| **B1** `flush_to_http` 구현 | NDJSON POST to `/log/ingest`, accepted/skipped 파싱 로그 |
| **B1** `post_ndjson()` | WinHttpClient에 application/x-ndjson Content-Type 추가 |
| **B2** `session_id` 필드 | 모든 JSONL 라인에 session_id 포함 (서버 소유권 검증 필수) |
| **B2** `event_id` 필드 | PostureEvent에 event_id 추가, `"evt-{timestamp_ms}"` 자동 생성 |
| **B2** `set_session_id()` | ILogSink → HttpJsonlLogSink → JsonlBatchUploader 위임 체인 |
| **B2** View 연결 | set_session_id() 호출 시 log_sink에 자동 주입 |

### ✅ UI — 로그인 / 회원가입

| 항목 | 내용 |
|------|------|
| LoginDlg | IDD_LOGIN 다이얼로그, AuthApi::login() 연결 |
| RegisterDlg | IDD_REGISTER 다이얼로그, AuthApi::register_user() 연결 |
| 에러 표시 | message + detail 두 필드 합쳐서 IDC_STATIC_MSG 표시 |
| 세션 흐름 | 로그인 성공 → SessionApi::start() → session_id → View 전달 |

### ✅ 빌드 / 인프라

| 항목 | 내용 |
|------|------|
| CMake + VS2022 | configure + MSBuild Debug x64 빌드 성공 |
| /utf-8 플래그 | 한국어 소스 인코딩 문제 해소 |
| dwrite 링크 | DirectWrite HUD 렌더링 링크 추가 |
| ws2_32 링크 | Winsock2 TCP 링크 추가 |

---

## 현재 JSONL 라인 형식 (서버 계약 기준)

```jsonl
{"kind":"analysis","session_id":1001,"timestamp_ms":1746514205123,"focus_score":85,"state":"focus","neck_angle":12.3,"shoulder_diff":3.1,"ear":0.35,"posture_ok":true,"drowsy":false,"absent":false}
{"kind":"event","session_id":1001,"event_id":"evt-1746514210000","timestamp_ms":1746514210000,"reason":"neck_angle over threshold","frame_count":30,"clip_id":"...","clip_ref":"local://..."}
```

---

## 남은 작업 목록

### ✅ C3 완료 — SessionApi 버그 수정 (2026-05-07)

`goal_achieved`가 JSON 불린(`true`/`false`)인데 문자열 파서로 읽어 **항상 false** 반환하던 버그 수정.  
`extract_bool()` 헬퍼 추가. 세션 종료 실패 시 에러 로그 출력 추가.

---

### 🔴 높음

| # | 항목 | 내용 |
|---|------|------|
| D1 | DummyAnalysisGenerator | AI 서버 없이 더미 분석결과로 전체 로그 흐름 실 서버 테스트 |

### 🟡 중간

| # | 항목 | 내용 |
|---|------|------|
| D2 | 통계 대시보드 UI | StatsApi + /stats/today, /hourly, /weekly, /pattern 화면 연결 |
| D3 | 목표 설정 UI | GoalApi + /goal POST/GET 연결 |

### 🔵 후순위 (AI팀 준비 후)

| # | 항목 | 내용 |
|---|------|------|
| E1 | AiTcpClient 실연동 | AI팀 포트 9100 준비 후 실제 연결 테스트 |
| E2 | 더미 → 실제 AI 전환 | DummyAnalysisGenerator 비활성화 후 AiTcpClient 결과로 전환 |

---

## 데이터 흐름 전체 구조 (현재 기준)

```
[로그인] LoginDlg → AuthApi → WinHttpClient → /auth/login
                                              → TokenStore 저장
[세션]   StudySyncClientApp → SessionApi → /session/start → session_id
                                         → View::set_session_id()
                                         → log_sink::set_session_id()

[캡처]   CaptureThread → RingBuffer ──────────────────────────────┐
                                                                   │
[렌더]   RenderThread ← RingBuffer                                 │
         D2DRenderer.upload_and_render()                           │
         OverlayPainter.draw(AnalysisResult) ← AnalysisResultBuffer
                                                     ↑
[AI TCP] AiTcpClient ─── FRAME_PUSH(2000) ──→ AI서버(9100)       │
         recv ANALYSIS_RES(2001) ───────────────────────────────── ┘
         result_buffer_.update()
         PostureEventDetector.feed() → EventQueue

[로그]   EventUploadThread → clip_store_.store_clip()
                           → log_sink_.append_event_metadata()
                           → log_sink_.flush()
                           → JsonlBatchUploader.flush_to_http()
                           → WinHttpClient.post_ndjson(/log/ingest)
                           → HTTP 200 : accepted/skipped 로그 출력

[세션종료] OnDestroy → SessionApi::end() → /session/end
                     → focus_min / avg_focus / goal_achieved 출력
```

---

## 메인서버 연동 테스트 통과 항목 (2026-05-07 서버팀 확인)

| 시나리오 | 결과 |
|---------|------|
| 회원가입/로그인 (access_token + token alias) | 200/201 ✓ |
| /goal POST/GET | 200 ✓ |
| /session/start | 200 ✓ |
| /log/ingest 30라인 (analysis 25 + event 5) | accepted: {analysis:25, event:5}, skipped:0 ✓ |
| /log/ingest 멱등 재전송 (같은 event_id) | DB 변경 없이 success ✓ |
| /log/ingest 남의 세션 차단 | skipped:1 ✓ |
| /session/end | focus_min(INT), avg_focus(0.84), goal_achieved(bool) ✓ |
| /stats/today /hourly /weekly /pattern | 모두 200 ✓ |
| 인증 실패 (401) | message + detail 두 필드 ✓ |
