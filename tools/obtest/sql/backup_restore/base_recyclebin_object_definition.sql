CREATE TABLE IF NOT EXISTS base_table_to_flashback(
     col1 int,
     col2 VARCHAR(30),
     key base_index_to_flashback(col2)
);
drop table base_table_to_flashback;

CREATE TABLE IF NOT EXISTS base_table_to_truncate(
     col1 int,
     col2 VARCHAR(30),
     key base_index_to_truncate(col2)
);
truncate table base_table_to_truncate;
rename table base_table_to_truncate to base_table_has_been_truncated;

CREATE VIEW base_view_to_flashback AS SELECT * FROM base_table_has_been_truncated;
drop view base_view_to_flashback;

CREATE DATABASE base_db_to_flashback;
use base_db_to_flashback;
CREATE TABLE IF NOT EXISTS base_table_in_recycle_db(
     col1 int,
     col2 VARCHAR(30),
     key base_index_in_recyle_db(col2)
);
CREATE VIEW base_view_in_recyle_db AS SELECT * FROM base_table_in_recycle_db;
CREATE PROCEDURE base_proc_in_recycle_db() BEGIN END;
CREATE FUNCTION base_func_in_recycle_db() RETURNS INT BEGIN RETURN 1; END;
DROP DATABASE base_db_to_flashback;

CREATE DATABASE base_db_to_flashback_and_rename;
DROP DATABASE base_db_to_flashback_and_rename;

CREATE DATABASE base_db_to_flashback_pl;
use base_db_to_flashback_pl;
CREATE PROCEDURE base_proc_in_recycle_db() BEGIN END;
CREATE FUNCTION base_func_in_recycle_db() RETURNS INT BEGIN RETURN 1; END;
DROP DATABASE base_db_to_flashback_pl;

eval USE $dataDB;
