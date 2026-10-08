-- Preserve the text_ops dependency while adding a second composed function.
CREATE FUNCTION seekdb_text_extra_bytes(input_text TEXT)
RETURNS BIGINT
DETERMINISTIC
NO SQL
SQL SECURITY INVOKER
RETURN seekdb_byte_count(input_text) - seekdb_char_count(input_text);
