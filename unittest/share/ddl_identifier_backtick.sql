-- Regression scenario for #1469. Run with a MySQL-compatible client without
-- --force, against a disposable database on a server built from this branch:
-- obclient -h127.0.0.1 -P2881 -uroot test < unittest/share/ddl_identifier_backtick.sql
-- Both CREATE UNIQUE INDEX statements must succeed. The final count is 2;
-- the ordered values are (1, a, a) and (2, b, b).
CREATE TABLE ddl_identifier_backtick (
  id INT PRIMARY KEY,
  `e``num` ENUM('a', 'b'),
  `s``et` SET('a', 'b')
);
INSERT INTO ddl_identifier_backtick VALUES (1, 'a', 'a'), (2, 'b', 'b');
CREATE UNIQUE INDEX enum_backtick_idx ON ddl_identifier_backtick (`e``num`);
CREATE UNIQUE INDEX set_backtick_idx ON ddl_identifier_backtick (`s``et`);
SELECT COUNT(*) FROM ddl_identifier_backtick;
SELECT id, `e``num`, `s``et` FROM ddl_identifier_backtick ORDER BY id;
DROP TABLE ddl_identifier_backtick;
