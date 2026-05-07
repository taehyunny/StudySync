# [4조] StudySync DB ERD

## 1. 테이블 관계 요약

| 관계 | 종류 | 설명 |
|---|---|---|
| USER → GOAL | 1:1 | 사용자 1명당 목표 설정 1개 |
| USER → SESSION | 1:N | 사용자 1명이 여러 공부 세션 보유 |
| SESSION → FOCUS_LOG | 1:N | 세션 1개당 5초 단위 집중도 로그 다수 |
| SESSION → POSTURE_LOG | 1:N | 세션 1개당 자세 데이터 다수 |
| SESSION → POSTURE_EVENT | 1:N | 세션 1개당 자세/졸음/이탈 이벤트 다수 |
| USER → TRAIN_DATA | 1:N | 사용자별 재학습 데이터 수집 |

## 2. ERD

```mermaid
erDiagram
    USER ||--|| GOAL : has
    USER ||--o{ SESSION : owns
    SESSION ||--o{ FOCUS_LOG : records
    SESSION ||--o{ POSTURE_LOG : records
    SESSION ||--o{ POSTURE_EVENT : detects
    USER ||--o{ TRAIN_DATA : collects

    USER {
        INTEGER id PK
        TEXT email UK
        TEXT password_hash
        TEXT name
        TEXT created_at
    }

    GOAL {
        INTEGER id PK
        INTEGER user_id FK
        INTEGER daily_goal_min
        INTEGER rest_interval_min
        INTEGER rest_duration_min
    }

    SESSION {
        INTEGER id PK
        INTEGER user_id FK
        TEXT date
        TEXT start_time
        TEXT end_time
        REAL focus_min
        REAL avg_focus
        INTEGER goal_achieved
    }

    FOCUS_LOG {
        INTEGER id PK
        INTEGER session_id FK
        TEXT timestamp
        INTEGER focus_score
        TEXT state
        INTEGER is_absent
        INTEGER is_drowsy
    }

    POSTURE_LOG {
        INTEGER id PK
        INTEGER session_id FK
        TEXT timestamp
        REAL neck_angle
        REAL shoulder_diff
        INTEGER posture_ok
        REAL vs_baseline
    }

    POSTURE_EVENT {
        INTEGER id PK
        TEXT event_id UK
        INTEGER session_id FK
        TEXT event_type
        TEXT severity
        TEXT reason
        TEXT timestamp
        INTEGER timestamp_ms
        TEXT clip_id
        TEXT clip_access
        TEXT clip_ref
        TEXT clip_format
        INTEGER frame_count
        INTEGER retention_days
        INTEGER expires_at_ms
    }

    TRAIN_DATA {
        INTEGER id PK
        INTEGER user_id FK
        TEXT timestamp
        TEXT landmarks_json
        TEXT label
        INTEGER used_for_training
    }
```

## 3. 테이블 상세 정의

### USER

| 컬럼명 | 타입 | 제약조건 | 설명 |
|---|---|---|---|
| id | INTEGER | PK, AUTO | 사용자 고유 ID |
| email | TEXT | UNIQUE, NOT NULL | 로그인 이메일 |
| password_hash | TEXT | NOT NULL | bcrypt 해시 비밀번호 |
| name | TEXT | NOT NULL | 사용자 이름 |
| created_at | TEXT | DEFAULT NOW | 가입 일시 |

### GOAL

| 컬럼명 | 타입 | 제약조건 | 설명 |
|---|---|---|---|
| id | INTEGER | PK | 목표 ID |
| user_id | INTEGER | FK → USER.id | 사용자 참조 |
| daily_goal_min | INTEGER | DEFAULT 120 | 하루 목표 공부 시간(분) |
| rest_interval_min | INTEGER | DEFAULT 50 | 휴식 주기(분) |
| rest_duration_min | INTEGER | DEFAULT 10 | 휴식 시간(분) |

### SESSION

| 컬럼명 | 타입 | 제약조건 | 설명 |
|---|---|---|---|
| id | INTEGER | PK | 세션 ID |
| user_id | INTEGER | FK → USER.id | 사용자 참조 |
| date | TEXT | NOT NULL | 공부 날짜 |
| start_time | TEXT | NOT NULL | 세션 시작 시각 |
| end_time | TEXT | NULL 허용 | 세션 종료 시각 |
| focus_min | REAL | DEFAULT 0 | 실제 집중 시간(분) |
| avg_focus | REAL | DEFAULT 0 | 평균 집중도 점수 |
| goal_achieved | INTEGER | DEFAULT 0 | 목표 달성 여부 |

