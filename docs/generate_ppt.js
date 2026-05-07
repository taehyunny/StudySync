const fs = require("fs");
const {
  Document, Packer, Paragraph, TextRun, Table, TableRow, TableCell,
  Header, Footer, AlignmentType, PageOrientation, PageBreak,
  HeadingLevel, BorderStyle, WidthType, ShadingType,
  PageNumber, LevelFormat, TabStopType, TabStopPosition
} = require("docx");

// ── 컬러 팔레트 ───────────────────────────
const C = {
  primary:   "1A56DB",
  dark:      "1E293B",
  accent:    "10B981",
  warn:      "F97316",
  danger:    "EF4444",
  purple:    "8B5CF6",
  bg_light:  "F1F5F9",
  bg_blue:   "DBEAFE",
  bg_green:  "D1FAE5",
  bg_orange: "FFF7ED",
  bg_purple: "EDE9FE",
  white:     "FFFFFF",
  gray:      "94A3B8",
  border:    "CBD5E1",
};

// ── 유틸리티 ──────────────────────────────
const W = 15840; // Letter landscape width DXA
const H = 12240; // Letter landscape height DXA
const CONTENT_W = W - 2880; // 12960 with 1-inch margins

const thinBorder = { style: BorderStyle.SINGLE, size: 1, color: C.border };
const borders = { top: thinBorder, bottom: thinBorder, left: thinBorder, right: thinBorder };
const noBorders = {
  top: { style: BorderStyle.NONE, size: 0 },
  bottom: { style: BorderStyle.NONE, size: 0 },
  left: { style: BorderStyle.NONE, size: 0 },
  right: { style: BorderStyle.NONE, size: 0 },
};

function cell(text, opts = {}) {
  const { bold, size, color, fill, align, width, font, colSpan } = opts;
  const cellOpts = {
    borders: opts.noBorder ? noBorders : borders,
    margins: { top: 60, bottom: 60, left: 120, right: 120 },
    children: [
      new Paragraph({
        alignment: align || AlignmentType.LEFT,
        children: [
          new TextRun({
            text,
            bold: bold || false,
            size: size || 20,
            color: color || C.dark,
            font: font || "Arial",
          }),
        ],
      }),
    ],
  };
  if (fill) cellOpts.shading = { fill, type: ShadingType.CLEAR };
  if (width) cellOpts.width = { size: width, type: WidthType.DXA };
  if (colSpan) cellOpts.columnSpan = colSpan;
  return new TableCell(cellOpts);
}

function slideTitle(text) {
  return new Paragraph({
    spacing: { before: 200, after: 300 },
    children: [
      new TextRun({ text, bold: true, size: 40, color: C.primary, font: "Arial" }),
    ],
  });
}

function subtitle(text) {
  return new Paragraph({
    spacing: { before: 100, after: 200 },
    children: [
      new TextRun({ text, bold: true, size: 28, color: C.dark, font: "Arial" }),
    ],
  });
}

function body(text, opts = {}) {
  return new Paragraph({
    spacing: { before: 60, after: 60 },
    alignment: opts.align || AlignmentType.LEFT,
    children: [
      new TextRun({
        text,
        size: opts.size || 22,
        color: opts.color || C.dark,
        bold: opts.bold || false,
        font: "Arial",
      }),
    ],
  });
}

function codeBlock(lines) {
  return lines.map(
    (line) =>
      new Paragraph({
        spacing: { before: 20, after: 20 },
        children: [
          new TextRun({
            text: line,
            size: 18,
            color: C.dark,
            font: "Consolas",
          }),
        ],
      })
  );
}

function spacer(h = 200) {
  return new Paragraph({ spacing: { before: h, after: 0 }, children: [] });
}

function pageBreakPara() {
  return new Paragraph({ children: [new PageBreak()] });
}

// ── 슬라이드 정의 ─────────────────────────

function slide1_title() {
  return [
    spacer(1800),
    new Paragraph({
      alignment: AlignmentType.CENTER,
      spacing: { after: 200 },
      children: [
        new TextRun({ text: "StudySync", bold: true, size: 72, color: C.primary, font: "Arial" }),
      ],
    }),
    new Paragraph({
      alignment: AlignmentType.CENTER,
      spacing: { after: 100 },
      children: [
        new TextRun({
          text: "MFC C++ Desktop Client Architecture",
          size: 32,
          color: C.gray,
          font: "Arial",
        }),
      ],
    }),
    spacer(200),
    new Paragraph({
      alignment: AlignmentType.CENTER,
      children: [
        new TextRun({
          text: "MediaPipe · Direct2D · WinHTTP · ZeroMQ",
          size: 24,
          color: C.primary,
          font: "Arial",
        }),
      ],
    }),
    spacer(800),
    new Paragraph({
      alignment: AlignmentType.CENTER,
      children: [
        new TextRun({ text: "4조  |  클라이언트 담당: 정태현", size: 22, color: C.gray, font: "Arial" }),
      ],
    }),
    new Paragraph({
      alignment: AlignmentType.CENTER,
      children: [
        new TextRun({ text: "2026.05", size: 20, color: C.gray, font: "Arial" }),
      ],
    }),
    pageBreakPara(),
  ];
}

