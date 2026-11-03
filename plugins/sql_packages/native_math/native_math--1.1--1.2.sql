-- Copyright (c) 2026 OceanBase.
-- Licensed under the Apache License, Version 2.0 (the "License");

ALTER FUNCTION native_add_one COMMENT 'native_math 1.2';

-- Replacement deliberately changes both identity and the omitted-argument
-- result (42 -> 100). Cached calls and grants must not retain the old object.
DROP FUNCTION native_increment;

CREATE FUNCTION native_increment(input_value BIGINT DEFAULT 99)
RETURNS BIGINT
NO SQL
SQL SECURITY INVOKER
AS 'MODULE_PATHNAME', 'org.seekdb.sql-extension.function.unnamed-add-one' LANGUAGE C;
