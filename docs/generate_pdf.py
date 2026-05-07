# -*- coding: utf-8 -*-
from fpdf import FPDF

class SlidePDF(FPDF):
    def __init__(self):
        super().__init__(orientation="L", unit="mm", format="A4")
        self.add_font("malgun", "", "C:/Windows/Fonts/malgun.ttf")
        self.add_font("malgun", "B", "C:/Windows/Fonts/malgunbd.ttf")
        self.set_auto_page_break(auto=False)
        # colors
        self.C_PRIMARY = (26, 86, 219)
        self.C_DARK = (30, 41, 59)
        self.C_ACCENT = (16, 185, 129)
        self.C_WARN = (249, 115, 22)
        self.C_GRAY = (148, 163, 184)
        self.C_WHITE = (255, 255, 255)
        self.C_BG_LIGHT = (241, 245, 249)
        self.C_BG_BLUE = (219, 234, 254)
        self.C_BG_GREEN = (209, 250, 229)
        self.C_BG_ORANGE = (255, 247, 237)
        self.C_BG_PURPLE = (237, 233, 254)
        self.C_BORDER = (203, 213, 225)

    def header(self):
        if self.page_no() > 1:
            self.set_font("malgun", "", 7)
            self.set_text_color(*self.C_GRAY)
            self.set_xy(230, 5)
            self.cell(60, 5, "StudySync Client Architecture", align="R")
            self.set_draw_color(*self.C_PRIMARY)
            self.line(10, 11, 287, 11)

    def footer(self):
        self.set_y(-10)
        self.set_font("malgun", "", 7)
        self.set_text_color(*self.C_GRAY)
        self.cell(0, 5, f"Page {self.page_no()}", align="C")

    def slide_title(self, text):
        self.set_font("malgun", "B", 20)
        self.set_text_color(*self.C_PRIMARY)
        self.cell(0, 14, text, new_x="LMARGIN", new_y="NEXT")
        self.ln(3)

    def subtitle(self, text):
        self.set_font("malgun", "B", 13)
        self.set_text_color(*self.C_DARK)
        self.cell(0, 10, text, new_x="LMARGIN", new_y="NEXT")
        self.ln(1)

    def body_text(self, text, bold=False, color=None, size=10):
        self.set_font("malgun", "B" if bold else "", size)
        self.set_text_color(*(color or self.C_DARK))
        self.multi_cell(0, 6, text, new_x="LMARGIN", new_y="NEXT")

    def code_block(self, lines):
        self.set_font("malgun", "", 8)
        self.set_text_color(*self.C_DARK)
        self.set_fill_color(*self.C_BG_LIGHT)
        x0 = self.get_x()
        for line in lines:
            self.set_x(x0)
            self.cell(267, 5, "  " + line, fill=True, new_x="LMARGIN", new_y="NEXT")

    def table_header(self, widths, texts):
        self.set_font("malgun", "B", 8)
        self.set_fill_color(*self.C_PRIMARY)
        self.set_text_color(*self.C_WHITE)
        x0 = self.get_x()
        for w, t in zip(widths, texts):
            self.cell(w, 7, t, border=1, fill=True, align="C")
        self.ln()
        self.set_x(x0)

    def table_row(self, widths, texts, fills=None, bolds=None, aligns=None):
        self.set_font("malgun", "", 8)
        self.set_text_color(*self.C_DARK)
        x0 = self.get_x()
        h = 7
        for i, (w, t) in enumerate(zip(widths, texts)):
            if fills and fills[i]:
                self.set_fill_color(*fills[i])
                fill = True
            else:
                fill = False
            if bolds and i < len(bolds) and bolds[i]:
                self.set_font("malgun", "B", 8)
            else:
                self.set_font("malgun", "", 8)
            al = (aligns[i] if aligns else None) or "L"
            self.cell(w, h, t, border=1, fill=fill, align=al)
        self.ln()
        self.set_x(x0)


