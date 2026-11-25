use oceanbase;
create user if not exists 'admin' IDENTIFIED BY 'admin';
grant all on *.* to 'admin' WITH GRANT OPTION;
system sleep 5;
ANALYZE TABLE OCEANBASE.__ALL_VIRTUAL_CORE_ALL_TABLE;
ANALYZE TABLE OCEANBASE.__ALL_VIRTUAL_CORE_COLUMN_TABLE;
