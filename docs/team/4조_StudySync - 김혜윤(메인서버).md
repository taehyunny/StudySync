# 4조 StudySync - 김혜윤(메인서버)

## 1. 담당 범위

김혜윤은 StudySync의 메인 운용서버를 담당한다. 회원 인증, 목표 설정, 공부 세션, 집중도 로그, 자세 로그, 통계 집계를 관리하며 클라이언트가 조회할 수 있는 REST API를 제공한다.

## 2. 주요 기능

| 기능 | 설명 |
|---|---|
| 회원가입 | 이메일, 비밀번호, 이름 저장 및 중복 검사 |
| 로그인 | 비밀번호 검증 후 JWT Access Token 발급 |
| 목표 설정 | 하루 목표 시간, 휴식 주기, 휴식 시간 저장 |
| 세션 관리 | 공부 세션 시작/종료 및 평균 집중도 계산 |
| 집중도 로그 저장 | 5초 단위 집중도 점수와 상태 저장 |
| 자세 로그 저장 | 목 각도, 어깨 기울기, 기준 자세 변화량 저장 |
| 오늘 통계 | 집중 시간, 평균 집중도, 경고 횟수, 목표 달성률 반환 |
| 주간 리포트 | 7일 공부량과 집중도 트렌드 반환 |

## 3. 주요 테이블

| 테이블 | 설명 |
|---|---|
| USER | 사용자 계정 정보 |
| GOAL | 사용자별 공부 목표 |
| SESSION | 공부 세션 정보 |
| FOCUS_LOG | 집중도 로그 |
| POSTURE_LOG | 자세 로그 |
| TRAIN_DATA | 재학습 데이터 메타 정보 |

## 4. API

| 메서드 | 엔드포인트 | 설명 |
|---|---|---|
| POST | `/auth/register` | 회원가입 |
| POST | `/auth/login` | 로그인/JWT 발급 |
| POST | `/goal` | 목표 저장/수정 |
| GET | `/goal` | 목표 조회 |
| POST | `/session/start` | 세션 시작 |
| POST | `/session/end` | 세션 종료 |
| POST | `/focus` | 집중도 로그 저장 |
| POST | `/posture` | 자세 로그 저장 |
| GET | `/stats/today` | 오늘 통계 조회 |
| GET | `/stats/hourly` | 시간대별 집중도 조회 |
| GET | `/stats/pattern` | 휴식 패턴 분석 |
| GET | `/stats/weekly` | 주간 리포트 조회 |

## 5. 산출물

- FastAPI 메인서버 프로젝트
- JWT 인증 모듈
- MySQL DB 스키마
- SQLAlchemy 모델
- 통계 집계 API
- API 테스트 결과

