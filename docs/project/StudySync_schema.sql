-- StudySync database schema
-- Generated from docs/project/[4조] StudySync DB_ERD.md
-- Date: 2026-05-06
--
-- Policy:
-- - Raw webcam/event video is not stored in the main server DB.
-- - posture_events stores only metadata and local_only clip references.
-- - event_id is UNIQUE to make event upload idempotent.

CREATE DATABASE IF NOT EXISTS StudySync
  DEFAULT CHARACTER SET utf8mb4
  DEFAULT COLLATE utf8mb4_unicode_ci;

USE StudySync;

CREATE TABLE IF NOT EXISTS users (
  id BIGINT AUTO_INCREMENT PRIMARY KEY,
  email VARCHAR(255) NOT NULL,
  password_hash VARCHAR(255) NOT NULL,
  name VARCHAR(100) NOT NULL,
  created_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,

  UNIQUE KEY uk_users_email (email)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS goals (
  id BIGINT AUTO_INCREMENT PRIMARY KEY,
  user_id BIGINT NOT NULL,
  daily_goal_min INT NOT NULL DEFAULT 120,
  rest_interval_min INT NOT NULL DEFAULT 50,
  rest_duration_min INT NOT NULL DEFAULT 10,
  updated_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,

  UNIQUE KEY uk_goals_user_id (user_id),
  CONSTRAINT fk_goals_user_id
    FOREIGN KEY (user_id) REFERENCES users(id)
    ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS sessions (
  id BIGINT AUTO_INCREMENT PRIMARY KEY,
  user_id BIGINT NOT NULL,
  date DATE NOT NULL,
  start_time DATETIME NOT NULL,
  end_time DATETIME NULL,
  focus_min FLOAT NOT NULL DEFAULT 0,
  avg_focus FLOAT NOT NULL DEFAULT 0,
  goal_achieved TINYINT(1) NOT NULL DEFAULT 0,

  KEY idx_sessions_user_date (user_id, date),
  KEY idx_sessions_start_time (start_time),
  CONSTRAINT fk_sessions_user_id
    FOREIGN KEY (user_id) REFERENCES users(id)
    ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS focus_logs (
  id BIGINT AUTO_INCREMENT PRIMARY KEY,
  session_id BIGINT NOT NULL,
  ts DATETIME NOT NULL,
  timestamp_ms BIGINT NULL,
  focus_score INT NOT NULL,
  state VARCHAR(20) NOT NULL,
  is_absent TINYINT(1) NOT NULL DEFAULT 0,
  is_drowsy TINYINT(1) NOT NULL DEFAULT 0,

  KEY idx_focus_logs_session_ts (session_id, ts),
  KEY idx_focus_logs_session_timestamp_ms (session_id, timestamp_ms),
  CONSTRAINT fk_focus_logs_session_id
    FOREIGN KEY (session_id) REFERENCES sessions(id)
    ON DELETE CASCADE,
  CONSTRAINT chk_focus_logs_score
    CHECK (focus_score >= 0 AND focus_score <= 100)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS posture_logs (
  id BIGINT AUTO_INCREMENT PRIMARY KEY,
  session_id BIGINT NOT NULL,
  ts DATETIME NOT NULL,
  timestamp_ms BIGINT NULL,
  neck_angle FLOAT NULL,
  shoulder_diff FLOAT NULL,
  posture_ok TINYINT(1) NOT NULL DEFAULT 1,
  vs_baseline FLOAT NULL DEFAULT 0,

  KEY idx_posture_logs_session_ts (session_id, ts),
  KEY idx_posture_logs_session_timestamp_ms (session_id, timestamp_ms),
  CONSTRAINT fk_posture_logs_session_id
    FOREIGN KEY (session_id) REFERENCES sessions(id)
    ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS posture_events (
  id BIGINT AUTO_INCREMENT PRIMARY KEY,
  event_id VARCHAR(128) NOT NULL,
  session_id BIGINT NOT NULL,
  event_type VARCHAR(32) NOT NULL,
  severity VARCHAR(20) NOT NULL DEFAULT 'warning',
  reason TEXT NULL,
  ts DATETIME NOT NULL,
  timestamp_ms BIGINT NOT NULL,

  clip_id VARCHAR(128) NULL,
  clip_access VARCHAR(32) NOT NULL DEFAULT 'local_only',
  clip_ref TEXT NULL,
  clip_format VARCHAR(32) NULL,
  frame_count INT NOT NULL DEFAULT 0,
  retention_days INT NOT NULL DEFAULT 3,
  expires_at_ms BIGINT NULL,

  created_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,

  UNIQUE KEY uk_posture_events_event_id (event_id),
  KEY idx_posture_events_session_ts (session_id, timestamp_ms),
  KEY idx_posture_events_type (event_type),
  KEY idx_posture_events_expires_at_ms (expires_at_ms),
  CONSTRAINT fk_posture_events_session_id
    FOREIGN KEY (session_id) REFERENCES sessions(id)
    ON DELETE CASCADE,
  CONSTRAINT chk_posture_events_clip_access
    CHECK (clip_access IN ('none', 'local_only', 'uploaded_url')),
  CONSTRAINT chk_posture_events_event_type
    CHECK (event_type IN ('bad_posture', 'drowsy', 'absent', 'rest_required')),
  CONSTRAINT chk_posture_events_severity
    CHECK (severity IN ('info', 'warning', 'critical'))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS train_data (
  id BIGINT AUTO_INCREMENT PRIMARY KEY,
  user_id BIGINT NOT NULL,
  ts DATETIME NOT NULL,
  landmarks_json JSON NOT NULL,
  label VARCHAR(20) NOT NULL,
  used_for_training TINYINT(1) NOT NULL DEFAULT 0,
  created_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,

  KEY idx_train_data_user_ts (user_id, ts),
  KEY idx_train_data_label (label),
  KEY idx_train_data_used (used_for_training),
  CONSTRAINT fk_train_data_user_id
    FOREIGN KEY (user_id) REFERENCES users(id)
    ON DELETE CASCADE,
  CONSTRAINT chk_train_data_label
    CHECK (label IN ('focus', 'distracted', 'drowsy'))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- Optional table for StudySync model version tracking.
-- Keep only if the main server owns model metadata.
CREATE TABLE IF NOT EXISTS models (
  id BIGINT AUTO_INCREMENT PRIMARY KEY,
  model_type VARCHAR(50) NOT NULL,
  version VARCHAR(50) NOT NULL,
  accuracy FLOAT NULL,
  file_path TEXT NULL,
  is_active TINYINT(1) NOT NULL DEFAULT 0,
  created_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,

  UNIQUE KEY uk_models_type_version (model_type, version),
  KEY idx_models_type_active (model_type, is_active)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

