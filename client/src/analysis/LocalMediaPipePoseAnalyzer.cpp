#include "pch.h"
#include "analysis/LocalMediaPipePoseAnalyzer.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <sstream>

// ── OpenCV Haar-cascade 기반 keypoint 추출 ──────────────────────────────
//
// MediaPipe C++ SDK 연동 전 임시 구현.
// 얼굴 bbox + 눈 감지로 EAR/각도를 기하학적으로 근사한다.
//
// 교체 시: MediaPipe Tasks Pose Landmarker → 33 landmark → 각 필드 직접 계산
// 참고:    https://developers.google.com/mediapipe/solutions/vision/pose_landmarker

namespace {

constexpr double kPi = 3.14159265358979323846;

// OpenCV 배포본 haarcascades 디렉터리
// OPENCV_DIR 환경변수 미설정 시 기본 경로 사용
const char* kHaarDir = "C:/opencv/build/etc/haarcascades/";

} // namespace

// ── 초기화 ─────────────────────────────────────────────────────────────

bool LocalMediaPipePoseAnalyzer::initialize()
{
    const std::string face_xml = std::string(kHaarDir) + "haarcascade_frontalface_default.xml";
    const std::string eye_xml  = std::string(kHaarDir) + "haarcascade_eye.xml";

    const bool face_ok = face_cascade_.load(face_xml);
    const bool eye_ok  = eye_cascade_.load(eye_xml);

    if (!face_ok) {
        OutputDebugStringA("[LocalPose] face cascade not found — face_detected always 0\n");
    }
    if (!eye_ok) {
        OutputDebugStringA("[LocalPose] eye cascade not found — EAR uses fallback 0.32\n");
    }

    initialized_ = face_ok;
    return true;   // cascade 없어도 hard-fail 안 함 (더미 값 반환)
}

// ── 메인 분석 ──────────────────────────────────────────────────────────

std::optional<AnalysisResult> LocalMediaPipePoseAnalyzer::analyze(const Frame& frame)
{
    if (frame.mat.empty()) return std::nullopt;

    AnalysisResult result;
    result.timestamp_ms = frame.timestamp_ms;

    // BGR → Grayscale + 히스토그램 평탄화 (조도 보상)
    cv::Mat gray;
    cv::cvtColor(frame.mat, gray, cv::COLOR_BGR2GRAY);
    cv::equalizeHist(gray, gray);

    // ── 얼굴 감지 ────────────────────────────────────────────────
    std::vector<cv::Rect> faces;
    if (initialized_) {
        face_cascade_.detectMultiScale(
            gray, faces,
            1.1,            // scaleFactor
            3,              // minNeighbors
            0,
            cv::Size(80, 80)   // minSize
        );
    }

    if (faces.empty()) {
        // 얼굴 미감지 → absent
        result.face_detected = 0;
        result.ear           = 0.0;
        result.neck_angle    = 0.0;
        result.shoulder_diff = 0.0;
        result.head_yaw      = 0.0;
        result.head_pitch    = 0.0;
        result.focus_score   = 0;
        result.state         = "absent";
        result.posture_ok    = false;
        result.drowsy        = false;
        result.absent        = true;
        return result;
    }

    // 가장 큰 얼굴 사용
    const cv::Rect face = *std::max_element(faces.begin(), faces.end(),
        [](const cv::Rect& a, const cv::Rect& b) { return a.area() < b.area(); });

    result.face_detected = 1;
    result.absent        = false;

    const double frame_w = static_cast<double>(frame.mat.cols);
    const double frame_h = static_cast<double>(frame.mat.rows);

    // 얼굴 중심 (정규화: -1~+1)
    const double face_cx = face.x + face.width  * 0.5;
    const double face_cy = face.y + face.height * 0.5;
    const double rel_x   = (face_cx - frame_w * 0.5) / (frame_w * 0.5);
    const double rel_y   = (face_cy - frame_h * 0.5) / (frame_h * 0.5);

    // ── head_yaw (좌우 회전) ───────────────────────────────────
    // 정면: face.width/face.height ≈ 0.80
    // 측면으로 돌수록 폭/높이 비 감소
    const double face_ar = static_cast<double>(face.width) / face.height;
    result.head_yaw = std::clamp((0.80 - face_ar) / 0.40 * 45.0, -90.0, 90.0);

    // ── head_pitch (앞뒤 기울기) ───────────────────────────────
    // 아래를 보면 얼굴이 화면 하단으로 이동 → rel_y 양수
    result.head_pitch = std::clamp(rel_y * 30.0, -90.0, 90.0);

    // ── neck_angle ─────────────────────────────────────────────
    // pitch + 수직 이동 복합 추정
    result.neck_angle = std::clamp(
        std::abs(result.head_pitch) + std::abs(rel_y) * 10.0, 0.0, 90.0);

    // ── shoulder_diff ──────────────────────────────────────────
    // 머리 수평 오프셋으로 어깨 비대칭 근사 (픽셀 단위)
    result.shoulder_diff = std::abs(rel_x) * face.width * 0.3;

    // ── EAR (눈 감김) ──────────────────────────────────────────
    result.ear = detect_ear(gray, face);

    // ── 판정 ───────────────────────────────────────────────────
    result.drowsy     = (result.ear > 0.0 && result.ear < 0.25f);
    result.posture_ok = (result.neck_angle < 25.0);

    if (result.drowsy) {
        result.focus_score = 30 + static_cast<int>(result.ear * 100.0);
        result.state       = "drowsy";
    } else if (!result.posture_ok) {
        result.focus_score = 55;
        result.state       = "distracted";
    } else {
        result.focus_score = static_cast<int>(
            80.0 - std::abs(result.head_yaw) * 0.5 - std::abs(result.head_pitch) * 0.3);
        result.state = "focus";
    }
    result.focus_score = std::clamp(result.focus_score, 0, 100);
    result.guide = result.posture_ok ? "" : "Please sit up straight";

    return result;
}

// ── EAR 추정 ───────────────────────────────────────────────────────────

double LocalMediaPipePoseAnalyzer::detect_ear(const cv::Mat& gray, const cv::Rect& face_rect)
{
    if (eye_cascade_.empty()) return 0.32;  // cascade 없으면 기본값

    // 얼굴 상단 60% 영역에서만 눈 탐색 (입/코 오감지 방지)
    const cv::Rect eye_roi(
        face_rect.x,
        face_rect.y,
        face_rect.width,
        static_cast<int>(face_rect.height * 0.6)
    );
    cv::Mat roi = gray(eye_roi & cv::Rect(0, 0, gray.cols, gray.rows));

    std::vector<cv::Rect> eyes;
    eye_cascade_.detectMultiScale(roi, eyes, 1.1, 2, 0, cv::Size(20, 20));

    if (eyes.size() < 2) return 0.32;

    // 좌우로 정렬
    std::sort(eyes.begin(), eyes.end(),
        [](const cv::Rect& a, const cv::Rect& b) { return a.x < b.x; });

    // 각 눈 bbox의 aspect ratio → EAR 근사
    // 실제 EAR = Σ|vertical| / (2*|horizontal|) ≈ height/width * 0.75
    double ear_sum = 0.0;
    const int n = std::min(2, static_cast<int>(eyes.size()));
    for (int i = 0; i < n; ++i) {
        ear_sum += static_cast<double>(eyes[i].height) / eyes[i].width * 0.75;
    }
    return std::clamp(ear_sum / n, 0.0, 1.0);
}

void LocalMediaPipePoseAnalyzer::shutdown()
{
}
