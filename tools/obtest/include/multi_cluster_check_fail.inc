--echo ------------------ multi_cluster_check_fail.inc ------------------
let $full_name=root@sys#$cluster;
#-- echo cluster   :$cluster;
#-- echo full_name :$full_name;
-- echo conn      :$conn;

--disable_query_log
--disable_result_log
--error 0,1045
connect($conn,$ip,$full_name,,,$port);
--enable_query_log
--enable_result_log

--echo ---------------------------------------------------------------------

