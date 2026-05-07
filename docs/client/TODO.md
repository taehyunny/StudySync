# StudySync 클라이언트 — 구현 현황 및 잔여 작업

> 최종 업데이트: 2026-05-07 (WinHttpClient post_multipart 완료)  
> 브랜치: `feature/taehyun`  
> 작성자: 정태현 (클라이언트 담당)

---

## 전체 구현 완료율

| 카테고리 | 완료도 | 비고 |
|---|---|---|
| MFC 프로젝트 구조 | ✅ 100% | CWinApp → CMainFrame → CStudySyncClientView |
| OpenCV 캡처 파이프라인 | ✅ 100% | CaptureThread, 30fps, 3-buffer 분기 |
| Direct2D 렌더링 | ✅ 100% | D2DRenderer, OverlayPainter, RenderThread |
| HUD 오버레이 | ✅ 100% | 타이머, 토스트, 통계 그래프, 휴식 알림, 캘리브레이션 |
| 캘리브레이션 시스템 | ✅ 100% | 5초 카운트다운 → neck_threshold 자동 설정 |
| 더미 AI (테스트용) | ✅ 100% | 30초 주기 시나리오, 모든 상태 커버 |
| 메인서버 HTTP 연동 | ✅ 95% | Auth/Session/Log 구현, 일부 기능 미연결 |
| JSONL 로그 배치 전송 | ✅ 100% | 10초 주기 + 이벤트 즉시 전송, keypoint 필드 포함 |
| 이벤트 감지/업로드 | ✅ 90% | 구조 완성, MP4 인코딩만 미완 |
| AI TCP — keypoint 전송 | ✅ 100% | Stage 1 완료, JPEG 전송 제거 |
| 로컬 자세 분석기 | ⚠️ 50% | OpenCV Haar placeholder, MediaPipe 대기 |
| 피드백 UI (복기 화면) | ❌ 0% | Stage 2 구현 필요 |
| POST /feedback | ❌ 0% | Stage 3 구현 필요 |
| 세션 종료 팝업 | ❌ 0% | Stage 3 구현 필요 |
| 서버 통계 화면 | ❌ 0% | 대시보드 UI 미구현 |
| 목표 설정 UI | ❌ 0% | GoalApi 연결 미구현 |
| 토큰 만료 자동처리 | ❌ 0% | 현재 디버그 출력만 |
| 이메일 인증 안내 | ❌ 0% | 회원가입 후 안내 메시지 미구현 |

---

## ✅ 1단계 완료 (2026-05-07)

client.txt 아키텍처 명세 1단계가 모두 구현되었다.

### 변경 요약

| 항목 | 이전 | 이후 |
|---|---|---|
| AI 서버 전송 데이터 | JPEG 이미지 (~100KB, 5fps) | keypoint JSON (~100B, 30fps 가능) |
| 전송 프로토콜 | 4-byte header + JSON + JPEG binary | 4-byte header + JSON only |
| 로컬 자세 분석 | 없음 (서버 처리) | OpenCV Haar cascade (placeholder) |
| AnalysisResult 필드 | ear, neck_angle, shoulder_diff | + head_yaw, head_pitch, face_detected, confidence 추가 |
| JSONL 로그 | 기본 keypoint만 | 신규 4개 필드 포함 |

### 관련 파일

| 파일 | 변경 내용 |
|---|---|
| `include/model/AnalysisResult.h` | head_yaw, head_pitch, face_detected, confidence 추가 |
| `include/analysis/LocalMediaPipePoseAnalyzer.h` | CascadeClassifier 멤버 추가 |
| `src/analysis/LocalMediaPipePoseAnalyzer.cpp` | OpenCV Haar 기반 keypoint 추출 구현 |
| `include/network/AiTcpClient.h` | pose_analyzer 멤버, send_keypoint_packet 선언 |
| `src/network/AiTcpClient.cpp` | JPEG 전송 → keypoint JSON 전송으로 교체 |
| `src/network/JsonlBatchUploader.cpp` | JSONL에 신규 필드 포함 |
| `src/analysis/DummyAnalysisGenerator.cpp` | 더미 시나리오에 신규 필드 값 추가 |

---

## 🔴 2단계 — 피드백 UI (복기 화면)

> client.txt § 2-2, § 2-3, § 2-4 참조

### 2-1. AnalysisResult confidence 파싱

