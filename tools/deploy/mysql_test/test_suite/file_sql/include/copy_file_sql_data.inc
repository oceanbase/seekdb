# mysqltest.py starts the server on the same host and exports its base directory.
let $FILE_SQL_DATA_DIR = $OBSERVER_DIR/data_for_mysqltest_file_sql;
--exec mkdir -p "$FILE_SQL_DATA_DIR" && rm -f "$FILE_SQL_DATA_DIR/export.csv" && cp mysql_test/test_suite/file_sql/data/basic.csv mysql_test/test_suite/file_sql/data/basic.jsonl mysql_test/test_suite/file_sql/data/invalid.txt "$FILE_SQL_DATA_DIR/" && base64 -d mysql_test/test_suite/file_sql/data/simple.parquet.b64 > "$FILE_SQL_DATA_DIR/simple.parquet"