function slide2_overview() {
  return [
    slideTitle("프로젝트 개요"),
    body("웹쮨 기반 실시간 자세 분석 및 공부 집중도 관리 데스크톱 앱"),
    spacer(100),
    subtitle("핵심 아키텍처 결정"),
    new Table({
      width: { size: CONTENT_W, type: WidthType.DXA },
      columnWidths: [3000, 4980, 4980],
      rows: [
        new TableRow({
          children: [
            cell("항목", { bold: true, fill: C.primary, color: C.white, width: 3000 }),
            cell("기존 (서버 분석)", { bold: true, fill: C.primary, color: C.white, width: 4980 }),
            cell("현재 (로컬 분석)", { bold: true, fill: C.primary, color: C.white, width: 4980 }),
          ],
        }),
        new TableRow({
          children: [
            cell("응답 지연", { bold: true, width: 3000 }),
            cell("네트워크 왕복 50~300ms", { width: 4980 }),
            cell("사실상 0ms", { fill: C.bg_green, width: 4980 }),
          ],
        }),
        new TableRow({
          children: [
            cell("네트워크 사용량", { bold: true, width: 3000 }),
            cell("5fps × 프레임 크기", { width: 4980 }),
            cell("JSON 수십 바이트/5초", { fill: C.bg_green, width: 4980 }),
          ],
        }),
        new TableRow({
          children: [
            cell("프라이버시", { bold: true, width: 3000 }),
            cell("영상이 서버를 거침", { width: 4980 }),
            cell("영상이 기기 밖으로 나가지 않음", { fill: C.bg_green, width: 4980 }),
          ],
        }),
      ],
    }),
    spacer(200),
    subtitle("기술 스택"),
    new Table({
      width: { size: CONTENT_W, type: WidthType.DXA },
      columnWidths: [3240, 3240, 3240, 3240],
      rows: [
        new TableRow({
          children: [
            cell("MFC C++ (Win32)", { bold: true, fill: C.bg_blue, align: AlignmentType.CENTER, width: 3240 }),
            cell("Direct2D (GPU)", { bold: true, fill: C.bg_blue, align: AlignmentType.CENTER, width: 3240 }),
            cell("WinHTTP (OS API)", { bold: true, fill: C.bg_blue, align: AlignmentType.CENTER, width: 3240 }),
            cell("ZeroMQ (IPC)", { bold: true, fill: C.bg_blue, align: AlignmentType.CENTER, width: 3240 }),
          ],
        }),
        new TableRow({
          children: [
            cell("로우레벨 UI\n윈도우 메시지 펌프", { align: AlignmentType.CENTER, width: 3240 }),
            cell("GPU 가속 렌더링\n웹쮨 프레임 표시", { align: AlignmentType.CENTER, width: 3240 }),
            cell("외부 라이브러리 없이\nHTTP 통신", { align: AlignmentType.CENTER, width: 3240 }),
            cell("AI 서버와\n비동기 프레임 전송", { align: AlignmentType.CENTER, width: 3240 }),
          ],
        }),
      ],
    }),
    pageBreakPara(),
  ];
}

