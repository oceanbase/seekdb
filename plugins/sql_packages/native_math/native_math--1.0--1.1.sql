-- Copyright (c) 2026 OceanBase.
-- Licensed under the Apache License, Version 2.0 (the "License");

ALTER FUNCTION native_add_one COMMENT 'native_math 1.1';

CREATE FUNCTION native_successor(input_value BIGINT)
RETURNS BIGINT
DETERMINISTIC
NO SQL
SQL SECURITY INVOKER
AS 'MODULE_PATHNAME', 'org.seekdb.sql-extension.function.native-add-one' LANGUAGE C;
