--echo ------------------ multi_cluster_check_succ.inc ------------------
let $full_name=root@sys#$cluster;
#-- echo cluster   :$cluster;
#-- echo full_name :$full_name;
-- echo conn      :$conn;

#let $wait_time=10;
#while($wait_time > 0)
#{
#  let $clusre_exist=query_get_value(show proxycluster like "$cluster", cluster_name, 1);
#  if($clusre_exist == $cluster)
#  {
#    let $wait_time = 0;
#  }
#  if($clusre_exist != $cluster)
#  {
#    sleep 1;
#    des $wait_time;
#  }
#}

connect($conn,$ip,$full_name,,oceanbase,$port);
connection $conn;

show proxyconfig like "config_server_refresh_interval";

let $cluster_name2=query_get_value(select value v from __all_virtual_sys_parameter_stat where name = 'cluster', v, 1);
let $full_name_curr=root@sys#$cluster_name2;
if($full_name_curr != $full_name)
{
  -- echo error result full_name_curr, full_name: $full_name_curr, $full_name;
  -- exit;
}

create table t1(pk int primary key);
insert into t1 values(1);
insert into t1 values(2);
select * from t1;
drop table t1;

source sql/check_ddl.sql;
--echo ---------------------------------------------------------------------