function slide3_pipeline() {
  return [
    slideTitle("스레드 파이프라인 — 8개 동시 실행"),
    spacer(50),
    ...codeBlock([
      "  CaptureThread (std::thread, 30fps)",
      "    │",
      "    ├─▶ render_buffer_ (RingBuffer<Frame, 8>)",
      "    │       └─ RenderThread ─▶ D2DRenderer ─▶ 화면 출력",
      "    │             wait_pop()     BGR→BGRA      DrawBitmap",
      "    │                            CopyFromMemory",
      "    │",
      "    ├─▶ send_buffer_ (RingBuffer<Frame, 8>)",
      "    │       └─ ZmqSendThread ─▶ ZMQ PUSH ─▶ pose_server.py",
      "    │             try_pop()                  MediaPipe",
      "    │             매 6프레임",
      "    │",
      "    └─▶ shadow_buffer_ (RingBuffer<Frame, 60>)",
      "            └─ 이벤트 발생 시 snapshot() → JPEG 시퀀스 저장",
      "",
      "  ZmqRecvThread ─ ZMQ PULL ─▶ PostureEventDetector ─▶ EventQueue",
      "  EventUploadThread ─ EventQueue ─▶ ClipStore + LogSink (JSONL)",
      "  AlertDispatchThread ─ AlertQueue ─▶ 팝업 / Arduino",
      "  HeartbeatClient × 2 ─ AI/Main 서버 연결 상태 모니터링",
      "  LocalClipGarbageCollector ─ 만료 클립 자동 삭제 (30분 간격)",
    ]),
    spacer(200),
    new Table({
      width: { size: CONTENT_W, type: WidthType.DXA },
      columnWidths: [2160, 2700, 2700, 2700, 2700],
      rows: [
        new TableRow({
          children: [
            cell("버퍼 종류", { bold: true, fill: C.primary, color: C.white, width: 2160 }),
            cell("자료구조", { bold: true, fill: C.primary, color: C.white, width: 2700 }),
            cell("용도", { bold: true, fill: C.primary, color: C.white, width: 2700 }),
            cell("속도", { bold: true, fill: C.primary, color: C.white, width: 2700 }),
            cell("소비자", { bold: true, fill: C.primary, color: C.white, width: 2700 }),
          ],
        }),
        new TableRow({
          children: [
            cell("render_buffer_", { bold: true, width: 2160 }),
            cell("RingBuffer<Frame,8>", { font: "Consolas", size: 18, width: 2700 }),
            cell("화면 출력용", { width: 2700 }),
            cell("30fps (wait_pop)", { width: 2700 }),
            cell("RenderThread", { width: 2700 }),
          ],
        }),
        new TableRow({
          children: [
            cell("send_buffer_", { bold: true, width: 2160 }),
            cell("RingBuffer<Frame,8>", { font: "Consolas", size: 18, width: 2700 }),
            cell("AI 서버 전송용", { width: 2700 }),
            cell("5fps (try_pop)", { width: 2700 }),
            cell("ZmqSendThread", { width: 2700 }),
          ],
        }),
        new TableRow({
          children: [
            cell("shadow_buffer_", { bold: true, width: 2160 }),
            cell("RingBuffer<Frame,60>", { font: "Consolas", size: 18, width: 2700 }),
            cell("이벤트 스냅샷용", { width: 2700 }),
            cell("최근 2초 보관", { width: 2700 }),
            cell("PostureEventDetector", { width: 2700 }),
          ],
        }),
      ],
    }),
    pageBreakPara(),
  ];
}

function slide4_gpu() {
  return [
    slideTitle("GPU 렌더링 파이프라인 — Direct2D"),
    spacer(50),
    subtitle("D2DRenderer — 프레임당 처리 흐름"),
    ...codeBlock([
      "  upload_and_render(bgr)",
      "    │",
      "    ├─ ① update_bgra_buffer(bgr)",
      "    │     cv::cvtColor(BGR → BGRA)      // D2D는 4채널만 수용",
      "    │     bgra_buffer_ 재사용 (Mat::create)  // 사이즈 같으면 재할당 없음",
      "    │",
      "    ├─ ② ensure_bitmap(w, h)",
      "    │     사이즈 변경 시에만 CreateBitmap()  // GPU 비트맵 재생성",
      "    │     같은 사이즈면 기존 비트맵 재사용",
      "    │",
      "    ├─ ③ CopyFromMemory(bgra.data)",
      "    │     CPU → GPU 픽셀 전송 (재할당 없이 덮어쓰기)",
      "    │",
      "    └─ ④ BeginDraw → Clear(Black) → DrawBitmap → EndDraw",
      "          GPU가 직접 화면에 렌더링 (HwndRenderTarget)",
    ]),
    spacer(200),
    subtitle("스레드 안전성"),
    new Table({
      width: { size: CONTENT_W, type: WidthType.DXA },
      columnWidths: [3240, 3240, 3240, 3240],
      rows: [
        new TableRow({
          children: [
            cell("문제", { bold: true, fill: C.bg_orange, width: 3240 }),
            cell("해결", { bold: true, fill: C.bg_green, width: 3240 }),
            cell("방법", { bold: true, fill: C.bg_blue, width: 3240 }),
            cell("위치", { bold: true, fill: C.bg_blue, width: 3240 }),
          ],
        }),
        new TableRow({
          children: [
            cell("UI스레드와 렌더스레드 충돌", { width: 3240 }),
            cell("D2D를 렌더스레드에서만 사용", { width: 3240 }),
            cell("init + render 모두 같은 스레드", { width: 3240 }),
            cell("RenderThread::run()", { font: "Consolas", size: 18, width: 3240 }),
          ],
        }),
        new TableRow({
          children: [
            cell("GDI 배경 지우기로 깜빡임", { width: 3240 }),
            cell("OnEraseBkgnd → TRUE", { width: 3240 }),
            cell("GDI 배경 paint 억제", { width: 3240 }),
            cell("StudySyncClientView", { font: "Consolas", size: 18, width: 3240 }),
          ],
        }),
        new TableRow({
          children: [
            cell("창 크기 변경 시 비트맵 불일치", { width: 3240 }),
            cell("notify_resize → mutex 보호", { width: 3240 }),
            cell("UI스레드에서 통지, 렌더스레드에서 적용", { width: 3240 }),
            cell("D2DRenderer", { font: "Consolas", size: 18, width: 3240 }),
          ],
        }),
        new TableRow({
          children: [
            cell("그래픽 디바이스 소실", { width: 3240 }),
            cell("D2DERR_RECREATE_TARGET 감지", { width: 3240 }),
            cell("EndDraw 반환값 확인 → 자동 재생성", { width: 3240 }),
            cell("recreate_target()", { font: "Consolas", size: 18, width: 3240 }),
          ],
        }),
      ],
    }),
    pageBreakPara(),
  ];
}

