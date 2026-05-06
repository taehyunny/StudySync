-- ============================================================================
-- Factory DB 스키마 (로컬 테스트 / 초기 설치용)
-- ============================================================================
-- 사용법:
--   mysql -u factorymanager -p1234 Factory < schema.sql
--
-- 포함 테이블:
--   inspections   — 검사 결과 (v0.9.0+ heatmap_path / pred_mask_path 포함)
--   assemblies    — Station2 조립 검사 상세
--   models        — AI 모델 버전 관리
--   users         — 로그인 계정 (bcrypt 해시)
--   bottles       — 용기 상태 추적 (향후)
-- ============================================================================

CREATE DATABASE IF NOT EXISTS Factory DEFAULT CHARACTER SET utf8mb4;
USE Factory;

-- ─────────────────────────────────────────────────────────────────────────
-- inspections (검사 결과 — OK/NG 공통)
-- ─────────────────────────────────────────────────────────────────────────
CREATE TABLE IF NOT EXISTS inspections (
    id              INT AUTO_INCREMENT PRIMARY KEY,
    station_id      INT NOT NULL,
    bottle_id       INT NULL,
    model_id        INT NULL,
    timestamp       DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    result          ENUM('ok','ng') NOT NULL,
    confidence      FLOAT NULL,
    defect_type     VARCHAR(100) NULL,
    image_path      VARCHAR(255) NULL,
    heatmap_path    VARCHAR(255) NULL,   -- v0.9.0+
    pred_mask_path  VARCHAR(255) NULL,   -- v0.9.0+
    latency_ms      INT NOT NULL,
    INDEX idx_station (station_id),
    INDEX idx_result  (result),
    INDEX idx_time    (timestamp)
);

-- ─────────────────────────────────────────────────────────────────────────
-- assemblies (Station2 조립 검사 상세)
-- ─────────────────────────────────────────────────────────────────────────
CREATE TABLE IF NOT EXISTS assemblies (
    id              INT AUTO_INCREMENT PRIMARY KEY,
    inspection_id   INT NOT NULL,
    bottle_id       INT NULL,
    cap_ok          TINYINT(1) NOT NULL,
    label_ok        TINYINT(1) NOT NULL,
    fill_ok         TINYINT(1) NOT NULL,
    yolo_detections JSON NOT NULL,
    patchcore_score FLOAT NOT NULL,
    timestamp       DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    INDEX idx_inspection (inspection_id)
);

-- ─────────────────────────────────────────────────────────────────────────
-- models (AI 모델 버전 관리)
--   기획서 v0.12 ERD 기준. v0.15.1 에서 DB 와 schema.sql 정합성 맞춤.
--   - file_path (이전 model_path 아님)
--   - model_type 은 ENUM 으로 값 범위 DB 레벨 검증
--   - trained_by FK: 누가 이 모델을 학습 등록했는가 (NULL 허용 — AI 자동 학습)
-- ─────────────────────────────────────────────────────────────────────────
CREATE TABLE IF NOT EXISTS models (
    id           INT AUTO_INCREMENT PRIMARY KEY,
    station_id   INT NOT NULL,
    model_type   ENUM('PatchCore','YOLO11') NOT NULL,
    version      VARCHAR(20) NOT NULL,
    accuracy     FLOAT NULL,
    file_path    VARCHAR(255) NOT NULL,
    deployed_at  DATETIME NULL DEFAULT CURRENT_TIMESTAMP,
    is_active    TINYINT(1) NOT NULL DEFAULT 0,
    trained_by   INT NULL,
    INDEX idx_station_active (station_id, is_active),
    INDEX idx_trained_by (trained_by),
    CONSTRAINT fk_models_trained_by
        FOREIGN KEY (trained_by) REFERENCES users(id) ON DELETE SET NULL
);

-- ─────────────────────────────────────────────────────────────────────────
-- users (로그인 계정 — bcrypt 해시)
-- ─────────────────────────────────────────────────────────────────────────
CREATE TABLE IF NOT EXISTS users (
    id             INT AUTO_INCREMENT PRIMARY KEY,
    employee_id    VARCHAR(20) NOT NULL,
    username       VARCHAR(50) UNIQUE NOT NULL,
    password_hash  VARCHAR(255) NOT NULL,
    role           VARCHAR(20) NOT NULL,
    created_at     DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    last_login_at  DATETIME NULL
);

-- ─────────────────────────────────────────────────────────────────────────
-- bottles (용기 상태 추적 — 향후 확장용)
-- ─────────────────────────────────────────────────────────────────────────
CREATE TABLE IF NOT EXISTS bottles (
    id          INT AUTO_INCREMENT PRIMARY KEY,
    code        VARCHAR(50) UNIQUE NULL,
    status      VARCHAR(20) NOT NULL DEFAULT 'pending',
    created_at  DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at  DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP
);

-- ─────────────────────────────────────────────────────────────────────────
-- 초기 관리자 계정 (로컬 테스트용)
-- 비밀번호: "admin123" 의 bcrypt 해시 (로컬 테스트만 — 실서버에서는 직접 등록)
-- bcrypt $2b$12$... 형식. 서버가 검증 시 PasswordHash::verify 사용.
-- ─────────────────────────────────────────────────────────────────────────
INSERT IGNORE INTO users (employee_id, username, password_hash, role)
VALUES ('EMP-LOCAL', 'admin', '$2b$12$0AzNHhCkFvNKZ/2rEDt8auON.dOx.0BLDVUZlfZBwuDH6BTvIZk1W', 'Admin');

-- 확인용 쿼리 (row_count — MariaDB 예약어 'rows' 충돌 방지)
SELECT 'inspections' AS tbl, COUNT(*) AS row_count FROM inspections
UNION ALL SELECT 'assemblies', COUNT(*) FROM assemblies
UNION ALL SELECT 'models',     COUNT(*) FROM models
UNION ALL SELECT 'users',      COUNT(*) FROM users
UNION ALL SELECT 'bottles',    COUNT(*) FROM bottles;
