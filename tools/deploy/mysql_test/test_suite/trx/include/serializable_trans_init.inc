connect (conn1,$OBMYSQL_MS0,$OBMYSQL_USR,$OBMYSQL_PWD,TEST,$OBMYSQL_PORT);
connect (conn2,$OBMYSQL_MS0,$OBMYSQL_USR,$OBMYSQL_PWD,TEST,$OBMYSQL_PORT);

connection conn1;
set autocommit=0;

CREATE TABLE t1 (i int primary key, c varchar(50));

# Initialize table.
INSERT INTO t1 VALUES (1, 'a');
INSERT INTO t1 VALUES (2, 'b');

commit;

connection conn2;
set autocommit=0;