`confidence` 필드는 `AnalysisResult.h`에 이미 추가되어 있고 `AiTcpClient::recv_result_packet()`에서 파싱 중이다.  
더미 모드에서도 `confidence = 1.0` 고정값이 들어간다.

**남은 작업**: 이벤트를 저장할 때 confidence 값을 함께 보관해야 한다.

- `PostureEvent` 구조체에 `double confidence = 1.0` 필드 추가
- `EventUploadThread`에서 AI 응답의 confidence를 PostureEvent에 기록
- JSONL 이벤트 라인에 `confidence` 필드 포함

### 2-2. 복기 화면 (공부 기록 화면)

세션 종료 후 이벤트 목록을 보는 화면. 현재 미구현.

**구현 목표 UI**:
```
[타임라인]
  14:30:05  ─  졸음 감지  ▶ 영상 재생
  ┌──────────────────────────────────┐
  │  이 판정이 맞나요?                │
  │  [맞아요]        [틀렸어요]       │
  └──────────────────────────────────┘
```

**구현 파일 목록**:

| 파일 | 내용 |
|---|---|
| `src/app/ReviewDlg.h/.cpp` | 복기 화면 다이얼로그 (신규) |
| `res/resource.h`, `StudySyncClient.rc` | IDD_REVIEW 리소스 추가 |
| `include/model/ReviewEvent.h` | 복기용 이벤트 뷰모델 (신규) |

**confidence별 표시 기준**:

| confidence 범위 | 표시 방식 |
|---|---|
| ≥ 0.85 | 피드백 버튼 숨김 (확실한 판정) |
| 0.70 ~ 0.85 | 피드백 버튼 표시 (세션 종료 시 목록에 포함) |
| < 0.70 | 피드백 버튼 표시 + 상단 정렬 (우선 표시) |

**"틀렸어요" 클릭 시 흐름**:
1. 최초 1회: 동의 팝업 표시  
   → "이 영상이 모델 개선 목적으로 개발팀에 전송됩니다. 동의하시겠습니까?"
2. 동의 시: POST /feedback 업로드 (3단계 구현)
3. clip_access를 `"local_only"` → `"uploaded_url"` 로 변경

**"맞아요"**: 아무 동작 없음

---

## 🟠 3단계 — 피드백 업로드 + 세션 종료 팝업

> client.txt § 2-5, § 2-6 참조

### 3-1. POST /feedback 업로드

**엔드포인트**: `POST /feedback`  
**형식**: `multipart/form-data`  
**인증**: `Authorization: Bearer {token}`

**전송 필드**:
| 필드 | 값 |
|---|---|
| `event_id` | `"event_1001_1746514205123_drowsy"` |
| `session_id` | `"1001"` |
| `model_pred` | `"drowsy"` |
| `user_feedback` | `"wrong"` |
| `consent_ver` | `"v1.0"` |
| `clip` | 3초 MP4 파일 |

**응답**: `{ "saved": true }`

**구현 파일**:

| 파일 | 내용 |
|---|---|
| `include/network/FeedbackApi.h` | POST /feedback 요청 래퍼 (신규) |
| `src/network/FeedbackApi.cpp` | multipart/form-data 전송 (신규) |
| `include/network/WinHttpClient.h` | `MultipartField` 구조체 + `post_multipart()` 선언 ✅ 완료 |
| `src/network/WinHttpClient.cpp` | `post_multipart()` + `build_multipart_body()` 구현 ✅ 완료 |
| `include/network/ConsentStore.h` | 동의 여부 로컬 저장 ✅ 완료 |
| `src/network/ConsentStore.cpp` | %APPDATA%/StudySync/consent.dat ✅ 완료 |

**WinHTTP multipart 구현 참고**:  
`WinHttpClient::post_multipart()` ✅ 완료 — `MultipartField` 구조체 + `build_multipart_body()` 포함.  
`include/network/WinHttpClient.h` / `src/network/WinHttpClient.cpp` 참조.

### 3-2. 세션 종료 팝업

```
[세션 종료 팝업]
오늘 불확실한 판정이 3건 있었어요.
확인하면 AI 정확도 개선에 도움이 됩니다.

[지금 확인하기]   [나중에]
```

- 세션 종료(`OnDestroy`)에서 confidence < 0.85 이벤트 개수 집계
- 1건 이상이면 팝업 표시
- "지금 확인하기" → ReviewDlg 열기, confidence 낮은 항목 상단 정렬