function slide5_claimcheck() {
  return [
    slideTitle("Claim Check 패턴 — 이벤트 영상 처리"),
    spacer(50),
    body("큰 영상 데이터는 외부 저장소에 보관하고, JSONL에는 위치표(clip_ref)만 기록"),
    spacer(100),
    ...codeBlock([
      "  ① 웹쮨 60fps → ② EventShadowBuffer (RAM 링버퍼, 최근 60프레임)",
      "                         │",
      "  ③ AI/Main 서버가   │",
      "     이벤트 타임스탬프 ──┤",
      "     전송              │",
      "                         ↓",
      "  ④ 타임스탬프 기준으로 이전/이후 구간 프레임 추출 (snapshot)",
      "                         │",
      "                         ↓",
      "  ⑤ ClipStore에 저장 (JPEG 시퀀스) → 로컬 디스크 / NAS / S3",
      "                         │",
      "                         ↓",
      "  ⑥ ClipRef(위치표) 생성 ─ {clip_id, uri, format, retention_days}",
      "                         │",
      "                         ↓",
      "  ⑦ JSONL으로 메타데이터 전송 (영상 미포함) → Main Server",
      "                         │",
      "                         ↓",
      "  ⑧ Main Server가 clip_ref로 영상 조회/재생",
    ]),
    spacer(200),
    new Table({
      width: { size: CONTENT_W, type: WidthType.DXA },
      columnWidths: [6480, 6480],
      rows: [
        new TableRow({
          children: [
            cell("✅  영상 데이터는 JSONL에 직접 넣지 않음", {
              bold: true, fill: C.bg_green, width: 6480,
            }),
            cell("✅  JSONL에는 위치표(clip_ref)만 포함", {
              bold: true, fill: C.bg_green, width: 6480,
            }),
          ],
        }),
      ],
    }),
    pageBreakPara(),
  ];
}

function slide6_network() {
  return [
    slideTitle("통신 구조 — WinHTTP + JWT 인증"),
    spacer(50),
    subtitle("인증 흐름"),
    ...codeBlock([
      "  회원가입:  POST /auth/register  {email, password, name}",
      "                                    │",
      "                            서버: bcrypt(비밀번호) → DB 저장",
      "",
      "  로그인:   POST /auth/login     {email, password}",
      "                                    │",
      "                            서버: bcrypt 검증 → JWT 발급",
      "                                    │",
      "                            ▼",
      "              ┌─────────────────────────────────┐",
      "              │  클라이언트                     │",
      "              │  TokenStore.save(jwt)           │  → %APPDATA%/token.dat",
      "              │  WinHttpClient.set_token(jwt)   │  → 이후 모든 요청에 Bearer 헤더",
      "              └─────────────────────────────────┘",
    ]),
    spacer(100),
    subtitle("WinHttpClient — 싱글톤 HTTP 인프라"),
    new Table({
      width: { size: CONTENT_W, type: WidthType.DXA },
      columnWidths: [3240, 4860, 4860],
      rows: [
        new TableRow({
          children: [
            cell("WinHTTP API", { bold: true, fill: C.primary, color: C.white, width: 3240 }),
            cell("역할", { bold: true, fill: C.primary, color: C.white, width: 4860 }),
            cell("호출 시점", { bold: true, fill: C.primary, color: C.white, width: 4860 }),
          ],
        }),
        new TableRow({
          children: [
            cell("WinHttpOpen", { font: "Consolas", size: 18, width: 3240 }),
            cell("세션 생성 (UA 설정)", { width: 4860 }),
            cell("앱 시작 시 1회", { width: 4860 }),
          ],
        }),
        new TableRow({
          children: [
            cell("WinHttpConnect", { font: "Consolas", size: 18, width: 3240 }),
            cell("host:port TCP 연결", { width: 4860 }),
            cell("요청마다", { width: 4860 }),
          ],
        }),
        new TableRow({
          children: [
            cell("WinHttpAddRequestHeaders", { font: "Consolas", size: 18, width: 3240 }),
            cell("Authorization: Bearer + Content-Type", { width: 4860 }),
            cell("토큰 있으면 자동 주입", { width: 4860 }),
          ],
        }),
        new TableRow({
          children: [
            cell("WinHttpSendRequest", { font: "Consolas", size: 18, width: 3240 }),
            cell("HTTP 요청 + JSON body 전송", { width: 4860 }),
            cell("요청마다", { width: 4860 }),
          ],
        }),
        new TableRow({
          children: [
            cell("WinHttpReceiveResponse", { font: "Consolas", size: 18, width: 3240 }),
            cell("응답 수신 + 상태코드 + body 읽기", { width: 4860 }),
            cell("요청마다", { width: 4860 }),
          ],
        }),
      ],
    }),
    pageBreakPara(),
  ];
}

