# [4조] StudySync 통신 프로토콜

## 1. 통신 구조 개요

| 구간 | 프로토콜 | 형식 | 비고 |
|---|---|---|---|
| 클라이언트 → AI서버 | HTTP POST | multipart/form-data 또는 JSON | 5fps base64 프레임 전송 |
| AI서버 → 클라이언트 | HTTP 200 | application/json | 집중도, 자세, 가이드 반환 |
| 클라이언트 → 메인서버 | HTTP REST | application/json | JWT 인증 헤더 필수 |
| 메인서버 → 클라이언트 | HTTP 200 | application/json | 통계, 세션, 목표 데이터 반환 |

## 2. AI 서버 API

| 메서드 | 엔드포인트 | 요청 파라미터 | 응답 | 설명 |
|---|---|---|---|---|
| POST | `/analyze/frame` | `session_id`, `frame(base64)` | `focus_score`, `state`, `neck_angle`, `guide` | 5fps 프레임 분석 |
| POST | `/baseline/capture` | `session_id`, `frame` | `saved: true` | 기준 자세 저장 |
| POST | `/traindata` | `landmarks_json`, `label` | `id`, `saved` | 재학습 데이터 수집 |
| GET | `/traindata/count` | - | `{집중, 딴짓, 졸음, total}` | 라벨별 수집 현황 |
| POST | `/model/train` | - | `accuracy`, `status` | 재학습 실행 트리거 |

## 3. 메인서버 API

| 메서드 | 엔드포인트 | 요청 파라미터 | 응답 | 설명 |
|---|---|---|---|---|
| POST | `/auth/register` | `email`, `password`, `name` | `user_id`, `message` | 회원가입 |
| POST | `/auth/login` | `email`, `password` | `access_token`, `user_id` | 로그인/JWT 발급 |
| POST | `/goal` | `daily_goal_min`, `rest_interval_min`, `rest_duration_min` | `saved: true` | 목표 저장/수정 |
| GET | `/goal` | - | 목표 설정 JSON | 목표 조회 |
| POST | `/session/start` | `date` | `session_id`, `start_time` | 세션 시작 |
| POST | `/session/end` | `session_id` | `focus_min`, `avg_focus`, `goal_achieved` | 세션 종료 |
| POST | `/focus` | `session_id`, `focus_score`, `state`, `is_absent`, `is_drowsy` | `saved: true` | 집중도 저장 |
| POST | `/posture` | `session_id`, `neck_angle`, `shoulder_diff`, `posture_ok`, `vs_baseline` | `saved: true` | 자세 저장 |
| GET | `/stats/today` | - | `focus_min`, `avg_focus`, `warning_count`, `goal_progress` | 오늘 통계 |
| GET | `/stats/hourly` | `date=YYYY-MM-DD` | `[{hour, avg_focus}]` | 시간대별 집중도 |
| GET | `/stats/pattern` | - | `avg_focus_duration`, `best_hour`, `weekly_avg` | 휴식 패턴 분석 |
| GET | `/stats/weekly` | - | `[{date, focus_min, avg_focus}]` | 주간 리포트 |

## 4. 공통 인증 헤더

```http
Authorization: Bearer {access_token}
Content-Type: application/json
```

## 5. 오류 코드

| HTTP 코드 | 오류 코드 | 설명 |
|---|---|---|
| 400 | `INVALID_REQUEST` | 요청 파라미터 누락 또는 형식 오류 |
| 401 | `UNAUTHORIZED` | JWT 토큰 없음 또는 만료 |
| 404 | `NOT_FOUND` | 세션 또는 리소스 없음 |
| 409 | `DUPLICATE_EMAIL` | 이미 사용 중인 이메일 |
| 500 | `INTERNAL_ERROR` | 서버 내부 오류 또는 AI 분석 실패 |

