# 2026-05-04 Client 작업 정리

작성자: 정태현

## 1. 오늘 작업 목표

기존 PyQt 중심 계획을 정리하고, StudySync client를 C++17 / MFC 기반 실시간 머신비전 클라이언트 구조로 재설계했다.

핵심 목표는 다음과 같다.

- 웹캠 30fps 이상 실시간 렌더링
- AI 서버에는 5fps 샘플 프레임만 ZMQ로 전송
- 이벤트 발생 시 최근 프레임 윈도우에서 클립 추출
- 메인서버에는 영상 원본이 아니라 JSONL 로그와 clip reference 중심으로 전송
- 렌더링, AI 전송, 이벤트 저장, 알림 처리를 서로 독립된 책임으로 분리

## 2. 현재 Client 파이프라인

```text
CaptureThread
  -> render_buffer_ : RingBuffer<Frame, 8>
  -> send_buffer_   : RingBuffer<Frame, 8>
  -> shadow_buffer_ : EventShadowBuffer<Frame, 60>

RenderThread
  -> wait_pop(render_buffer_)
  -> D2DRenderer::upload_and_render()
  -> Direct2D DrawBitmap

ZmqSendThread
  -> try_pop(send_buffer_)
  -> 6프레임마다 1장 JPEG 인코딩
  -> IFrameSender(ZmqFrameSender)

ZmqRecvThread
  -> AI 분석 결과 수신 예정
  -> PostureEventDetector
  -> EventQueue

EventUploadThread
  -> IEventClipStore(LocalClaimCheckClipStore)
  -> ILogSink(HttpJsonlLogSink)
```

## 3. 주요 설계 변경점

| 구분 | 기존 계획 | 현재 구조 |
|---|---|---|
| UI/렌더링 | Python PyQt5 / QLabel | C++17 / MFC / Direct2D |
| 실시간 렌더 | OpenCV 프레임을 UI 위젯에 표시 | RenderThread + D2DRenderer |
| AI 전송 | HTTP/base64 중심 계획 | ZMQ + JPEG memory payload |
| 로그 | JSON 단건 전송 | HTTP JSONL batch |
| 이벤트 영상 | 재학습 데이터 정도로만 계획 | Claim Check Pattern |
| 통신 책임 | API 클래스 중심 | IFrameSender / ILogSink / IEventClipStore |
| 알림 | 팝업 계획 | AlertManager / AlertQueue / AlertDispatchThread |

## 4. 오늘 구현한 핵심 파일

### 렌더링

- `include/render/D2DRenderer.h`
- `src/render/D2DRenderer.cpp`
- `include/render/RenderThread.h`
- `src/render/RenderThread.cpp`

역할:

- `RenderThread`는 렌더링 루프 담당
- `D2DRenderer`는 실제 Direct2D 리소스 생성, bitmap 업로드, 화면 출력 담당
- `StudySyncClientView`는 HWND 전달, resize 통지, GDI paint 억제 담당

### 프레임/이벤트 구조

- `include/core/RingBuffer.h`
- `include/model/Frame.h`
- `include/event/EventShadowBuffer.h`
- `include/event/PostureEventDetector.h`

역할:

- 렌더링용 프레임과 AI 전송용 프레임을 서로 다른 링버퍼로 분리
- 이벤트 클립 추출을 위해 최근 프레임을 별도 shadow buffer에 유지

### 통신 책임 분리

- `include/network/IFrameSender.h`
- `include/network/ILogSink.h`
- `include/network/IEventClipStore.h`
- `include/network/ClientTransportConfig.h`
- `include/network/ClientTransportFactory.h`
- `include/network/ZmqFrameSender.h`
- `include/network/HttpJsonlLogSink.h`
- `include/network/LocalClaimCheckClipStore.h`

역할:

- AI 프레임 전송은 `IFrameSender`
- JSONL 로그 전송은 `ILogSink`
- 이벤트 클립 저장은 `IEventClipStore`
- 실제 조합은 `ClientTransportConfig`로 주입