function slide7_layers() {
  return [
    slideTitle("레이어별 파일 구조 — SRP 원칙"),
    spacer(50),
    new Table({
      width: { size: CONTENT_W, type: WidthType.DXA },
      columnWidths: [2160, 2700, 4000, 4100],
      rows: [
        new TableRow({
          children: [
            cell("레이어", { bold: true, fill: C.primary, color: C.white, width: 2160 }),
            cell("파일 수", { bold: true, fill: C.primary, color: C.white, width: 2700 }),
            cell("주요 파일", { bold: true, fill: C.primary, color: C.white, width: 4000 }),
            cell("책임", { bold: true, fill: C.primary, color: C.white, width: 4100 }),
          ],
        }),
        new TableRow({
          children: [
            cell("model/", { bold: true, fill: C.bg_blue, width: 2160 }),
            cell("5개", { align: AlignmentType.CENTER, width: 2700 }),
            cell("Frame, AnalysisResult, User,\nPostureEvent, Alert", { size: 18, width: 4000 }),
            cell("순수 데이터 구조체만\n메서드, API, UI 코드 없음", { width: 4100 }),
          ],
        }),
        new TableRow({
          children: [
            cell("core/", { bold: true, fill: C.bg_blue, width: 2160 }),
            cell("3개", { align: AlignmentType.CENTER, width: 2700 }),
            cell("RingBuffer, ThreadSafeQueue,\nWorkerThreadPool", { size: 18, width: 4000 }),
            cell("범용 동기화 원형\n비즈니스 로직 무관", { width: 4100 }),
          ],
        }),
        new TableRow({
          children: [
            cell("capture/", { bold: true, fill: C.bg_green, width: 2160 }),
            cell("1개", { align: AlignmentType.CENTER, width: 2700 }),
            cell("CaptureThread", { size: 18, width: 4000 }),
            cell("OpenCV 웹쮨 캡처\n3개 버퍼에 동시 push", { width: 4100 }),
          ],
        }),
        new TableRow({
          children: [
            cell("render/", { bold: true, fill: C.bg_green, width: 2160 }),
            cell("3개", { align: AlignmentType.CENTER, width: 2700 }),
            cell("D2DRenderer, RenderThread,\nOverlayPainter", { size: 18, width: 4000 }),
            cell("GPU 렌더링 전담\nUI 의존성 없음", { width: 4100 }),
          ],
        }),
        new TableRow({
          children: [
            cell("event/", { bold: true, fill: C.bg_orange, width: 2160 }),
            cell("3개", { align: AlignmentType.CENTER, width: 2700 }),
            cell("EventQueue, EventShadowBuffer,\nPostureEventDetector", { size: 18, width: 4000 }),
            cell("자세 이벤트 판정 + 스냅샷", { width: 4100 }),
          ],
        }),
        new TableRow({
          children: [
            cell("alert/", { bold: true, fill: C.bg_orange, width: 2160 }),
            cell("3개", { align: AlignmentType.CENTER, width: 2700 }),
            cell("AlertQueue, AlertManager,\nAlertDispatchThread", { size: 18, width: 4000 }),
            cell("경고/휴식/강제휴식 판정\n팝업 + Arduino 출력", { width: 4100 }),
          ],
        }),
        new TableRow({
          children: [
            cell("network/", { bold: true, fill: C.bg_purple, width: 2160 }),
            cell("17개", { align: AlignmentType.CENTER, width: 2700 }),
            cell("WinHttpClient, AuthApi, TokenStore,\nZmqSend/Recv, LogSink, ClipStore,\nHeartbeat, ReconnectPolicy ...", { size: 18, width: 4000 }),
            cell("통신 전담\n인터페이스 + 구현체 분리", { width: 4100 }),
          ],
        }),
        new TableRow({
          children: [
            cell("analysis/", { bold: true, fill: C.bg_purple, width: 2160 }),
            cell("3개", { align: AlignmentType.CENTER, width: 2700 }),
            cell("IPoseAnalyzer, ZmqPoseAnalyzer,\nLocalMediaPipePoseAnalyzer", { size: 18, width: 4000 }),
            cell("MediaPipe 분석 추상화\nZMQ vs 로컬 교체 가능", { width: 4100 }),
          ],
        }),
        new TableRow({
          children: [
            cell("app/", { bold: true, fill: C.bg_light, width: 2160 }),
            cell("6개", { align: AlignmentType.CENTER, width: 2700 }),
            cell("StudySyncClientApp, MainFrm,\nStudySyncClientView, pch ...", { size: 18, width: 4000 }),
            cell("MFC 부트스트랩\n스레드 오케스트레이션", { width: 4100 }),
          ],
        }),
      ],
    }),
    spacer(100),
    body("총 44개 파일  |  헤더 .h + 구현 .cpp 분리  |  include/ + src/ 디렉토리 구조", { bold: true, color: C.primary }),
    pageBreakPara(),
  ];
}

