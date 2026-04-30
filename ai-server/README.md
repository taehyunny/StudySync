# AI Server

담당: 정재훈

## 책임 범위

- 클라이언트에서 받은 5fps 웹캠 프레임 분석
- MediaPipe Pose/Face Mesh 기반 랜드마크 추출
- 목 각도, 어깨 기울기, EAR, 자리이탈 판정
- 규칙 기반 집중도 점수 계산
- Phase 2 재학습 데이터 수집 및 PyTorch 모델 학습

## 권장 구조

```text
ai-server/
├─ app/
│  ├─ api/             # FastAPI 라우터
│  ├─ services/        # 자세/졸음/집중도 분석 로직
│  ├─ models/          # 요청/응답 스키마
│  └─ utils/
├─ data/               # 로컬 학습 데이터, Git 제외 대상
├─ models/             # 로컬 모델 파일, Git 제외 대상
└─ tests/
```

## 담당 API

- `POST /analyze/frame`
- `POST /baseline/capture`
- `POST /traindata`
- `GET /traindata/count`
- `POST /model/train`
