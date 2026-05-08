#include "pch.h"
#include "ReviewDlg.h"
#include "resource.h"

#include <chrono>
#include <ctime>
#include <sstream>

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

// ── confidence 임계값 ────────────────────────────────────────────
//   TODO: AI팀과 합의 후 ClientTransportConfig 등으로 이동
constexpr double kConfHighThresh   = 0.85;   // 이상: 피드백 버튼 없음
constexpr double kConfMedThresh    = 0.70;   // 이상 kConfHighThresh 미만: 일반 표시
// 미만: 노란 배경 + 상단 정렬

// ── 배경 색상 ────────────────────────────────────────────────────
static const COLORREF kColorLowConf  = RGB(255, 255, 160); // 연노랑
static const COLORREF kColorHighConf = RGB(200, 200, 200); // 회색 (confidence 높아서 버튼 없음)
static const COLORREF kColorFeedback = RGB(220, 240, 220); // 연녹색 (피드백 완료)

// consent_given_ 제거 — ConsentStore 파일 저장으로 교체됨

// ── 메시지 맵 ────────────────────────────────────────────────────

BEGIN_MESSAGE_MAP(ReviewDlg, CDialog)
    ON_NOTIFY(NM_CUSTOMDRAW, IDC_LIST_EVENTS, OnNMCustomdrawListEvents)
    ON_NOTIFY(LVN_ITEMCHANGED, IDC_LIST_EVENTS, OnLvnItemchangedListEvents)
    ON_BN_CLICKED(IDC_BTN_CORRECT, OnBnClickedCorrect)
    ON_BN_CLICKED(IDC_BTN_WRONG,   OnBnClickedWrong)
END_MESSAGE_MAP()

// ── 생성 ─────────────────────────────────────────────────────────

ReviewDlg::ReviewDlg(ReviewEventStore& store, long long session_id, CWnd* parent)
    : CDialog(IDD_REVIEW, parent)
    , store_(store)
    , session_id_(session_id)
{
}

bool ReviewDlg::has_uncertain_events() const
{
    return store_.count_uncertain() > 0;
}

void ReviewDlg::DoDataExchange(CDataExchange* pDX)
{
    CDialog::DoDataExchange(pDX);
    DDX_Control(pDX, IDC_LIST_EVENTS, list_ctrl_);
}

// ── 초기화 ───────────────────────────────────────────────────────

BOOL ReviewDlg::OnInitDialog()
{
    CDialog::OnInitDialog();

    // ── 리스트 스타일 설정 ──────────────────────────────────────
    list_ctrl_.SetExtendedStyle(
        LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);

    // ── 컬럼 추가 ───────────────────────────────────────────────
    //   총 460 DLU 중: 시각(90) + 판정(110) + 신뢰도(100) + 피드백(160)
    list_ctrl_.InsertColumn(0, _T("시각"),     LVCFMT_LEFT,  90);
    list_ctrl_.InsertColumn(1, _T("판정"),     LVCFMT_LEFT, 110);
    list_ctrl_.InsertColumn(2, _T("신뢰도"),   LVCFMT_LEFT, 100);
    list_ctrl_.InsertColumn(3, _T("피드백"),   LVCFMT_LEFT, 160);

    // ── 헤더 문구 ────────────────────────────────────────────────
    const int uncertain = store_.count_uncertain();
    CString header;
    if (uncertain > 0) {
        header.Format(
            _T("이번 세션에서 불확실한 판정이 %d건 있었습니다. 맞는지 확인해 주세요."),
            uncertain);
    } else {
        header = _T("이번 세션의 모든 판정이 높은 신뢰도로 완료되었습니다.");
    }
    SetDlgItemText(IDC_STATIC_REVIEW_HEADER, header);

    // ── 목록 채우기 ──────────────────────────────────────────────
    events_ = store_.sorted_events();
    populate_list();

    // ── 피드백 패널 초기 비활성화 ────────────────────────────────
    update_feedback_panel(-1);

    return TRUE;
}

// ── 목록 채우기 ──────────────────────────────────────────────────

void ReviewDlg::populate_list()
{
    list_ctrl_.DeleteAllItems();
    for (int i = 0; i < static_cast<int>(events_.size()); ++i) {
        const ReviewEvent& e = events_[i];

        list_ctrl_.InsertItem(i, format_time(e.timestamp_ms));
        list_ctrl_.SetItemText(i, 1, type_label(e.type));
        list_ctrl_.SetItemText(i, 2, confidence_label(e.confidence));
        list_ctrl_.SetItemText(i, 3, feedback_label(e.feedback));
        list_ctrl_.SetItemData(i, static_cast<DWORD_PTR>(i));
    }
}