function slide8_status() {
  return [
    slideTitle("현재 진행 상황"),
    spacer(50),
    new Table({
      width: { size: CONTENT_W, type: WidthType.DXA },
      columnWidths: [4320, 5400, 3240],
      rows: [
        new TableRow({
          children: [
            cell("항목", { bold: true, fill: C.primary, color: C.white, width: 4320 }),
            cell("세부 내용", { bold: true, fill: C.primary, color: C.white, width: 5400 }),
            cell("상태", { bold: true, fill: C.primary, color: C.white, align: AlignmentType.CENTER, width: 3240 }),
          ],
        }),
        // 완성
        new TableRow({
          children: [
            cell("RingBuffer 동기화", { bold: true, width: 4320 }),
            cell("push/wait_pop/try_pop/snapshot/close 전체", { width: 5400 }),
            cell("✅ 완성", { fill: C.bg_green, align: AlignmentType.CENTER, color: C.accent, bold: true, width: 3240 }),
          ],
        }),
        new TableRow({
          children: [
            cell("웹쮨 캡처 파이프라인", { bold: true, width: 4320 }),
            cell("CaptureThread → 3개 버퍼 동시 push (fps config)", { width: 5400 }),
            cell("✅ 완성", { fill: C.bg_green, align: AlignmentType.CENTER, color: C.accent, bold: true, width: 3240 }),
          ],
        }),
        new TableRow({
          children: [
            cell("GPU 렌더링 (Direct2D)", { bold: true, width: 4320 }),
            cell("D2DRenderer + RenderThread + 리사이즈 + 디바이스 복구", { width: 5400 }),
            cell("✅ 완성", { fill: C.bg_green, align: AlignmentType.CENTER, color: C.accent, bold: true, width: 3240 }),
          ],
        }),
        new TableRow({
          children: [
            cell("인터페이스 추상화", { bold: true, width: 4320 }),
            cell("IFrameSender, ILogSink, IEventClipStore, IPoseAnalyzer", { width: 5400 }),
            cell("✅ 완성", { fill: C.bg_green, align: AlignmentType.CENTER, color: C.accent, bold: true, width: 3240 }),
          ],
        }),
        new TableRow({
          children: [
            cell("Claim Check 이벤트 파이프라인", { bold: true, width: 4320 }),
            cell("EventShadowBuffer → ClipStore → ClipRef → JSONL", { width: 5400 }),
            cell("✅ 완성", { fill: C.bg_green, align: AlignmentType.CENTER, color: C.accent, bold: true, width: 3240 }),
          ],
        }),
        new TableRow({
          children: [
            cell("서버 연결 관리", { bold: true, width: 4320 }),
            cell("HeartbeatClient + ReconnectPolicy (지수 백오프)", { width: 5400 }),
            cell("✅ 완성", { fill: C.bg_green, align: AlignmentType.CENTER, color: C.accent, bold: true, width: 3240 }),
          ],
        }),
        new TableRow({
          children: [
            cell("WinHTTP 통신 인프라", { bold: true, width: 4320 }),
            cell("WinHttpClient 싱글톤 + JWT 헤더 자동 주입", { width: 5400 }),
            cell("✅ 완성", { fill: C.bg_green, align: AlignmentType.CENTER, color: C.accent, bold: true, width: 3240 }),
          ],
        }),
        new TableRow({
          children: [
            cell("로그인/회원가입 API", { bold: true, width: 4320 }),
            cell("AuthApi + TokenStore (%APPDATA% 저장)", { width: 5400 }),
            cell("✅ 완성", { fill: C.bg_green, align: AlignmentType.CENTER, color: C.accent, bold: true, width: 3240 }),
          ],
        }),
        // 진행중
        new TableRow({
          children: [
            cell("ZMQ 실제 송수신", { bold: true, width: 4320 }),
            cell("ZmqRecvThread 내부 TODO — JSON 파싱 + 분석결과 연결", { width: 5400 }),
            cell("🔧 진행중", { fill: C.bg_orange, align: AlignmentType.CENTER, color: C.warn, bold: true, width: 3240 }),
          ],
        }),
        new TableRow({
          children: [
            cell("JSONL HTTP 전송", { bold: true, width: 4320 }),
            cell("flush_to_http() TODO — WinHttpClient 연동 예정", { width: 5400 }),
            cell("🔧 진행중", { fill: C.bg_orange, align: AlignmentType.CENTER, color: C.warn, bold: true, width: 3240 }),
          ],
        }),
        // 예정
        new TableRow({
          children: [
            cell("MediaPipe 연동", { bold: true, width: 4320 }),
            cell("Python pose_server.py → ZMQ → AnalysisResult", { width: 5400 }),
            cell("📅 예정", { fill: C.bg_light, align: AlignmentType.CENTER, color: C.gray, bold: true, width: 3240 }),
          ],
        }),
        new TableRow({
          children: [
            cell("오버레이 렌더링", { bold: true, width: 4320 }),
            cell("OverlayPainter — 랜드마크/각도/상태 표시", { width: 5400 }),
            cell("📅 예정", { fill: C.bg_light, align: AlignmentType.CENTER, color: C.gray, bold: true, width: 3240 }),
          ],
        }),
        new TableRow({
          children: [
            cell("로그인 UI", { bold: true, width: 4320 }),
            cell("MFC 로그인/회원가입 화면 + AuthApi 연동", { width: 5400 }),
            cell("📅 예정", { fill: C.bg_light, align: AlignmentType.CENTER, color: C.gray, bold: true, width: 3240 }),
          ],
        }),
        new TableRow({
          children: [
            cell("알림 팝업 / Arduino", { bold: true, width: 4320 }),
            cell("AlertDispatchThread → UI 마셜링 + 시리얼 통신", { width: 5400 }),
            cell("📅 예정", { fill: C.bg_light, align: AlignmentType.CENTER, color: C.gray, bold: true, width: 3240 }),
          ],
        }),
      ],
    }),
    pageBreakPara(),
  ];
}