**구현 위치**: `CStudySyncClientView::OnDestroy()` 또는 `MainFrm`에서 세션 종료 후 처리

### 3-3. 동의 관리

- 최초 "틀렸어요" 클릭 시 동의 팝업 1회만 표시
- 동의 여부 `%APPDATA%/StudySync/consent.dat` 에 저장
- `consent_ver = "v1.0"` (문구 확정 전까지 v1.0 고정)
- 동의 철회 UI는 설정 화면에 추가 예정

---

## 🟡 기타 미구현 (별도 일정)

### A. 서버 통계 화면

현재 `/stats/today`는 `StatsApi` + `ServerStatsSnapshot`으로 HUD에 초안 연결된 상태.  
나머지 API는 대시보드 UI 작성 후 연결.

| API | 상태 |
|---|---|
| `/stats/today` | HUD 초안 연결 |
| `/stats/hourly` | 미연결 |
| `/stats/weekly` | 미연결 |
| `/stats/pattern` | 미연결 |

구현 방향: 별도 `StatsDlg` 다이얼로그 또는 확장 HUD 패널

### B. 목표 설정 UI

- `GoalApi`: `POST /goal`, `GET /goal` 연결
- 학습 목표 시간 입력 다이얼로그
- 세션 종료 시 달성 여부 표시
- 현재 `SessionEndResult.goal_achieved` 값은 수신 중 (디버그 출력만)

### C. 이메일 인증 안내

- `AuthApi::register_user()` 성공 후 서버가 인증 요구 시
- 클라이언트: `"이메일을 확인하세요"` 안내 메시지 표시
- 현재는 별도 처리 없음

### D. 토큰 만료 / 자동 재로그인

- HTTP 401 수신 시 동작 필요
- 현재: `WinHttpClient`에서 디버그 출력만 함
- 목표: `TokenStore::clear()` → `LoginDlg` 재표시
- 구현 위치: `WinHttpClient::post_json()` / `get()` 공통 처리

### E. 이벤트 클립 MP4 인코딩

- 현재: `EventShadowBuffer`에 JPEG 프레임 저장, Claim Check 구조 완성
- 목표: `cv::VideoWriter`로 3초 MP4 인코딩
- 팀 합의 필요: MP4 vs JPEG sequence (현재 미확정)
- 관련 파일: `src/network/LocalClaimCheckClipStore.cpp`

### F. AI 서버 실 연동 전환

```cpp
// include/network/ClientTransportConfig.h
bool use_dummy_ai = true;  // → false 로 변경
```

AI 서버 준비 완료 후 전환.  
`LocalMediaPipePoseAnalyzer`의 Haar cascade는 MediaPipe C++ SDK 확정 후 교체.

---

## 🔵 기술 부채

### ZMQ 레거시 코드 정리

다음 파일은 초기 설계 당시 ZMQ 방식 흔적으로, 현재 미구현 상태:

| 파일 | 상태 | 처리 방향 |
|---|---|---|
| `include/analysis/ZmqPoseAnalyzer.h` | 스텁 | TCP keypoint 방식으로 대체됨 → 제거 검토 |
| `src/analysis/ZmqPoseAnalyzer.cpp` | TODO만 존재 | 제거 검토 |
| `include/network/ZmqFrameSender.h` | 스텁 | 제거 검토 |
| `src/network/ZmqFrameSender.cpp` | TODO만 존재 | 제거 검토 |
| `include/network/ZmqSendThread.h` | 스텁 | 제거 검토 |
| `src/network/ZmqSendThread.cpp` | TODO만 존재 | 제거 검토 |
| `include/network/ZmqRecvThread.h` | 스텁 | 제거 검토 |
| `src/network/ZmqRecvThread.cpp` | TODO만 존재 | 제거 검토 |

→ AI팀 확인 후 ZMQ 방식이 필요 없으면 vcxproj에서 제거.

### LocalMediaPipePoseAnalyzer — MediaPipe 교체 대기

현재 `LocalMediaPipePoseAnalyzer`는 OpenCV Haar cascade 기반 placeholder다.  
정확도 한계:
- EAR: 눈 cascade 2개 감지 실패 시 고정값 0.32 반환
- head_yaw/pitch: face bbox 비율 기반 기하학적 근사 (±15° 오차)
- shoulder_diff: 머리 수평 오프셋 기반 근사 (실제 어깨 좌표 없음)

