# Main Server

담당: 김혜윤

## 책임 범위

- 회원가입, 로그인, JWT 인증
- 목표 설정, 공부 세션 시작/종료
- 집중도 로그와 자세 로그 저장
- 오늘 통계, 시간대별 통계, 휴식 패턴, 주간 리포트 API
- MySQL 스키마와 SQLAlchemy 모델 관리

## 권장 구조

```text
main-server/
├─ app/
│  ├─ api/             # FastAPI 라우터
│  ├─ core/            # 설정, 인증, 보안
│  ├─ models/          # SQLAlchemy 모델
│  ├─ schemas/         # 요청/응답 스키마
│  └─ services/        # 세션/통계 비즈니스 로직
├─ database/           # DDL, 마이그레이션, 샘플 쿼리
└─ tests/
```

## 담당 API

- `POST /auth/register`
- `POST /auth/login`
- `POST /goal`, `GET /goal`
- `POST /session/start`, `POST /session/end`
- `POST /focus`
- `POST /posture`
- `GET /stats/today`
- `GET /stats/hourly`
- `GET /stats/pattern`
- `GET /stats/weekly`