function slide9_erd() {
  return [
    slideTitle("DB ERD — 서버 연동 구조"),
    spacer(50),
    ...codeBlock([
      "  USER ────1:1──── GOAL        (하루 목표/휴식 주기)",
      "    │",
      "    ├───1:N──── SESSION     (공부 세션 시작~종료)",
      "    │                │",
      "    │                ├──1:N── FOCUS_LOG    (5초단위 집중도 점수)",
      "    │                └──1:N── POSTURE_LOG  (목각도/어깨기울기)",
      "    │",
      "    └───1:N──── TRAIN_DATA  (Phase2 재학습용 랜드마크)",
    ]),
    spacer(200),
    subtitle("클라이언트 → 서버 연동 엔드포인트"),
    new Table({
      width: { size: CONTENT_W, type: WidthType.DXA },
      columnWidths: [2700, 4320, 2700, 3240],
      rows: [
        new TableRow({
          children: [
            cell("메서드", { bold: true, fill: C.primary, color: C.white, width: 2700 }),
            cell("엔드포인트", { bold: true, fill: C.primary, color: C.white, width: 4320 }),
            cell("주기", { bold: true, fill: C.primary, color: C.white, width: 2700 }),
            cell("데이터", { bold: true, fill: C.primary, color: C.white, width: 3240 }),
          ],
        }),
        new TableRow({
          children: [
            cell("POST", { bold: true, width: 2700 }),
            cell("/auth/register", { font: "Consolas", size: 18, width: 4320 }),
            cell("1회", { width: 2700 }),
            cell("email + password + name", { width: 3240 }),
          ],
        }),
        new TableRow({
          children: [
            cell("POST", { bold: true, width: 2700 }),
            cell("/auth/login", { font: "Consolas", size: 18, width: 4320 }),
            cell("1회", { width: 2700 }),
            cell("email + password → JWT", { width: 3240 }),
          ],
        }),
        new TableRow({
          children: [
            cell("POST", { bold: true, width: 2700 }),
            cell("/session/start", { font: "Consolas", size: 18, width: 4320 }),
            cell("세션 시작", { width: 2700 }),
            cell("user_id, start_time", { width: 3240 }),
          ],
        }),
        new TableRow({
          children: [
            cell("POST", { bold: true, width: 2700 }),
            cell("/focus", { font: "Consolas", size: 18, width: 4320 }),
            cell("5초마다", { width: 2700 }),
            cell("focus_score, state (JSONL)", { width: 3240 }),
          ],
        }),
        new TableRow({
          children: [
            cell("POST", { bold: true, width: 2700 }),
            cell("/posture", { font: "Consolas", size: 18, width: 4320 }),
            cell("5초마다", { width: 2700 }),
            cell("neck_angle, shoulder (JSONL)", { width: 3240 }),
          ],
        }),
        new TableRow({
          children: [
            cell("POST", { bold: true, width: 2700 }),
            cell("/events", { font: "Consolas", size: 18, width: 4320 }),
            cell("이벤트 발생", { width: 2700 }),
            cell("event + clip_ref (JSONL)", { width: 3240 }),
          ],
        }),
      ],
    }),
    spacer(100),
    body("⚠️  클라이언트는 비밀번호 평문 전송 (HTTPS 전제)  |  bcrypt 해싱은 서버에서만 수행", { bold: true, color: C.warn }),
    pageBreakPara(),
  ];
}

