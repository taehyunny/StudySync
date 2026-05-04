# StudySync MFC Client

MFC 기반 Windows 클라이언트입니다. 캡처, 렌더링, ZMQ 송수신, 이벤트 감지를 서로 다른 스레드로 분리하고, 캡처 프레임은 렌더/전송용 링버퍼와 이벤트 감지용 섀도 버퍼에 동시에 보관합니다.

## Thread Model

```text
CaptureThread
  -> RingBuffer<Frame, 8>       render buffer
  -> RingBuffer<Frame, 8>       send sampling buffer
  -> EventShadowBuffer<60>      recent frame window for event clips

RenderThread
  -> wait_pop(RingBuffer)
  -> Direct2D bitmap upload
  -> OverlayPainter

ZmqSendThread
  -> try_pop(RingBuffer)
  -> every 6th frame
  -> JPEG encode
  -> ZMQ PUSH to pose_server.py

ZmqRecvThread
  -> ZMQ PULL from pose_server.py
  -> PostureEventDetector.feed(result, shadow)
  -> EventQueue.push(snapshot)

EventUploadThread
  -> EventQueue.try_pop()
  -> encode event clip
  -> IEventClipStore claim check
  -> ILogSink event metadata upload

AlertManager
  -> local analysis alert rules
  -> server alert pass-through
  -> AlertQueue

AlertDispatchThread
  -> popup/toast
  -> Arduino serial command

JsonlBatchUploader
  -> newline-delimited JSON log batches
  -> HTTP POST application/x-ndjson

ClientTransportFactory
  -> builds ZMQ + HTTP JSONL + Claim Check from ClientTransportConfig
  -> make_transport_config(...) accepts runtime endpoints and policies

WorkerThreadPool
  -> 3 workers for short background tasks
  -> encode/upload/notification work items

IPoseAnalyzer
  -> ZmqPoseAnalyzer for pose_server.py
  -> LocalMediaPipePoseAnalyzer for future in-process MediaPipe
```

## Folder Layout

```text
client/
  StudySyncClient.sln
  StudySyncClient.vcxproj
  StudySyncClient.vcxproj.filters
  include/
    analysis/     IPoseAnalyzer, ZmqPoseAnalyzer, LocalMediaPipePoseAnalyzer
    alert/        AlertManager, AlertQueue, AlertDispatchThread
    core/          RingBuffer, ThreadSafeQueue, WorkerThreadPool
    model/         Frame, AnalysisResult, PostureEvent
    capture/       CaptureThread
    render/        RenderThread, OverlayPainter
    network/       ZmqSendThread, ZmqRecvThread, EventUploadThread, JsonlBatchUploader
    event/         EventShadowBuffer, EventQueue, PostureEventDetector
  src/
    app/           MFC App, MainFrame, View
    capture/
    render/
    network/
    event/
    model/
  res/
    resource.h
    StudySyncClient.rc
  tests/
```

## Build Notes

- Visual Studio 2022
- Windows SDK 10+
- MFC enabled in Visual Studio Installer
- OpenCV and ZeroMQ include/lib paths should be set in project properties or via `OPENCV_DIR` / `ZMQ_DIR` property sheets later.
- CMake build entrypoint: `client/CMakeLists.txt`