## 5. Direct2D 렌더링 안정화 1차 반영

오늘 렌더링 안정화를 위해 다음 내용을 반영했다.

### 5.1 빈 프레임 방어

이전에는 빈 프레임이 들어오면 렌더 스레드가 종료될 수 있었다.

현재는 빈 프레임이 들어오면 렌더 스레드를 죽이지 않고 다음 프레임을 기다린다.

```cpp
if (frame.mat.empty()) {
    continue;
}
```

### 5.2 BGRA 변환 버퍼 재사용

Direct2D는 BGRA bitmap을 사용하고 OpenCV 웹캠 프레임은 보통 BGR이다.

매 프레임 임시 `cv::Mat bgra`를 새로 만드는 대신, `D2DRenderer` 내부에 `bgra_buffer_`를 두고 `create()`로 재사용한다.

```cpp
bgra_buffer_.create(bgr.rows, bgr.cols, CV_8UC4);
cv::cvtColor(bgr, bgra_buffer_, cv::COLOR_BGR2BGRA);
```

### 5.3 Bitmap 재사용

프레임 크기가 바뀌지 않으면 `ID2D1Bitmap`을 새로 만들지 않는다.

```text
프레임 크기 동일:
  기존 ID2D1Bitmap에 CopyFromMemory

프레임 크기 변경:
  ID2D1Bitmap 재생성
```

### 5.4 GDI와 Direct2D 충돌 방지

`OnPaint()`는 `ValidateRect(nullptr)`만 수행하고, `OnEraseBkgnd()`는 `TRUE`를 반환한다.

목적:

- GDI가 Direct2D 화면을 덮지 않게 함
- 배경 지우기 과정에서 생기는 깜빡임 방지

## 6. 현재 진행도

| 항목 | 상태 | 진행률 |
|---|---|---|
| MFC 프로젝트 구조 | 완료 | 90% |
| CMake / VS 프로젝트 구성 | 초안 완료 | 75% |
| CaptureThread / RingBuffer 구조 | 초안 완료 | 75% |
| Direct2D 렌더링 기반 | 1차 구현 | 60% |
| ZMQ 프레임 전송 | 인터페이스/뼈대 | 45% |
| ZMQ 분석 결과 수신 | 인터페이스/뼈대 | 35% |
| 이벤트 감지 구조 | 초안 구현 | 60% |
| 이벤트 클립 저장 | Claim Check 뼈대 | 40% |
| HTTP JSONL 업로드 | JSONL 생성 뼈대 | 45% |
| Alert 팝업/Arduino | 구조만 구현 | 35% |
| 로그인/회원가입 UI | 미구현 | 0% |

전체적으로 보면 현재 client는 기능 완성 단계가 아니라, 실시간 처리 아키텍처와 렌더링 기반을 잡는 단계이다.

## 7. 남은 작업

우선순위 기준 다음 작업은 아래와 같다.

1. 실제 OpenCV 웹캠 출력 빌드 확인
2. `D2DRenderer`에 OverlayPainter 연결
3. ZMQ socket 실제 송신 구현
4. ZMQ 수신 JSON parsing 및 `AnalysisResult` 매핑
5. timestamp 기반 분석 결과 매칭
6. `EventShadowBuffer::snapshot_around(timestamp_ms)` 구현
7. 이벤트 클립 JPEG sequence 또는 MP4 저장 구현
8. WinHTTP 기반 JSONL POST 구현
9. 로그인/회원가입 UI 및 `SessionContext` 추가
10. AlertPopup 및 스트레칭 안내 UI 구현

## 8. 설계 요약

현재 client 설계의 핵심은 다음 세 가지다.

```text
1. 렌더링은 AI 서버를 기다리지 않는다.
2. 오래된 프레임은 버리고 최신 프레임을 유지한다.
3. 이벤트 영상은 상시 전송하지 않고 필요할 때만 저장/참조한다.
```

이 구조를 통해 실시간 화면 출력, AI 분석, 이벤트 클립, 로그 전송을 서로 독립적으로 발전시킬 수 있다.
