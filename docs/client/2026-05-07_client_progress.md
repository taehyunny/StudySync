# StudySync 클라이언트 구현 진행 현황

작성일: 2026-05-07 (최종 업데이트)  
브랜치: `feature/taehyun`  
작성자: 정태현 (클라이언트 담당)

---

## 전체 진행률 요약

| 영역 | 상태 |
|------|------|
| 렌더링 파이프라인 (D2D HUD) | ✅ 완료 |
| 로그인 / 회원가입 UI | ✅ 완료 |
| 메인서버 HTTP 연동 | ✅ 완료 |
| 분석 데이터 로그 전송 | ✅ 완료 |
| DummyAnalysisGenerator (더미 AI) | ✅ 완료 |
| 세션 타이머 HUD | ✅ 완료 |
| 토스트 알림 | ✅ 완료 |
| 캘리브레이션 (기준 자세 설정) | ✅ 완료 |
| 통계 다이어그램 (집중도 그래프) | ✅ 완료 |
| 휴식 권장 알림 오버레이 | ✅ 완료 |
| **Stage 1: MediaPipe 이관 + keypoint 전송** | ✅ 완료 |
| AI 서버 TCP 실 연동 (use_dummy_ai=false) | 🔵 대기 (AI팀 준비 후) |
| 피드백 UI — 복기 화면 (맞아요/틀렸어요) | 🔴 2단계 미구현 |
| POST /feedback 업로드 | 🔴 3단계 미구현 |
| 세션 종료 불확실 판정 팝업 | 🔴 3단계 미구현 |
| 서버 통계 API 화면 연결 | 🟡 미구현 |
| 목표 설정 UI | 🟡 미구현 |
| 이메일 인증 처리 | 🟡 미구현 |
| 토큰 만료 / 자동 재로그인 | 🟡 미구현 |
| 이벤트 클립 MP4 인코딩 | 🟡 미구현 (포맷 미확정) |

> 상세 잔여 작업 목록: [`TODO.md`](./TODO.md)

---

## ✅ Stage 1 완료 (2026-05-07)

### 변경 요약

| 항목 | 이전 | 이후 |
|---|---|---|
| AI 서버 전송 방식 | JPEG 이미지 (~100KB, 5fps) | keypoint JSON (~100B, 30fps 가능) |
| 클라이언트 분석 | 없음 | OpenCV Haar cascade placeholder |
| TCP 패킷 | header + JSON + binary | header + JSON only (바이너리 없음) |
| AnalysisResult | ear, neck_angle, shoulder_diff | + head_yaw, head_pitch, face_detected, confidence |
| JSONL 로그 | 기존 5개 필드 | 9개 필드 (신규 4개 추가) |

### 변경 파일

| 파일 | 변경 내용 |
|---|---|
| `include/model/AnalysisResult.h` | head_yaw, head_pitch, face_detected, confidence 추가 |
| `include/analysis/LocalMediaPipePoseAnalyzer.h` | CascadeClassifier 멤버, detect_ear() 추가 |
| `src/analysis/LocalMediaPipePoseAnalyzer.cpp` | OpenCV Haar 기반 keypoint 추출 구현 |
| `include/network/AiTcpClient.h` | pose_analyzer_ 멤버, send_keypoint_packet() 선언 |
| `src/network/AiTcpClient.cpp` | JPEG 전송 → keypoint JSON 전송으로 교체 |
| `src/network/JsonlBatchUploader.cpp` | JSONL에 head_yaw/pitch/face_detected 포함 |
| `src/analysis/DummyAnalysisGenerator.cpp` | 더미 시나리오 신규 필드 값 추가 |

### AI 서버 통신 현황

| 항목 | 상태 |
|---|---|
| 프로토콜 | TCP, 포트 9100 |
| 패킷 형식 | 4-byte big-endian 길이 + JSON |
| 전송 방향 (클라 → AI) | keypoint JSON (protocol_no: 2000) |
| 수신 방향 (AI → 클라) | state/confidence/focus_score (protocol_no: 2001) |
| phone_detected | 사용 안 함 |
| JPEG/binary | 사용 안 함 |

프로토콜 상세: [`docs/ai_client_tcp_protocol.md`](../ai_client_tcp_protocol.md)

---

## ✅ 이전 완료 작업

### 아키텍처 / 렌더링

| 항목 | 내용 |
|---|---|
| MFC 프로젝트 구조 | CWinApp → CMainFrame → CStudySyncClientView 계층 완성 |
| Direct2D 렌더링 파이프라인 | CaptureThread → RingBuffer → RenderThread → D2DRenderer → HWND |
| OverlayPainter HUD | 집중도 바, 상태 점, 자세/목각도/EAR/어깨차 DirectWrite 패널 |
| AnalysisResultBuffer | AiTcpClient(쓰기) ↔ D2DRenderer(읽기) 스레드 안전 공유 버퍼 |
| 세션 타이머 | 좌상단 `HH:MM:SS` steady_clock 기반 표시 |
| 토스트 알림 | ToastBuffer + draw_toast() 좌상단 빨간 배너 (4초 자동 소멸) |
| 캘리브레이션 오버레이 | 세션 시작 5초 카운트다운, 기준 자세 수집 후 목각도 임계값 자동 설정 |
| 통계 다이어그램 | SessionStatsHistory 링버퍼(150샘플) + 좌하단 꺾은선 그래프 패널 |
| 휴식 권장 알림 | 졸음/자세불량 이벤트 시 중앙 대형 오버레이 8초 노출 |

