# Install the fixtures of the type inference and of the malformed files.
# The fixtures live in their own directory, so that the glob of the basic test
# still finds only the four files of the basic fixture.
connect (file_sql_types_setup,$OBMYSQL_MS0,admin,$OBMYSQL_PWD,test,$OBMYSQL_PORT);
connection file_sql_types_setup;

--disable_query_log
let $FILE_SQL_HOST = query_get_value('select host_ip()',host_ip(), 1);
let $FILE_SQL_TYPES_DIR = $OBSERVER_DIR/data_for_mysqltest_file_sql_types;
let $FILE_SQL_TYPES_COPY_CMD = ssh $FILE_SQL_HOST 'rm -rf "$FILE_SQL_TYPES_DIR" && mkdir -p "$FILE_SQL_TYPES_DIR"' && scp -q $PWD/mysql_test/test_suite/file_sql/data_types/* $FILE_SQL_HOST:"$FILE_SQL_TYPES_DIR/";
exec $FILE_SQL_TYPES_COPY_CMD;
--enable_query_log

disconnect file_sql_types_setup;
connection default;