function slide10_next() {
  return [
    slideTitle("향후 계획"),
    spacer(100),
    new Table({
      width: { size: CONTENT_W, type: WidthType.DXA },
      columnWidths: [2160, 5400, 5400],
      rows: [
        new TableRow({
          children: [
            cell("단계", { bold: true, fill: C.primary, color: C.white, width: 2160 }),
            cell("작업", { bold: true, fill: C.primary, color: C.white, width: 5400 }),
            cell("설명", { bold: true, fill: C.primary, color: C.white, width: 5400 }),
          ],
        }),
        new TableRow({
          children: [
            cell("Phase 1-A", { bold: true, fill: C.bg_green, width: 2160 }),
            cell("MediaPipe Python 서버 (pose_server.py)", { width: 5400 }),
            cell("ZMQ PULL → MediaPipe Pose + FaceMesh → JSON PUSH", { width: 5400 }),
          ],
        }),
        new TableRow({
          children: [
            cell("Phase 1-B", { bold: true, fill: C.bg_green, width: 2160 }),
            cell("ZmqRecvThread 실제 연동", { width: 5400 }),
            cell("JSON 파싱 → AnalysisResult → PostureEventDetector", { width: 5400 }),
          ],
        }),
        new TableRow({
          children: [
            cell("Phase 1-C", { bold: true, fill: C.bg_green, width: 2160 }),
            cell("OverlayPainter 랜드마크 오버레이", { width: 5400 }),
            cell("D2D 위에 33개 랜드마크 + 각도 + 상태 표시", { width: 5400 }),
          ],
        }),
        new TableRow({
          children: [
            cell("Phase 1-D", { bold: true, fill: C.bg_blue, width: 2160 }),
            cell("로그인 UI + 세션 관리", { width: 5400 }),
            cell("MFC 로그인 화면 → AuthApi → 세션 시작/종료", { width: 5400 }),
          ],
        }),
        new TableRow({
          children: [
            cell("Phase 1-E", { bold: true, fill: C.bg_blue, width: 2160 }),
            cell("JSONL flush + 통계 API 연동", { width: 5400 }),
            cell("WinHttpClient로 flush_to_http 구현 + GET /stats", { width: 5400 }),
          ],
        }),
        new TableRow({
          children: [
            cell("Phase 2", { bold: true, fill: C.bg_purple, width: 2160 }),
            cell("라벨링 UI + AI 재학습", { width: 5400 }),
            cell("집중/딴짓/졸음 라벨 → TRAIN_DATA → PyTorch MLP", { width: 5400 }),
          ],
        }),
      ],
    }),
    spacer(400),
    new Paragraph({
      alignment: AlignmentType.CENTER,
      children: [
        new TextRun({ text: "Thank You", bold: true, size: 48, color: C.primary, font: "Arial" }),
      ],
    }),
  ];
}

// ── 문서 조립 ─────────────────────────────
const doc = new Document({
  styles: {
    default: {
      document: { run: { font: "Arial", size: 22 } },
    },
  },
  sections: [
    {
      properties: {
        page: {
          size: {
            width: 12240,
            height: 15840,
            orientation: PageOrientation.LANDSCAPE,
          },
          margin: { top: 1080, right: 1440, bottom: 720, left: 1440 },
        },
      },
      headers: {
        default: new Header({
          children: [
            new Paragraph({
              alignment: AlignmentType.RIGHT,
              border: { bottom: { style: BorderStyle.SINGLE, size: 2, color: C.primary, space: 4 } },
              children: [
                new TextRun({ text: "StudySync Client Architecture", size: 16, color: C.gray, font: "Arial" }),
              ],
            }),
          ],
        }),
      },
      footers: {
        default: new Footer({
          children: [
            new Paragraph({
              alignment: AlignmentType.CENTER,
              children: [
                new TextRun({ text: "Page ", size: 16, color: C.gray, font: "Arial" }),
                new TextRun({ children: [PageNumber.CURRENT], size: 16, color: C.gray, font: "Arial" }),
              ],
            }),
          ],
        }),
      },
      children: [
        ...slide1_title(),
        ...slide2_overview(),
        ...slide3_pipeline(),
        ...slide4_gpu(),
        ...slide5_claimcheck(),
        ...slide6_network(),
        ...slide7_layers(),
        ...slide8_status(),
        ...slide9_erd(),
        ...slide10_next(),
      ],
    },
  ],
});

const OUTPUT = "C:/naegastudy_project/StudySync/docs/StudySync_Architecture.docx";
Packer.toBuffer(doc).then((buffer) => {
  fs.writeFileSync(OUTPUT, buffer);
  console.log("Created: " + OUTPUT);
});