### FOCUS_LOG

| 컬럼명 | 타입 | 제약조건 | 설명 |
|---|---|---|---|
| id | INTEGER | PK | 로그 ID |
| session_id | INTEGER | FK → SESSION.id | 세션 참조 |
| timestamp | TEXT | NOT NULL | 기록 시각 |
| focus_score | INTEGER | 0~100 | 집중도 점수 |
| state | TEXT | NOT NULL | 집중/딴짓/졸음 |
| is_absent | INTEGER | DEFAULT 0 | 자리이탈 여부 |
| is_drowsy | INTEGER | DEFAULT 0 | 졸음 여부 |

### POSTURE_LOG

| 컬럼명 | 타입 | 제약조건 | 설명 |
|---|---|---|---|
| id | INTEGER | PK | 로그 ID |
| session_id | INTEGER | FK → SESSION.id | 세션 참조 |
| timestamp | TEXT | NOT NULL | 기록 시각 |
| neck_angle | REAL | NOT NULL | 목 각도 |
| shoulder_diff | REAL | NOT NULL | 어깨 기울기 차이 |
| posture_ok | INTEGER | DEFAULT 1 | 자세 정상 여부 |
| vs_baseline | REAL | DEFAULT 0 | 기준 자세 대비 변화량 |

### POSTURE_EVENT

`POSTURE_EVENT`는 `POSTURE_LOG`와 역할이 다릅니다. `POSTURE_LOG`는 주기적으로 쌓이는 자세 수치 로그이고, `POSTURE_EVENT`는 기준치를 넘은 특정 사건(자세 경고, 졸음, 자리이탈 등)을 기록합니다.

영상 원본은 개인정보 보호를 위해 기본적으로 메인서버에 저장하지 않습니다. 클라이언트는 이벤트 클립을 로컬에 3일간 보관하고, 메인서버에는 `clip_id`, `clip_access`, `expires_at_ms` 같은 메타데이터만 전송합니다.

| 컬럼명 | 타입 | 제약조건 | 설명 |
|---|---|---|---|
| id | INTEGER | PK | 이벤트 DB ID |
| event_id | TEXT | UNIQUE, NOT NULL | 중복 저장 방지용 이벤트 ID |
| session_id | INTEGER | FK → SESSION.id | 세션 참조 |
| event_type | TEXT | NOT NULL | bad_posture / drowsy / absent / rest_required |
| severity | TEXT | DEFAULT warning | info / warning / critical |
| reason | TEXT | NULL 허용 | 이벤트 발생 사유 |
| timestamp | TEXT | NOT NULL | ISO8601 이벤트 발생 시각 |
| timestamp_ms | INTEGER | NOT NULL | 프레임 매칭용 epoch milliseconds |
| clip_id | TEXT | NULL 허용 | 클라이언트 로컬 클립 식별자 |
| clip_access | TEXT | NOT NULL | none / local_only / uploaded_url |
| clip_ref | TEXT | NULL 허용 | local_only이면 로컬 참조값, uploaded_url이면 접근 URL |
| clip_format | TEXT | NULL 허용 | jpeg_sequence / mp4 / none |
| frame_count | INTEGER | DEFAULT 0 | 이벤트 클립 프레임 수 |
| retention_days | INTEGER | DEFAULT 3 | 로컬 보관 일수 |
| expires_at_ms | INTEGER | NULL 허용 | 로컬 클립 만료 시각 |

### TRAIN_DATA

| 컬럼명 | 타입 | 제약조건 | 설명 |
|---|---|---|---|
| id | INTEGER | PK | 데이터 ID |
| user_id | INTEGER | FK → USER.id | 사용자 참조 |
| timestamp | TEXT | NOT NULL | 수집 시각 |
| landmarks_json | TEXT | NOT NULL | 33개 랜드마크 JSON |
| label | TEXT | NOT NULL | 집중/딴짓/졸음 |
| used_for_training | INTEGER | DEFAULT 0 | 학습 사용 여부 |

