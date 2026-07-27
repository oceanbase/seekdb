connect (obsys,$OBMYSQL_MS0,admin,$OBMYSQL_PWD,test,$OBMYSQL_PORT);
connection obsys;
# trigger a major freeze for the local database
--disable_warnings
alter system major freeze;
--enable_warnings
disconnect obsys;
connection default;
