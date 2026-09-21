-- Additive update example. A fresh VERSION '1.1' installation reads the 1.0
-- base followed by this file; no duplicated text_ops--1.1.sql is necessary.
CREATE FUNCTION seekdb_is_empty(value LONGTEXT)
RETURNS TINYINT
DETERMINISTIC
NO SQL
SQL SECURITY INVOKER
RETURN CHAR_LENGTH(value) = 0;
