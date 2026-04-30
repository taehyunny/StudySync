# Client

담당: 김찬우

## 책임 범위

- PyQt5 로그인/회원가입 UI
- OpenCV 웹캠 30fps 렌더링
- 5fps 프레임 base64 인코딩 및 AI 서버 전송
- 집중도 게이지, 상태 뱃지, 자세 오버레이
- 메인서버 목표/세션/로그/통계 API 연동
- Phase 2 라벨링 데이터 수집 UI

## 권장 구조

```text
client/
├─ app/
│  ├─ api/             # AI 서버, 메인서버 통신 클라이언트
│  ├─ ui/              # PyQt 화면
│  ├─ camera/          # OpenCV 캡처와 프레임 처리
│  ├─ state/           # JWT, 세션 상태 관리
│  └─ widgets/
├─ assets/             # 클라이언트 UI 이미지, 아이콘
└─ tests/
```

## 연동 대상

- AI 서버: `/analyze/frame`, `/baseline/capture`, `/traindata`
- 메인서버: `/auth/*`, `/goal`, `/session/*`, `/focus`, `/posture`, `/stats/*`
