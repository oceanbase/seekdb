-- Copyright (c) 2026 OceanBase.
-- Licensed under the Apache License, Version 2.0 (the "License");

CREATE FUNCTION native_add_one(input_value BIGINT)
RETURNS BIGINT
DETERMINISTIC
NO SQL
SQL SECURITY INVOKER
AS 'MODULE_PATHNAME', 'org.seekdb.sql-extension.function.native-add-one' LANGUAGE C;

CREATE FUNCTION native_increment(input_value BIGINT DEFAULT 41)
RETURNS BIGINT
NO SQL
SQL SECURITY INVOKER
AS 'MODULE_PATHNAME', 'org.seekdb.sql-extension.function.unnamed-add-one' LANGUAGE C;
