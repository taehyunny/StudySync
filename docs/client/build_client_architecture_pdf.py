from reportlab.lib import colors
from reportlab.lib.enums import TA_CENTER, TA_LEFT
from reportlab.lib.pagesizes import A4
from reportlab.lib.styles import ParagraphStyle, getSampleStyleSheet
from reportlab.lib.units import cm
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont
from reportlab.platypus import (
    KeepTogether,
    PageBreak,
    Paragraph,
    Preformatted,
    SimpleDocTemplate,
    Spacer,
    Table,
    TableStyle,
)


OUT = "StudySync/docs/client/StudySync_Client_Architecture_Report_정태현.pdf"
FONT = "NotoSansKR"
FONT_BOLD = "NotoSansKRBold"


def register_fonts():
    regular = r"C:\Windows\Fonts\NotoSansKR-VF.ttf"
    bold = r"C:\Windows\Fonts\malgunbd.ttf"
    pdfmetrics.registerFont(TTFont(FONT, regular))
    pdfmetrics.registerFont(TTFont(FONT_BOLD, bold))


def styles():
    base = getSampleStyleSheet()
    return {
        "title": ParagraphStyle(
            "title",
            parent=base["Title"],
            fontName=FONT_BOLD,
            fontSize=22,
            leading=30,
            alignment=TA_CENTER,
            textColor=colors.HexColor("#1F4E79"),
            spaceAfter=12,
        ),
        "subtitle": ParagraphStyle(
            "subtitle",
            parent=base["Normal"],
            fontName=FONT,
            fontSize=13,
            leading=20,
            alignment=TA_CENTER,
            textColor=colors.HexColor("#444444"),
            spaceAfter=20,
        ),
        "h1": ParagraphStyle(
            "h1",
            parent=base["Heading1"],
            fontName=FONT_BOLD,
            fontSize=15,
            leading=21,
            textColor=colors.HexColor("#1F4E79"),
            spaceBefore=14,
            spaceAfter=7,
        ),
        "h2": ParagraphStyle(
            "h2",
            parent=base["Heading2"],
            fontName=FONT_BOLD,
            fontSize=12,
            leading=17,
            textColor=colors.HexColor("#2F75B5"),
            spaceBefore=10,
            spaceAfter=5,
        ),
        "body": ParagraphStyle(
            "body",
            parent=base["BodyText"],
            fontName=FONT,
            fontSize=9.5,
            leading=14.2,
            alignment=TA_LEFT,
            spaceAfter=5,
        ),
        "small": ParagraphStyle(
            "small",
            parent=base["BodyText"],
            fontName=FONT,
            fontSize=8.4,
            leading=12.2,
            spaceAfter=4,
        ),
        "cell": ParagraphStyle(
            "cell",
            parent=base["BodyText"],
            fontName=FONT,
            fontSize=8.2,
            leading=11.5,
            wordWrap="CJK",
        ),
        "cell_header": ParagraphStyle(
            "cell_header",
            parent=base["BodyText"],
            fontName=FONT_BOLD,
            fontSize=8.6,
            leading=12,
            textColor=colors.white,
            alignment=TA_CENTER,
            wordWrap="CJK",
        ),
        "code": ParagraphStyle(
            "code",
            parent=base["Code"],
            fontName=FONT,
            fontSize=7.4,
            leading=9.7,
            textColor=colors.HexColor("#1F2937"),
        ),
    }


def p(text, style):
    return Paragraph(text.replace("\n", "<br/>"), style)


def tbl(headers, rows, widths, st):
    data = [[p(h, st["cell_header"]) for h in headers]]
    for row in rows:
        data.append([p(str(v), st["cell"]) for v in row])
    table = Table(data, colWidths=widths, repeatRows=1, hAlign="CENTER")
    table.setStyle(
        TableStyle(
            [
                ("BACKGROUND", (0, 0), (-1, 0), colors.HexColor("#1F4E79")),
                ("TEXTCOLOR", (0, 0), (-1, 0), colors.white),
                ("GRID", (0, 0), (-1, -1), 0.35, colors.HexColor("#B7C9D6")),
                ("VALIGN", (0, 0), (-1, -1), "MIDDLE"),
                ("LEFTPADDING", (0, 0), (-1, -1), 5),
                ("RIGHTPADDING", (0, 0), (-1, -1), 5),
                ("TOPPADDING", (0, 0), (-1, -1), 4),
                ("BOTTOMPADDING", (0, 0), (-1, -1), 4),
                ("ROWBACKGROUNDS", (0, 1), (-1, -1), [colors.white, colors.HexColor("#F7FAFC")]),
            ]
        )
    )
    return table


