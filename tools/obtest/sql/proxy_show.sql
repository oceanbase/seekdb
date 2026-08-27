--echo ------------------ proxy_show.sql ------------------

--disable_query_log
--disable_result_log

show proxystat;
show proxystat like 'proxy__get_pl_after_response_succ_stat';
show proxystat refresh;
show proxystat refresh like 'proxy__get_pl_after_response_succ_stat';

show proxynet thread;

show proxynet connection;
eval show proxynet connection $conn_id;
eval show proxynet connection $conn_id limit 2;

show proxysm;
show proxysm 10;

show proxyconfig;
alter proxyconfig set min_keep_congestion_interval='10s';
show proxyconfig like '%min%';

show proxycluster;
show proxycluster like 'ob1';

show proxymemory;
show proxymemory objpool;

show proxycongestion;
show proxycongestion all;
eval show proxycongestion $appname;
eval show proxycongestion all $appname;

show processlist;
show proxysession;
eval show proxysession attribute $conn_id;
eval show proxysession stat $conn_id;
eval show proxysession variables $conn_id;
eval show proxysession variables all $conn_id;

let $ss_id = query_get_value(show proxysession attribute $conn_id like "ss_id", value, 1);
eval kill proxysession $conn_id $ss_id;
eval kill query $conn_id;
eval kill proxysession $conn_id;
sleep 1;
--error 1094
eval kill $conn_id;

--enable_query_log
--enable_result_log

--echo ------------------------------------