void ReviewDlg::refresh_item(int index)
{
    if (index < 0 || index >= static_cast<int>(events_.size())) return;
    const ReviewEvent& e = events_[index];
    list_ctrl_.SetItemText(index, 3, feedback_label(e.feedback));
    list_ctrl_.RedrawItems(index, index);
}

// ── 피드백 패널 갱신 ─────────────────────────────────────────────

void ReviewDlg::update_feedback_panel(int index)
{
    CWnd* wnd_q   = GetDlgItem(IDC_STATIC_QUESTION);
    CWnd* wnd_ok  = GetDlgItem(IDC_BTN_CORRECT);
    CWnd* wnd_ng  = GetDlgItem(IDC_BTN_WRONG);
    CWnd* wnd_done = GetDlgItem(IDC_STATIC_FEEDBACK_DONE);

    if (index < 0 || index >= static_cast<int>(events_.size())) {
        // 선택 없음 → 패널 전체 비활성
        if (wnd_q)    wnd_q->ShowWindow(SW_HIDE);
        if (wnd_ok)   wnd_ok->ShowWindow(SW_HIDE);
        if (wnd_ng)   wnd_ng->ShowWindow(SW_HIDE);
        if (wnd_done) wnd_done->ShowWindow(SW_HIDE);
        return;
    }

    const ReviewEvent& e = events_[index];

    if (!e.needs_feedback()) {
        // confidence >= 0.85: 피드백 버튼 없음
        if (wnd_q) {
            wnd_q->SetWindowText(_T("이 판정의 신뢰도가 높아 별도 피드백이 불필요합니다."));
            wnd_q->ShowWindow(SW_SHOW);
        }
        if (wnd_ok)   wnd_ok->ShowWindow(SW_HIDE);
        if (wnd_ng)   wnd_ng->ShowWindow(SW_HIDE);
        if (wnd_done) wnd_done->ShowWindow(SW_HIDE);
        return;
    }

    if (e.feedback != ReviewEvent::Feedback::None) {
        // 이미 피드백 완료
        if (wnd_q)    wnd_q->ShowWindow(SW_HIDE);
        if (wnd_ok)   wnd_ok->ShowWindow(SW_HIDE);
        if (wnd_ng)   wnd_ng->ShowWindow(SW_HIDE);
        if (wnd_done) {
            const CString msg = (e.feedback == ReviewEvent::Feedback::Correct)
                ? _T("✓ 올바른 판정으로 확인되었습니다.")
                : _T("✗ 오류 피드백이 접수되었습니다. (업로드 예정)");
            wnd_done->SetWindowText(msg);
            wnd_done->ShowWindow(SW_SHOW);
        }
        return;
    }

    // 피드백 대기 중 → 버튼 표시
    if (wnd_q) {
        wnd_q->SetWindowText(_T("이 판정이 맞나요?"));
        wnd_q->ShowWindow(SW_SHOW);
    }
    if (wnd_ok)   wnd_ok->ShowWindow(SW_SHOW);
    if (wnd_ng)   wnd_ng->ShowWindow(SW_SHOW);
    if (wnd_done) wnd_done->ShowWindow(SW_HIDE);
}

// ── 피드백 적용 ──────────────────────────────────────────────────

void ReviewDlg::apply_feedback(int index, ReviewEvent::Feedback fb)
{
    if (index < 0 || index >= static_cast<int>(events_.size())) return;

    events_[index].feedback = fb;
    store_.update_feedback(events_[index].event_id, fb);

    refresh_item(index);
    update_feedback_panel(index);
}

// ── 버튼 핸들러 ──────────────────────────────────────────────────

void ReviewDlg::OnBnClickedCorrect()
{
    if (sel_index_ < 0) return;
    apply_feedback(sel_index_, ReviewEvent::Feedback::Correct);
}

