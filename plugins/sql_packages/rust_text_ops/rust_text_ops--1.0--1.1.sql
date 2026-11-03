CREATE FUNCTION rust_text_nonempty(input_text TEXT)
RETURNS BIGINT
DETERMINISTIC
NO SQL
SQL SECURITY INVOKER
RETURN seekdb_rust_char_count(input_text) > 0;