def build_pdf():
    pdf = SlidePDF()

    # ── Slide 1: Title ──
    pdf.add_page()
    pdf.ln(55)
    pdf.set_font("malgun", "B", 36)
    pdf.set_text_color(*pdf.C_PRIMARY)
    pdf.cell(0, 20, "StudySync", align="C", new_x="LMARGIN", new_y="NEXT")
    pdf.set_font("malgun", "", 16)
    pdf.set_text_color(*pdf.C_GRAY)
    pdf.cell(0, 12, "MFC C++ Desktop Client Architecture", align="C", new_x="LMARGIN", new_y="NEXT")
    pdf.ln(8)
    pdf.set_font("malgun", "", 11)
    pdf.set_text_color(*pdf.C_PRIMARY)
    pdf.cell(0, 8, "MediaPipe  ·  Direct2D  ·  WinHTTP  ·  ZeroMQ", align="C", new_x="LMARGIN", new_y="NEXT")
    pdf.ln(30)
    pdf.set_font("malgun", "", 10)
    pdf.set_text_color(*pdf.C_GRAY)
    pdf.cell(0, 8, "4조  |  클라이언트 담당: 정태현  |  2026.05", align="C", new_x="LMARGIN", new_y="NEXT")

    # ── Slide 2: Overview ──
    pdf.add_page()
    pdf.slide_title("프로젝트 개요")
    pdf.body_text("웹캠 기반 실시간 자세 분석 및 공부 집중도 관리 데스크톱 앱")
    pdf.ln(3)
    pdf.subtitle("핵심 아키텍처 결정 — MediaPipe 분석을 클라이언트 로컬에서 수행")
    w = [50, 108, 109]
    pdf.table_header(w, ["항목", "기존 (서버 분석)", "현재 (로컬 분석)"])
    pdf.table_row(w, ["응답 지연", "네트워크 왕복 50~300ms", "사실상 0ms"], [None, None, pdf.C_BG_GREEN], [True, False, False])
    pdf.table_row(w, ["네트워크 사용량", "5fps × 프레임 크기", "JSON 수십 바이트/5초"], [None, None, pdf.C_BG_GREEN], [True, False, False])
    pdf.table_row(w, ["동시접속 부하", "유저 수 × 5fps 프레임", "서버 부하 없음"], [None, None, pdf.C_BG_GREEN], [True, False, False])
    pdf.table_row(w, ["프라이버시", "영상이 서버를 거침", "영상이 기기 밖으로 나가지 않음"], [None, None, pdf.C_BG_GREEN], [True, False, False])
    pdf.ln(6)
    pdf.subtitle("기술 스택")
    w4 = [66, 67, 67, 67]
    pdf.table_header(w4, ["MFC C++ (Win32)", "Direct2D (GPU)", "WinHTTP (OS API)", "ZeroMQ (IPC)"])
    pdf.table_row(w4,
        ["로우레벨 UI / 윈도우 메시지", "GPU 가속 렌더링", "외부 라이브러리 없이 HTTP", "AI 서버 비동기 통신"],
        [pdf.C_BG_BLUE]*4, aligns=["C","C","C","C"])

    # ── Slide 3: Thread Pipeline ──
    pdf.add_page()
    pdf.slide_title("스레드 파이프라인 — 8개 동시 실행")
    pdf.code_block([
        "CaptureThread (std::thread, 30fps)",
        "  │",
        "  ├─▶ render_buffer_ (RingBuffer<Frame, 8>)",
        "  │       └─ RenderThread ─▶ D2DRenderer ─▶ 화면 출력",
        "  │             wait_pop()     BGR→BGRA / CopyFromMemory / DrawBitmap",
        "  │",
        "  ├─▶ send_buffer_ (RingBuffer<Frame, 8>)",
        "  │       └─ ZmqSendThread ─▶ ZMQ PUSH ─▶ pose_server.py (MediaPipe)",
        "  │             try_pop() / 매 6프레임",
        "  │",
        "  └─▶ shadow_buffer_ (RingBuffer<Frame, 60>)",
        "          └─ 이벤트 발생 시 snapshot() → JPEG 시퀀스 저장",
        "",
        "ZmqRecvThread ── ZMQ PULL ──▶ PostureEventDetector ──▶ EventQueue",
        "EventUploadThread ── EventQueue ──▶ ClipStore + LogSink (JSONL)",
        "AlertDispatchThread ── AlertQueue ──▶ 팝업 / Arduino",
        "HeartbeatClient × 2 ── AI/Main 서버 연결 상태 모니터링",
        "LocalClipGarbageCollector ── 만료 클립 자동 삭제 (30분 간격)",
    ])
    pdf.ln(4)
    w5 = [40, 55, 60, 52, 60]
    pdf.table_header(w5, ["버퍼", "자료구조", "용도", "속도", "소비자"])
    pdf.table_row(w5, ["render_buffer_", "RingBuffer<Frame,8>", "화면 출력용", "30fps (wait_pop)", "RenderThread"], bolds=[True,False,False,False,False])
    pdf.table_row(w5, ["send_buffer_", "RingBuffer<Frame,8>", "AI 서버 전송용", "5fps (try_pop)", "ZmqSendThread"], bolds=[True,False,False,False,False])
    pdf.table_row(w5, ["shadow_buffer_", "RingBuffer<Frame,60>", "이벤트 스냅샷", "최근 2초 보관", "PostureEventDetector"], bolds=[True,False,False,False,False])

    # ── Slide 4: GPU Pipeline ──
    pdf.add_page()
    pdf.slide_title("GPU 렌더링 파이프라인 — Direct2D")
    pdf.subtitle("D2DRenderer — 프레임당 처리 흐름")
    pdf.code_block([
        "upload_and_render(bgr)",
        "  │",
        "  ├─ ① update_bgra_buffer(bgr)",
        "  │     cv::cvtColor(BGR → BGRA)       // D2D는 4채널만 수용",
        "  │     bgra_buffer_ 재사용              // 사이즈 같으면 재할당 없음",
        "  │",
        "  ├─ ② ensure_bitmap(w, h)",
        "  │     사이즈 변경 시에만 CreateBitmap() // GPU 비트맵 재생성",
        "  │",
        "  ├─ ③ CopyFromMemory(bgra.data)",
        "  │     CPU → GPU 픽셀 전송 (재할당 없이 덮어쓰기)",
        "  │",
        "  └─ ④ BeginDraw → Clear(Black) → DrawBitmap → EndDraw",
        "        GPU가 직접 HWND에 렌더링",
    ])
    pdf.ln(4)
    pdf.subtitle("스레드 안전성 해결")
    w4s = [65, 65, 70, 67]
    pdf.table_header(w4s, ["문제", "해결", "방법", "위치"])
    pdf.table_row(w4s, ["UI/렌더 스레드 충돌", "D2D를 렌더스레드에서만 사용", "init + render 같은 스레드", "RenderThread::run()"])
    pdf.table_row(w4s, ["GDI 배경 깜빡임", "OnEraseBkgnd → TRUE", "GDI 배경 paint 억제", "StudySyncClientView"])
    pdf.table_row(w4s, ["창 리사이즈 불일치", "notify_resize → mutex", "UI에서 통지, 렌더에서 적용", "D2DRenderer"])
    pdf.table_row(w4s, ["디바이스 소실", "D2DERR_RECREATE_TARGET", "EndDraw 반환값 → 자동 재생성", "recreate_target()"])

    # ── Slide 5: Claim Check ──
    pdf.add_page()
    pdf.slide_title("Claim Check 패턴 — 이벤트 영상 처리")
    pdf.body_text("큰 영상 데이터는 외부 저장소에 보관, JSONL에는 위치표(clip_ref)만 기록")
    pdf.ln(3)
    pdf.code_block([
        "① 웹캠 30fps → ② EventShadowBuffer (RAM 링버퍼, 최근 60프레임)",
        "                       │",
        "③ AI/Main 서버가       │",
        "   이벤트 타임스탬프 ──┤",
        "   전송                │",
        "                       ▼",
        "④ 타임스탬프 기준 이전/이후 프레임 추출 (shadow.snapshot())",
        "                       │",
        "                       ▼",
        "⑤ ClipStore에 저장 (JPEG 시퀀스) → 로컬 디스크",
        "                       │",
        "                       ▼",
        "⑥ ClipRef 생성 ── {clip_id, uri, format, retention_days}",
        "                       │",
        "                       ▼",
        "⑦ JSONL로 메타데이터 전송 (영상 데이터 미포함) → Main Server",
        "                       │",
        "                       ▼",
        "⑧ Main Server가 clip_ref로 영상 조회/재생",
    ])
    pdf.ln(5)
    w2 = [133, 134]
    pdf.set_font("malgun", "B", 9)
    pdf.set_fill_color(*pdf.C_BG_GREEN)
    pdf.set_text_color(*pdf.C_ACCENT)
    pdf.cell(133, 8, "  [O]  영상 데이터는 JSONL에 직접 넣지 않음", border=1, fill=True)
    pdf.cell(134, 8, "  [O]  JSONL에는 위치표(clip_ref)만 포함", border=1, fill=True)
    pdf.ln()

    # ── Slide 6: Network / Auth ──
    pdf.add_page()
    pdf.slide_title("통신 구조 — WinHTTP + JWT 인증")
    pdf.subtitle("인증 흐름")
    pdf.code_block([
        "회원가입:  POST /auth/register  {email, password, name}",
        "                                  │",
        "                          서버: bcrypt(비밀번호) → DB 저장",
        "",
        "로그인:   POST /auth/login     {email, password}",
        "                                  │",
        "                          서버: bcrypt 검증 → JWT 발급",
        "                                  │",
        "            ┌─────────────────────────────────────┐",
        "            │  TokenStore.save(jwt)               │ → %APPDATA%/token.dat",
        "            │  WinHttpClient.set_token(jwt)       │ → 모든 요청에 Bearer 헤더",
        "            └─────────────────────────────────────┘",
    ])
    pdf.ln(3)
    pdf.subtitle("WinHTTP API 호출 순서")
    w3 = [65, 100, 102]
    pdf.table_header(w3, ["WinHTTP API", "역할", "호출 시점"])
    pdf.table_row(w3, ["WinHttpOpen", "세션 생성 (UA 설정)", "앱 시작 시 1회"])
    pdf.table_row(w3, ["WinHttpConnect", "host:port TCP 연결", "요청마다"])
    pdf.table_row(w3, ["WinHttpAddRequestHeaders", "Authorization: Bearer + Content-Type", "토큰 있으면 자동 주입"])
    pdf.table_row(w3, ["WinHttpSendRequest", "HTTP 요청 + JSON body 전송", "요청마다"])
    pdf.table_row(w3, ["WinHttpReceiveResponse", "응답 수신 + 상태코드 + body 읽기", "요청마다"])

    # ── Slide 7: File Structure ──
    pdf.add_page()
    pdf.slide_title("레이어별 파일 구조 — SRP 원칙")
    w4f = [35, 20, 100, 112]
    pdf.table_header(w4f, ["레이어", "파일 수", "주요 파일", "책임"])
    pdf.table_row(w4f, ["model/", "5개", "Frame, AnalysisResult, User, PostureEvent, Alert", "순수 데이터 구조체 (메서드/API/UI 없음)"], [pdf.C_BG_BLUE, None, None, None], [True])
    pdf.table_row(w4f, ["core/", "3개", "RingBuffer, ThreadSafeQueue, WorkerThreadPool", "범용 동기화 원형 (비즈니스 로직 무관)"], [pdf.C_BG_BLUE, None, None, None], [True])
    pdf.table_row(w4f, ["capture/", "1개", "CaptureThread", "OpenCV 웹캠 캡처 → 3개 버퍼 동시 push"], [pdf.C_BG_GREEN, None, None, None], [True])
    pdf.table_row(w4f, ["render/", "3개", "D2DRenderer, RenderThread, OverlayPainter", "GPU 렌더링 전담 (UI 의존성 없음)"], [pdf.C_BG_GREEN, None, None, None], [True])
    pdf.table_row(w4f, ["event/", "3개", "EventQueue, EventShadowBuffer, PostureEventDetector", "자세 이벤트 판정 + 스냅샷 추출"], [pdf.C_BG_ORANGE, None, None, None], [True])
    pdf.table_row(w4f, ["alert/", "3개", "AlertQueue, AlertManager, AlertDispatchThread", "경고/휴식 판정 → 팝업 + Arduino"], [pdf.C_BG_ORANGE, None, None, None], [True])
    pdf.table_row(w4f, ["network/", "17개", "WinHttpClient, AuthApi, TokenStore, ZmqSend/Recv, ...", "통신 전담 (인터페이스 + 구현체 분리)"], [pdf.C_BG_PURPLE, None, None, None], [True])
    pdf.table_row(w4f, ["analysis/", "3개", "IPoseAnalyzer, ZmqPoseAnalyzer, LocalMediaPipe...", "MediaPipe 분석 추상화 (교체 가능)"], [pdf.C_BG_PURPLE, None, None, None], [True])
    pdf.table_row(w4f, ["app/", "6개", "StudySyncClientApp, MainFrm, View, pch, ...", "MFC 부트스트랩 + 스레드 오케스트레이션"], [pdf.C_BG_LIGHT, None, None, None], [True])
    pdf.ln(4)
    pdf.body_text("총 44개 파일  |  헤더 .h + 구현 .cpp 분리  |  include/ + src/ 디렉토리 구조", bold=True, color=pdf.C_PRIMARY)

    # ── Slide 8: Status ──
    pdf.add_page()
    pdf.slide_title("현재 진행 상황")
    w3s = [80, 107, 80]
    pdf.table_header(w3s, ["항목", "세부 내용", "상태"])
    done = [pdf.C_BG_GREEN]
    prog = [pdf.C_BG_ORANGE]
    plan = [pdf.C_BG_LIGHT]
    pdf.table_row(w3s, ["RingBuffer 동기화", "push / wait_pop / try_pop / snapshot / close", "[완성]"], [None, None, pdf.C_BG_GREEN], [True, False, True], ["L","L","C"])
    pdf.table_row(w3s, ["웹캠 캡처 파이프라인", "CaptureThread → 3개 버퍼 동시 push (fps config)", "[완성]"], [None, None, pdf.C_BG_GREEN], [True, False, True], ["L","L","C"])
    pdf.table_row(w3s, ["GPU 렌더링 (Direct2D)", "D2DRenderer + RenderThread + 리사이즈 + 디바이스 복구", "[완성]"], [None, None, pdf.C_BG_GREEN], [True, False, True], ["L","L","C"])
    pdf.table_row(w3s, ["인터페이스 추상화", "IFrameSender, ILogSink, IEventClipStore, IPoseAnalyzer", "[완성]"], [None, None, pdf.C_BG_GREEN], [True, False, True], ["L","L","C"])
    pdf.table_row(w3s, ["Claim Check 이벤트 파이프라인", "EventShadowBuffer → ClipStore → ClipRef → JSONL", "[완성]"], [None, None, pdf.C_BG_GREEN], [True, False, True], ["L","L","C"])
    pdf.table_row(w3s, ["서버 연결 관리", "HeartbeatClient + ReconnectPolicy (지수 백오프)", "[완성]"], [None, None, pdf.C_BG_GREEN], [True, False, True], ["L","L","C"])
    pdf.table_row(w3s, ["WinHTTP 통신 인프라", "WinHttpClient 싱글톤 + JWT 헤더 자동 주입", "[완성]"], [None, None, pdf.C_BG_GREEN], [True, False, True], ["L","L","C"])
    pdf.table_row(w3s, ["로그인/회원가입 API", "AuthApi + TokenStore (%APPDATA% 저장)", "[완성]"], [None, None, pdf.C_BG_GREEN], [True, False, True], ["L","L","C"])
    pdf.table_row(w3s, ["ZMQ 실제 송수신", "ZmqRecvThread — JSON 파싱 + 분석결과 연결", "[진행중]"], [None, None, pdf.C_BG_ORANGE], [True, False, True], ["L","L","C"])
    pdf.table_row(w3s, ["JSONL HTTP 전송", "flush_to_http() — WinHttpClient 연동 예정", "[진행중]"], [None, None, pdf.C_BG_ORANGE], [True, False, True], ["L","L","C"])
    pdf.table_row(w3s, ["MediaPipe 연동", "Python pose_server.py → ZMQ → AnalysisResult", "[예정]"], [None, None, pdf.C_BG_LIGHT], [True, False, True], ["L","L","C"])
    pdf.table_row(w3s, ["오버레이 렌더링", "OverlayPainter — 랜드마크/각도/상태 표시", "[예정]"], [None, None, pdf.C_BG_LIGHT], [True, False, True], ["L","L","C"])
    pdf.table_row(w3s, ["로그인 UI", "MFC 로그인/회원가입 화면 + AuthApi 연동", "[예정]"], [None, None, pdf.C_BG_LIGHT], [True, False, True], ["L","L","C"])
    pdf.table_row(w3s, ["알림 팝업 / Arduino", "AlertDispatchThread → UI 마셜링 + 시리얼", "[예정]"], [None, None, pdf.C_BG_LIGHT], [True, False, True], ["L","L","C"])

    # ── Slide 9: ERD + Endpoints ──
    pdf.add_page()
    pdf.slide_title("DB ERD — 서버 연동 구조")
    pdf.code_block([
        "USER ────1:1──── GOAL        (하루 목표 / 휴식 주기)",
        "  │",
        "  ├───1:N──── SESSION     (공부 세션 시작~종료)",
        "  │                │",
        "  │                ├──1:N── FOCUS_LOG    (5초 단위 집중도 점수)",
        "  │                └──1:N── POSTURE_LOG  (목 각도 / 어깨 기울기)",
        "  │",
        "  └───1:N──── TRAIN_DATA  (Phase2 재학습용 랜드마크)",
    ])
    pdf.ln(5)
    pdf.subtitle("클라이언트 → 서버 엔드포인트")
    w4e = [30, 80, 50, 107]
    pdf.table_header(w4e, ["메서드", "엔드포인트", "주기", "데이터"])
    pdf.table_row(w4e, ["POST", "/auth/register", "1회", "email + password + name"])
    pdf.table_row(w4e, ["POST", "/auth/login", "1회", "email + password → JWT 응답"])
    pdf.table_row(w4e, ["POST", "/session/start", "세션 시작", "user_id, start_time"])
    pdf.table_row(w4e, ["POST", "/focus", "5초마다", "focus_score, state (JSONL)"])
    pdf.table_row(w4e, ["POST", "/posture", "5초마다", "neck_angle, shoulder_diff (JSONL)"])
    pdf.table_row(w4e, ["POST", "/events", "이벤트 발생", "event + clip_ref (JSONL)"])
    pdf.ln(3)
    pdf.body_text("※  클라이언트는 비밀번호 평문 전송 (HTTPS 전제)  |  bcrypt 해싱은 서버에서만 수행", bold=True, color=pdf.C_WARN)

    # ── Slide 10: Next Steps ──
    pdf.add_page()
    pdf.slide_title("향후 계획")
    pdf.ln(3)
    w3n = [40, 110, 117]
    pdf.table_header(w3n, ["단계", "작업", "설명"])
    pdf.table_row(w3n, ["Phase 1-A", "MediaPipe Python 서버 (pose_server.py)", "ZMQ PULL → MediaPipe Pose+FaceMesh → JSON PUSH"], [pdf.C_BG_GREEN, None, None], [True])
    pdf.table_row(w3n, ["Phase 1-B", "ZmqRecvThread 실제 연동", "JSON 파싱 → AnalysisResult → PostureEventDetector"], [pdf.C_BG_GREEN, None, None], [True])
    pdf.table_row(w3n, ["Phase 1-C", "OverlayPainter 랜드마크 오버레이", "D2D 위에 33개 랜드마크 + 각도 + 상태 표시"], [pdf.C_BG_GREEN, None, None], [True])
    pdf.table_row(w3n, ["Phase 1-D", "로그인 UI + 세션 관리", "MFC 로그인 화면 → AuthApi → 세션 시작/종료"], [pdf.C_BG_BLUE, None, None], [True])
    pdf.table_row(w3n, ["Phase 1-E", "JSONL flush + 통계 API 연동", "WinHttpClient로 flush_to_http 구현 + GET /stats"], [pdf.C_BG_BLUE, None, None], [True])
    pdf.table_row(w3n, ["Phase 2", "라벨링 UI + AI 재학습", "집중/딴짓/졸음 라벨 → TRAIN_DATA → PyTorch MLP"], [pdf.C_BG_PURPLE, None, None], [True])
    pdf.ln(25)
    pdf.set_font("malgun", "B", 28)
    pdf.set_text_color(*pdf.C_PRIMARY)
    pdf.cell(0, 20, "Thank You", align="C")

    # ── Output ──
    out = "C:/Users/lms/Desktop/StudySync_Architecture.pdf"
    pdf.output(out)
    print(f"Created: {out}")

if __name__ == "__main__":
    build_pdf()
