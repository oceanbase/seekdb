connect (obsys_for_external_table,$OBMYSQL_MS0,admin,$OBMYSQL_PWD,oceanbase,$OBMYSQL_PORT);
connection obsys_for_external_table;

--disable_query_log
let $HOST_IP = query_get_value('select host_ip()',host_ip(), 1);

let $DATA_FOLDER_NAME = mysqltest_data_for_external_table;

let $SCP_CMD = ssh $HOST_IP 'mkdir -p "$OBSERVER_DIR/$DATA_FOLDER_NAME"' && scp -r $PWD/mysql_test/test_suite/external_table/data/* $HOST_IP:"$OBSERVER_DIR/$DATA_FOLDER_NAME";

exec $SCP_CMD;

--enable_query_log
disconnect obsys_for_external_table;
connection default;