def code_block(text, st):
    table = Table(
        [[Preformatted(text.strip("\n"), st["code"])]],
        colWidths=[17.0 * cm],
        hAlign="CENTER",
    )
    table.setStyle(
        TableStyle(
            [
                ("BACKGROUND", (0, 0), (-1, -1), colors.HexColor("#F3F6FA")),
                ("BOX", (0, 0), (-1, -1), 0.4, colors.HexColor("#C9D6E2")),
                ("LEFTPADDING", (0, 0), (-1, -1), 8),
                ("RIGHTPADDING", (0, 0), (-1, -1), 8),
                ("TOPPADDING", (0, 0), (-1, -1), 7),
                ("BOTTOMPADDING", (0, 0), (-1, -1), 7),
            ]
        )
    )
    return table


def bullet(text, st):
    return Paragraph("• " + text, st["body"])


def footer(canvas, doc):
    canvas.saveState()
    canvas.setFont(FONT, 8)
    canvas.setFillColor(colors.HexColor("#666666"))
    canvas.drawString(1.7 * cm, 1.0 * cm, "StudySync Client Architecture Report")
    canvas.drawRightString(19.3 * cm, 1.0 * cm, f"{doc.page}")
    canvas.restoreState()


def build():
    register_fonts()
    st = styles()
    story = []

    story.append(Spacer(1, 1.2 * cm))
    story.append(Paragraph("StudySync Client Architecture Report", st["title"]))
    story.append(Paragraph("실시간 머신비전 비동기 파이프라인 설계 및 진행 현황", st["subtitle"]))
    story.append(
        tbl(
            ["항목", "내용"],
            [
                ["작성자", "정태현"],
                ["대상 모듈", "StudySync / client"],
                ["작성일", "2026-05-04"],
                ["목적", "팀원에게 클라이언트 설계 의도, 파이프라인 구조, 현재 구현 진행도를 공유"],
                ["현재 단계", "MFC/C++ 클라이언트 아키텍처 뼈대 구현 및 통신 책임 분리 완료"],
            ],
            [3.0 * cm, 13.5 * cm],
            st,
        )
    )
    story.append(PageBreak())

    story.append(Paragraph("1. 설계 목표", st["h1"]))
    story.append(p("StudySync 클라이언트는 웹캠 기반 실시간 자세 분석 앱으로, 화면 렌더링과 AI 분석, 로그 전송, 이벤트 클립 저장을 서로 분리하여 네트워크 지연이 화면 출력에 영향을 주지 않도록 설계한다.", st["body"]))
    story.append(p("핵심 목표는 30fps 이상의 부드러운 로컬 렌더링, 5fps 샘플링 기반 AI 서버 분석, 이벤트 발생 시점의 클립 추출, JSONL 기반 경량 로그 전송이다.", st["body"]))

    story.append(Paragraph("2. 전체 파이프라인", st["h1"]))
    story.append(
        code_block(
            """
CaptureThread
  -> render_buffer_ : RingBuffer<Frame, 8>        실시간 화면 렌더링
  -> send_buffer_   : RingBuffer<Frame, 8>        AI 서버 5fps 샘플링 전송
  -> shadow_buffer_ : EventShadowBuffer<Frame,60> 이벤트 클립 추출용 최근 프레임 보관

RenderThread
  -> wait_pop(render_buffer_)
  -> Direct2D bitmap upload
  -> OverlayPainter

ZmqSendThread
  -> try_pop(send_buffer_)
  -> 6프레임마다 1장 JPEG 인코딩
  -> IFrameSender(ZmqFrameSender)

ZmqRecvThread
  -> pose_server.py 분석 결과 수신
  -> PostureEventDetector
  -> EventQueue

EventUploadThread
  -> IEventClipStore(LocalClaimCheckClipStore)
  -> ILogSink(HttpJsonlLogSink)
            """,
            st,
        )
    )

    story.append(Paragraph("3. 핵심 설계 결정", st["h1"]))
    story.append(
        tbl(
            ["영역", "설계", "이유"],
            [
                ["렌더링", "Direct2D 기반 2D 렌더링", "MFC/GDI보다 영상 프레임과 오버레이를 안정적으로 출력하기 쉬움"],
                ["프레임 흐름", "고정 크기 RingBuffer", "오래된 프레임을 덮어써 실시간성을 유지하고 지연 누적을 방지"],
                ["AI 분석", "ZMQ 기반 5fps 샘플링", "분석 서버 지연이 렌더링을 막지 않도록 분리"],
                ["로그", "HTTP JSONL", "분석 결과와 이벤트 메타데이터를 줄 단위로 배치 전송"],
                ["영상 클립", "Claim Check Pattern", "이벤트 영상 원본은 큐에 직접 넣지 않고 저장 경로만 로그로 전송"],
                ["확장성", "인터페이스 기반 통신 주입", "ZMQ/HTTP/저장 방식을 함수 인자와 설정값으로 교체 가능"],
            ],
            [2.6 * cm, 4.5 * cm, 9.4 * cm],
            st,
        )
    )

    story.append(Paragraph("4. 통신 책임 분리", st["h1"]))
    story.append(p("통신은 하나의 거대한 Sender로 묶지 않고 목적별 인터페이스로 분리했다. 이 방식은 단일 책임 원칙을 지키며, 추후 통신 방식 변경 시 핵심 파이프라인을 수정하지 않고 구현체만 교체할 수 있게 한다.", st["body"]))
    story.append(
        code_block(
            """
IFrameSender
  - Frame 전송 책임
  - 현재 구현체: ZmqFrameSender

ILogSink
  - JSONL 로그와 이벤트 메타데이터 전송 책임
  - 현재 구현체: HttpJsonlLogSink

IEventClipStore
  - 이벤트 클립 저장 및 참조 URI 반환 책임
  - 현재 구현체: LocalClaimCheckClipStore

ClientTransportConfig
  - ZMQ endpoint, JSONL endpoint, clip directory, sample interval, JPEG quality 설정
            """,
            st,
        )
    )

    story.append(Paragraph("5. 현재 구현 진행도", st["h1"]))
    story.append(
        tbl(
            ["구분", "상태", "진행률", "설명"],
            [
                ["프로젝트 구조", "완료", "90%", "MFC App/MainFrame/View, Visual Studio 프로젝트, CMake 진입점 구성"],
                ["프레임 버퍼", "완료", "85%", "RingBuffer, ThreadSafeQueue, render/send/shadow buffer 분리"],
                ["캡처 스레드", "초안 구현", "70%", "OpenCV VideoCapture 기반 캡처 루프와 3개 버퍼 push 구조 구현"],
                ["렌더 스레드", "초안 구현", "55%", "wait_pop 기반 렌더 스레드 구조 구현, Direct2D 업로더 세부 구현 필요"],
                ["AI 전송", "뼈대 구현", "55%", "ZmqSendThread, IFrameSender, ZmqFrameSender 구조 구현. 실제 ZMQ socket 송신 TODO"],
                ["AI 수신", "뼈대 구현", "40%", "ZmqRecvThread와 EventDetector 연결 구조 구현. JSON parsing 및 socket 수신 TODO"],
                ["이벤트 감지", "초안 구현", "65%", "목 각도/EAR streak 기반 이벤트 트리거 구조 구현"],
                ["이벤트 클립", "뼈대 구현", "45%", "Claim Check 인터페이스 구현. 실제 JPEG sequence/MP4 인코딩 TODO"],
                ["JSONL 로그", "초안 구현", "60%", "AnalysisResult/Event metadata JSONL 생성 구조 구현. WinHTTP POST TODO"],
                ["알림", "뼈대 구현", "45%", "AlertManager, AlertQueue, AlertDispatchThread 구성. MFC 팝업/Arduino Serial TODO"],
                ["빌드 검증", "부분 확인", "35%", ".vcxproj XML 및 파일 참조 검증 완료. MSBuild/CMake 실제 빌드 미실행"],
            ],
            [2.7 * cm, 2.2 * cm, 1.7 * cm, 9.9 * cm],
            st,
        )
    )

    story.append(Paragraph("6. 현재 코드 기준 주요 파일", st["h1"]))
    story.append(
        tbl(
            ["폴더", "역할", "대표 파일"],
            [
                ["include/core", "공용 동기화/스레드 기반", "RingBuffer.h, ThreadSafeQueue.h, WorkerThreadPool.h"],
                ["include/model", "데이터 모델", "Frame.h, AnalysisResult.h, PostureEvent.h, Alert.h"],
                ["include/capture", "웹캠 캡처", "CaptureThread.h"],
                ["include/render", "화면 렌더링", "RenderThread.h, OverlayPainter.h"],
                ["include/network", "ZMQ/HTTP JSONL/Claim Check 통신", "IFrameSender.h, ILogSink.h, IEventClipStore.h, ClientTransportConfig.h"],
                ["include/event", "이벤트 감지 및 클립 윈도우", "EventShadowBuffer.h, PostureEventDetector.h"],
                ["include/alert", "팝업/아두이노 알림", "AlertManager.h, AlertDispatchThread.h"],
                ["include/analysis", "분석 엔진 추상화", "IPoseAnalyzer.h, ZmqPoseAnalyzer.h, LocalMediaPipePoseAnalyzer.h"],
            ],
            [3.0 * cm, 4.3 * cm, 9.2 * cm],
            st,
        )
    )

    story.append(Paragraph("7. 장점", st["h1"]))
    for item in [
        "렌더링과 AI 분석이 분리되어 AI 서버가 느려져도 화면 출력은 계속 유지된다.",
        "링버퍼를 사용하므로 지연이 누적되지 않고 최신 프레임 중심으로 복구된다.",
        "AI 서버 전송은 30fps 전체가 아니라 5fps 샘플링이므로 네트워크/인코딩 부하를 줄인다.",
        "이벤트 영상은 상시 전송하지 않고 이벤트 발생 시 Claim Check 방식으로 처리하여 메인서버 트래픽을 억제한다.",
        "통신 책임을 인터페이스로 분리하여 ZMQ, HTTP JSONL, 클립 저장 방식을 설정 기반으로 교체할 수 있다.",
    ]:
        story.append(bullet(item, st))

    story.append(Paragraph("8. 우려되는 병목 및 보완 계획", st["h1"]))
    story.append(
        tbl(
            ["리스크", "영향", "보완 계획"],
            [
                ["AI 결과 지연", "랜드마크 오버레이가 현재 화면보다 늦게 표시될 수 있음", "Frame/AnalysisResult timestamp 기반 매칭 캐시 구현"],
                ["이벤트 클립 시점 불일치", "AI 응답이 늦으면 이벤트 순간보다 뒤쪽 프레임이 추출될 수 있음", "snapshot_around(timestamp_ms) 방식으로 개선"],
                ["CPU->GPU 업로드 비용", "고해상도에서 렌더 프레임 타임 증가 가능", "D2DFrameUploader에서 ID2D1Bitmap 및 BGRA 변환 버퍼 재사용"],
                ["JPEG 인코딩 비용", "전송 스레드 CPU 점유율 증가 가능", "전송 해상도 축소, JPEG quality 조정, WorkerThreadPool 활용"],
                ["스레드 종료", "blocking wait로 종료 시 deadlock 가능", "ThreadSafeQueue close(), optional 반환 정책 도입"],
                ["업로드 실패", "JSONL/클립 메타데이터 유실 가능", "로컬 spool 파일 저장 후 재전송 정책 추가"],
            ],
            [3.5 * cm, 5.2 * cm, 7.8 * cm],
            st,
        )
    )

    story.append(Paragraph("9. 다음 작업 계획", st["h1"]))
    story.append(
        tbl(
            ["우선순위", "작업", "목표"],
            [
                ["1", "Direct2D 렌더링 리소스 구현", "D2DDeviceResources, D2DFrameUploader 추가 및 실제 화면 출력 안정화"],
                ["2", "ZMQ 실제 송수신 구현", "ZmqFrameSender socket 송신, ZmqRecvThread JSON parsing 연결"],
                ["3", "timestamp 기반 분석 결과 매칭", "렌더 프레임과 AI 결과의 시간 불일치 최소화"],
                ["4", "EventShadowBuffer 개선", "snapshot_around(timestamp_ms) 구현"],
                ["5", "HTTP JSONL 업로드 구현", "WinHTTP 기반 application/x-ndjson POST"],
                ["6", "Claim Check 클립 저장 구현", "JPEG sequence 또는 MP4 저장 후 clip_ref 전송"],
                ["7", "알림 출력 구현", "MFC 팝업/toast 및 Arduino serial command 연결"],
                ["8", "CMake/VS 빌드 검증", "OpenCV, ZeroMQ 경로 설정 후 Debug x64 빌드 확인"],
            ],
            [2.0 * cm, 5.5 * cm, 9.0 * cm],
            st,
        )
    )

    story.append(Paragraph("10. 요약", st["h1"]))
    story.append(p("현재 client는 최종 기능 완성 단계가 아니라, 실시간 머신비전 앱의 핵심 아키텍처를 먼저 고정한 상태이다. 캡처, 렌더링, AI 전송, 결과 수신, 이벤트 추출, 로그 전송, 알림 처리를 독립 컴포넌트로 나누었고, 통신은 ZMQ + HTTP JSONL + Claim Check 조합으로 설계했다.", st["body"]))
    story.append(p("진행률을 전체 기준으로 보면 약 55~60% 수준이다. 구조와 인터페이스는 상당 부분 정리되었고, 다음 단계는 Direct2D 실제 렌더링, ZMQ socket 연결, WinHTTP 업로드, 이벤트 클립 인코딩을 채워 넣는 구현 단계이다.", st["body"]))

    doc = SimpleDocTemplate(
        OUT,
        pagesize=A4,
        rightMargin=1.7 * cm,
        leftMargin=1.7 * cm,
        topMargin=1.55 * cm,
        bottomMargin=1.45 * cm,
        title="StudySync Client Architecture Report",
        author="정태현",
    )
    doc.build(story, onFirstPage=footer, onLaterPages=footer)


if __name__ == "__main__":
    build()
