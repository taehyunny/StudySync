# 4조 StudySync - 정태현(클라이언트)

## 1. 담당 범위

정태현은 StudySync의 Windows 클라이언트를 담당한다. MFC 기반 UI를 구현하고 OpenCV 웹캠 화면을 30fps 이상으로 렌더링하며, 샘플링 프레임을 ZMQ로 AI 서버에 전송한다. AI 분석 결과를 화면에 오버레이하고 메인서버에 집중도와 자세 로그를 저장한다.

## 2. 주요 기능

| 기능 | 설명 |
|---|---|
| 로그인/회원가입 UI | 이메일, 비밀번호 입력 및 JWT 기반 로그인 흐름 |
| 웹캠 렌더링 | OpenCV 프레임을 MFC/Direct2D 화면에 표시 |
| AI 서버 전송 | 30fps 중 샘플링 프레임만 ZMQ로 전송 |
| 자세 오버레이 | 랜드마크, 목 각도, 교정 가이드 표시 |
| 집중도 게이지 | 0~100점 Progress Bar 및 상태 뱃지 표시 |
| 알림 팝업 | 자세 경고, 휴식 권장, 과집중 알림 |
| 목표 설정 UI | 목표 시간, 휴식 주기, 휴식 시간 설정 |
| 통계 대시보드 | 오늘 통계, 시간대별 그래프, 주간 리포트 표시 |
| 라벨링 버튼 | Phase 2 재학습용 집중/딴짓/졸음 데이터 수집 |

## 3. 현재 클라이언트 구조

```text
CaptureThread
  -> render_buffer_
  -> send_buffer_
  -> shadow_buffer_

RenderThread
  -> D2DRenderer

ZmqSendThread
  -> IFrameSender

EventUploadThread
  -> IEventClipStore
  -> ILogSink
```

## 4. 서버 연동

| 대상 | 연동 내용 |
|---|---|
| AI 서버 | ZMQ 기반 5fps 프레임 전송, 분석 결과 수신 |
| 메인서버 | 로그인, 목표, 세션, 집중도 로그, 자세 로그, 통계 조회 |

## 5. 제출물

- MFC 클라이언트 프로젝트
- Direct2D 렌더링 모듈
- ZMQ 프레임 전송 모듈
- 메인서버 API 연동 모듈
- 이벤트 클립 및 JSONL 로그 전송 구조
- 알림/스트레칭 안내 UI

