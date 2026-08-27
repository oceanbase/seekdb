--echo ------------------ proxy_show_not_support.sql ------------------

--disable_query_log
--disable_result_log

--error 1064
show proxystat;
--error 1064
show proxystat like 'proxy__get_pl_after_response_succ_stat';
--error 1064
show proxystat refresh;
--error 1064
show proxystat refresh like 'proxy__get_pl_after_response_succ_stat';

--error 1064
show proxynet thread;

--error 1064
show proxynet connection;

--error 1064
show proxysm;
--error 1064
show proxysm 10;

--error 1064
show proxyconfig;
--error 1064
alter proxyconfig set min_keep_congestion_interval='10s';
--error 1064
show proxyconfig like '%min%';

--error 1064
show proxycluster;
--error 1064
show proxycluster like '%ob1%';

--error 1064
show proxymemory;
--error 1064
show proxymemory objpool;

--error 1064
show proxycongestion;
--error 1064
show proxycongestion all;
--error 1064
eval show proxycongestion ob1.jianhua.sjh;
--error 1064
eval show proxycongestion all ob1.jianhua.sjh;

--error 1064
show proxysession;
--error 1064
eval show proxysession attribute $conn_id;
--error 1064
eval show proxysession stat $conn_id;
--error 1064
eval show proxysession variables $conn_id;
--error 1064
eval show proxysession variables all $conn_id;

--error 1064
eval kill proxysession $conn_id, 1;
--error 1064
eval kill proxysession $conn_id;

--enable_query_log
--enable_result_log

--echo ------------------------------------