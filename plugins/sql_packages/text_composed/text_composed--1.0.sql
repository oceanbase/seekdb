-- Compose another Extension's public SQL functions; no native module needed.
CREATE FUNCTION seekdb_text_is_ascii(input_text TEXT)
RETURNS TINYINT
DETERMINISTIC
NO SQL
SQL SECURITY INVOKER
RETURN seekdb_byte_count(input_text) = seekdb_char_count(input_text);