MediaPipe C++ SDK 확정 후 교체:
- `MediaPipePoseLandmarker` 33 landmark → 각 필드 직접 계산
- EAR: landmark 33개 중 눈 6점 좌표로 정확한 EAR 계산
- neck_angle: shoulder_mid ~ nose 벡터 각도
- shoulder_diff: left_shoulder.y − right_shoulder.y (픽셀)
- head_yaw/pitch: face normal vector 기반

---

## 🤝 팀 간 합의 필요 사항

| 항목 | 현황 | 담당 |
|---|---|---|
| **이벤트 클립 포맷** | MP4 vs JPEG sequence 미확정 | 클라이언트 + 메인서버 |
| **피드백 업로드 시점** | 즉시 전송 vs WiFi 연결 시 자동 전송 미확정 | 클라이언트 + 메인서버 |
| **동의 팝업 문구** | 법적 검토 전 임시 문구 사용 중 | 팀장 + 법적 검토 |
| **confidence 임계값** | 현재 0.70 / 0.85 초안 | 클라이언트 + AI 서버 |
| **AI 서버 응답 스키마 확정** | `ai_client_tcp_protocol.md` 초안 기준 | 클라이언트 + AI 서버 |
| **MediaPipe C++ SDK 통합 방식** | 미결정 (Bazel 빌드 vs prebuilt DLL) | 클라이언트 + AI 서버 |
| **ZMQ 코드 제거 여부** | AI팀 확인 필요 | 클라이언트 + AI 서버 |

---

## 📋 구현 우선순위 요약

```
[즉시 가능 — AI서버 불필요]
  ✦ PostureEvent에 confidence 필드 추가
  ✦ 토큰 만료(401) 자동 재로그인 처리
  ✦ 이메일 인증 안내 메시지

[2단계 — 복기 화면]
  ✦ ReviewDlg 다이얼로그 UI
  ✦ 피드백 버튼 (맞아요/틀렸어요)
  ✦ confidence 기반 항목 정렬

[3단계 — 피드백 업로드]
  ✅ WinHttpClient::post_multipart() 완료
  ✅ ConsentStore (동의 여부 저장) 완료
  ✦ FeedbackApi (POST /feedback multipart)
  ✦ ReviewDlg → FeedbackApi 연결
  ✦ 세션 종료 팝업 (불확실 판정 N건)

[AI팀 준비 후]
  ✦ use_dummy_ai = false 전환
  ✦ LocalMediaPipePoseAnalyzer → MediaPipe SDK 교체

[별도 일정]
  ✦ 이벤트 클립 MP4 인코딩
  ✦ 서버 통계 대시보드 화면
  ✦ 목표 설정 UI
  ✦ ZMQ 레거시 코드 제거
```

---

## 📁 현재 소스 구조 (참고)

```
client/
  include/
    analysis/   IPoseAnalyzer, LocalMediaPipePoseAnalyzer*, DummyAnalysisGenerator
                ZmqPoseAnalyzer (미구현, 제거 검토)
    alert/      AlertManager, AlertQueue, AlertDispatchThread
    capture/    CaptureThread
    core/       RingBuffer, ThreadSafeQueue, WorkerThreadPool
    event/      EventShadowBuffer, EventQueue, PostureEventDetector
    model/      AnalysisResult*, Frame, PostureEvent, Alert
                AnalysisResultBuffer, ToastBuffer, SessionStatsHistory
                ServerStatsSnapshot, User
    network/    WinHttpClient, AuthApi, SessionApi, StatsApi, TokenStore
                AiTcpClient*, HeartbeatClient
                EventUploadThread, HttpJsonlLogSink, JsonlBatchUploader*
                ClientTransportConfig, ClientTransportFactory
                IEventClipStore, IFrameSender, ILogSink
                LocalClaimCheckClipStore, LocalClipGarbageCollector
                ZmqFrameSender/RecvThread/SendThread (미구현, 제거 검토)
    render/     D2DRenderer, OverlayPainter, RenderThread
  src/
    app/        StudySyncClientApp, MainFrm, StudySyncClientView
                LoginDlg, RegisterDlg
    ...
  res/
    resource.h, StudySyncClient.rc

* 2026-05-07 Stage 1에서 변경된 파일
```
