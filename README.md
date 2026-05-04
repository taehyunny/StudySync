# StudySync

웹캠 기반 자세 감지와 AI 재학습을 활용해 공부 집중도를 관리하는 개인 공부 스케줄러 프로젝트입니다.

## 팀원별 작업 구역

| 담당 | 작업 폴더 | 주요 책임 |
|---|---|---|
| 정재훈 | `ai-server/` | MediaPipe 자세 분석, 졸음/이탈 감지, 집중도 계산, 재학습 모델 |
| 김혜윤 | `main-server/` | 회원/JWT, 목표, 세션, 집중도/자세 로그, 통계 API, DB |
| 정태현 | `client/` | MFC UI, OpenCV 웹캠 렌더링, ZMQ/서버 연동, 대시보드 |
| 김찬우 | `integration/`, `docs/` | 일정/문서 관리, API 명세 검토, 통합 테스트, 발표/데모 |

각 담당자는 기본적으로 자기 작업 폴더 안에서 개발합니다. 공통 API, DB 구조, 문서 변경은 PR이나 팀 공유 후 수정합니다.

## 폴더 구조

```text
StudySync/
├─ ai-server/          # AI 학습서버
├─ main-server/        # 메인 운용서버
├─ client/             # MFC Windows 클라이언트
├─ integration/        # 통합 테스트, QA, 데모 시나리오
├─ docs/               # 프로젝트 문서와 담당자별 문서
│  ├─ project/
│  └─ team/
└─ assets/
   └─ erd-flow/        # ERD, 순서도 이미지
```

## 통신 규약

전체 API 명세는 [통신 프로토콜](<docs/project/[4조] StudySync 통신프로토콜.md>)을 기준으로 합니다.

### AI 서버

| 메서드 | 엔드포인트 | 설명 |
|---|---|---|
| POST | `/analyze/frame` | 5fps 프레임 분석 후 집중도, 상태, 자세 가이드 반환 |
| POST | `/baseline/capture` | 기준 자세 저장 |
| POST | `/traindata` | 라벨링 학습 데이터 저장 |
| GET | `/traindata/count` | 라벨별 데이터 수 조회 |
| POST | `/model/train` | 재학습 모델 학습 실행 |

### 메인 서버

| 메서드 | 엔드포인트 | 설명 |
|---|---|---|
| POST | `/auth/register` | 회원가입 |
| POST | `/auth/login` | 로그인 및 JWT 발급 |
| POST | `/goal` | 목표 저장/수정 |
| GET | `/goal` | 목표 조회 |
| POST | `/session/start` | 공부 세션 시작 |
| POST | `/session/end` | 공부 세션 종료 |
| POST | `/focus` | 집중도 로그 저장 |
| POST | `/posture` | 자세 로그 저장 |
| GET | `/stats/today` | 오늘 통계 조회 |
| GET | `/stats/hourly` | 시간대별 집중도 조회 |
| GET | `/stats/pattern` | 휴식 패턴 분석 |
| GET | `/stats/weekly` | 주간 리포트 조회 |

공통 인증 헤더:

```http
Authorization: Bearer {access_token}
Content-Type: application/json
```

## 작업 규칙

- 개인 담당 폴더 외 변경이 필요하면 먼저 README 또는 이슈에 변경 이유를 남깁니다.
- API 요청/응답 형식은 `docs/project/[4조] StudySync 통신프로토콜.md`를 먼저 수정한 뒤 코드에 반영합니다.
- DB 테이블 변경은 `docs/project/[4조] StudySync DB_ERD.md`와 `main-server/database/`를 함께 수정합니다.
- 통합 테스트 항목은 `integration/qa/`에 정리합니다.
- `.env`, 가상환경, 캐시, 학습 데이터 원본, 모델 바이너리는 GitHub에 올리지 않습니다.

## 주요 문서

- [개발계획서](<docs/project/[4조] StudySync 개발계획서.md>)
- [개발스케줄](<docs/project/[4조] StudySync 개발스케줄.md>)
- [요구사항분석서](<docs/project/[4조] StudySync 요구사항분석서.md>)
- [DB ERD](<docs/project/[4조] StudySync DB_ERD.md>)
- [순서도](<docs/project/[4조] StudySync 순서도.md>)
- [디자인 목업](<docs/project/[4조] StudySync 디자인목업.md>)
