-- 1단계: bottle_id, model_id NULL 허용
-- AI서버가 해당 필드를 보내지 않으므로 NOT NULL 제약 해제

ALTER TABLE inspections
    MODIFY bottle_id INT NULL,
    MODIFY model_id  INT NULL;

ALTER TABLE assemblies
    MODIFY bottle_id INT NULL;