### 네트워크 — AI 서버 TCP

| 항목 | 내용 |
|---|---|
| AiTcpClient | keypoint 전송 + 분석결과 수신 (TCP 9100) |
| 자동 재연결 | 연결 끊김 시 2초 간격 재시도 루프 |
| DummyAnalysisGenerator | AI 서버 없이 30초 주기 더미 결과 생성 (use_dummy_ai=true) |

### 네트워크 — 메인서버 HTTP

| 항목 | 내용 |
|---|---|
| WinHttpClient | GET / POST JSON / POST NDJSON, JWT Bearer 자동 주입 |
| URL path 버그 수정 | 절대 URL → path 자동 추출 (방어 코드 포함) |
| AuthApi | /auth/login, /auth/register |
| SessionApi | /session/start → session_id, /session/end → 통계 반환 |
| StatsApi | /stats/today → ServerStatsSnapshot (HUD 초안 연결) |
| TokenStore | %APPDATA%/StudySync/token.dat 저장/복원 |
| HeartbeatClient × 2 | 메인서버 / AI서버 연결 상태 주기 모니터링 |

### 로그 파이프라인

| 항목 | 내용 |
|---|---|
| JsonlBatchUploader | NDJSON POST /log/ingest, accepted/skipped 파싱 |
| 분석 데이터 전송 | result_callback → append_analysis() → 10초 주기 flush |
| 이벤트 데이터 전송 | EventUploadThread → append_event_metadata() → 즉시 flush |
| 세션 종료 flush | OnDestroy에서 잔여 데이터 최종 전송 |
| 확인된 서버 응답 | `accepted(analysis=47~50, event=1)` 200 OK 반복 확인 |

### 이벤트 / 알림 파이프라인

| 항목 | 내용 |
|---|---|
| PostureEventDetector | neck_threshold 런타임 설정 (캘리브레이션 결과 적용) |
| AlertManager | 자세불량/졸음/집중저하 로컬 판단, AlertQueue 전달 |
| AlertDispatchThread | ToastBuffer 토스트 + RenderThread 휴식 오버레이 동시 트리거 |
| EventUploadThread | 이벤트 발생 시 ClipStore 저장 + 서버 즉시 전송 |

### UI

| 항목 | 내용 |
|---|---|
| LoginDlg | IDD_LOGIN, AuthApi::login() 연결 |
| RegisterDlg | IDD_REGISTER, AuthApi::register_user() 연결 |
| 에러 표시 | message + detail 두 필드 통합 표시 |
| 세션 흐름 | 로그인 → SessionApi::start() → session_id → View |

---

## 데이터 흐름 전체 구조 (2026-05-07 기준)

```
[로그인]  LoginDlg → AuthApi → /auth/login → access_token → WinHttpClient
[세션]    MainFrm → SessionApi → /session/start → session_id → View::set_session_id()

[캡처]    CaptureThread (30fps) → 3개 버퍼 분기
            ├─ RenderFrameBuffer → RenderThread → D2DRenderer → OverlayPainter
            ├─ SendFrameBuffer   → AiTcpClient (use_dummy_ai=false 시)
            └─ EventShadowBuffer → EventUploadThread

[더미AI]  DummyAnalysisGenerator (200ms 주기, use_dummy_ai=true)
            → result_callback
               ├─ SessionStatsHistory.push()    ← 통계 그래프용
               ├─ log_sink.append_analysis()    ← 10초 배치 전송
               └─ AlertManager.feed_local()     ← 알림 판단
                    └─ AlertDispatchThread
                         ├─ ToastBuffer (토스트, 4초)
                         └─ RenderThread.set_break_alert() (오버레이, 8초)

[실AI]    AiTcpClient (use_dummy_ai=false)
            → LocalMediaPipePoseAnalyzer (OpenCV Haar, placeholder)
               → keypoint JSON (protocol 2000)
                  → AI 서버 TCP 9100
                     → ANALYSIS_RES (protocol 2001)
                        → AnalysisResultBuffer + PostureEventDetector

[이벤트]  PostureEventDetector → EventQueue
          EventUploadThread → LocalClaimCheckClipStore
                            → log_sink.append_event_metadata() + flush()
                            → POST /log/ingest (즉시)

[배치]    IDT_LOG_FLUSH (10초) → log_sink.flush() → POST /log/ingest
[종료]    OnDestroy → log_sink.flush() → SessionApi::end()
```

---

## 메인서버 연동 실 확인 항목 (2026-05-07)

| 시나리오 | 결과 |
|---------|------|
| /auth/register, /auth/login | ✅ 200/201 |
| /session/start | ✅ session_id 수신 |
| /log/ingest 분석 데이터 배치 | ✅ accepted(analysis=47~50) 200 OK |
| /log/ingest 이벤트 데이터 | ✅ accepted(event=1) 200 OK |
| /session/end | ✅ focus_min / avg_focus / goal_achieved 수신 |
| Main Server Heartbeat | ✅ status=Connected failures=0 |
| AI Server Heartbeat | 🔄 status=Reconnecting (더미 모드라 정상) |
