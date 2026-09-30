CREATE FUNCTION `rust_text_length`(`input_text` TEXT)
RETURNS BIGINT
DETERMINISTIC
NO SQL
SQL SECURITY INVOKER
RETURN `seekdb_rust_char_count`(`input_text`);

CREATE FUNCTION rust_unicode_length(input_text TEXT)
RETURNS BIGINT
DETERMINISTIC
NO SQL
SQL SECURITY INVOKER
RETURN seekdb_rust_char_count(seekdb_rust_text(input_text));