void ReviewDlg::OnBnClickedWrong()
{
    if (sel_index_ < 0) return;

    // ── 동의 여부 확인 (최초 1회, ConsentStore — %APPDATA%\StudySync\consent.dat)
    if (!ConsentStore::is_consented()) {
        const int answer = AfxMessageBox(
            _T("이 영상이 모델 개선 목적으로 개발팀에 전송됩니다.\n동의하시겠습니까?"),
            MB_YESNO | MB_ICONQUESTION);
        if (answer != IDYES) return;
        ConsentStore::record_consent();
    }

    apply_feedback(sel_index_, ReviewEvent::Feedback::Wrong);

    // ── POST /feedback — multipart/form-data ──────────────────────
    const ReviewEvent& ev = events_[sel_index_];

    FeedbackRequest req;
    req.event_id      = ev.event_id;
    req.session_id    = session_id_;
    req.model_pred    = ev.clip_dir;   // clip_dir은 이벤트 유형 식별자로도 활용
    req.user_feedback = "wrong";
    req.consent_ver   = ConsentStore::kCurrentVersion;
    req.clip_path     = ev.clip_dir;   // 클립 경로 (MP4 미확정 → 디렉터리 or 빈 값)

    // 백그라운드 스레드 없이 동기 전송 (이벤트 발생 빈도가 낮아 UI 블록 무시 가능)
    const FeedbackResponse resp = FeedbackApi::send(req);

    if (!resp.saved) {
        // 업로드 실패 → 사용자에게 재시도 안내 없이 로컬에 피드백 상태만 유지
        OutputDebugStringA("[ReviewDlg] FeedbackApi::send 실패\n");
    }
}

// ── 리스트 선택 이벤트 ───────────────────────────────────────────

void ReviewDlg::OnLvnItemchangedListEvents(NMHDR* pNMHDR, LRESULT* pResult)
{
    const NMLISTVIEW* pNMLV = reinterpret_cast<const NMLISTVIEW*>(pNMHDR);
    if ((pNMLV->uNewState & LVIS_SELECTED) && !(pNMLV->uOldState & LVIS_SELECTED)) {
        sel_index_ = pNMLV->iItem;
        update_feedback_panel(sel_index_);
    }
    *pResult = 0;
}

// ── 커스텀 드로우 (행 배경 색 변경) ─────────────────────────────

void ReviewDlg::OnNMCustomdrawListEvents(NMHDR* pNMHDR, LRESULT* pResult)
{
    NMLVCUSTOMDRAW* pCD = reinterpret_cast<NMLVCUSTOMDRAW*>(pNMHDR);
    *pResult = CDRF_DODEFAULT;

    switch (pCD->nmcd.dwDrawStage) {
    case CDDS_PREPAINT:
        *pResult = CDRF_NOTIFYITEMDRAW;
        break;

    case CDDS_ITEMPREPAINT: {
        const int idx = static_cast<int>(pCD->nmcd.dwItemSpec);
        if (idx >= 0 && idx < static_cast<int>(events_.size())) {
            const ReviewEvent& e = events_[idx];
            if (e.feedback != ReviewEvent::Feedback::None) {
                pCD->clrTextBk = kColorFeedback;
            } else if (e.confidence >= kConfHighThresh) {
                pCD->clrTextBk = kColorHighConf;
                pCD->clrText   = RGB(100, 100, 100); // 회색 텍스트
            } else if (e.confidence < kConfMedThresh) {
                pCD->clrTextBk = kColorLowConf;
            }
        }
        *pResult = CDRF_NEWFONT;
        break;
    }
    default:
        break;
    }
}

// ── 정적 포맷 헬퍼 ───────────────────────────────────────────────

CString ReviewDlg::format_time(std::uint64_t timestamp_ms)
{
    const time_t sec = static_cast<time_t>(timestamp_ms / 1000);
    struct tm t {};
    localtime_s(&t, &sec);
    CString s;
    s.Format(_T("%02d:%02d:%02d"), t.tm_hour, t.tm_min, t.tm_sec);
    return s;
}

CString ReviewDlg::type_label(PostureEventType type)
{
    switch (type) {
    case PostureEventType::Drowsy:     return _T("졸음 감지");
    case PostureEventType::BadPosture: return _T("자세 불량");
    case PostureEventType::Absent:     return _T("자리 비움");
    default:                           return _T("알 수 없음");
    }
}

CString ReviewDlg::confidence_label(double confidence)
{
    CString s;
    if (confidence >= kConfHighThresh) {
        s.Format(_T("높음 (%.2f)"), confidence);
    } else if (confidence >= kConfMedThresh) {
        s.Format(_T("보통 (%.2f)"), confidence);
    } else {
        s.Format(_T("낮음 (%.2f)"), confidence);
    }
    return s;
}

CString ReviewDlg::feedback_label(ReviewEvent::Feedback fb)
{
    switch (fb) {
    case ReviewEvent::Feedback::Correct: return _T("✓ 올바른 판정");
    case ReviewEvent::Feedback::Wrong:   return _T("✗ 오류 제출");
    default:                             return _T("—");
    }
}
