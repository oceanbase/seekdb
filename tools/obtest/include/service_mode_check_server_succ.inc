--echo ------------------ service_mode_check_server_succ.inc ------------------


select database();
let $conn_id0 = query_get_value(select connection_id() as c,c,1);

let $row = 0;
let $try_times = 10;
while ($try_times > 1)
{
  let $row = math(($row+1)%2 + 1);
  let $conn_id = query_get_value(show processlist, Id, $row);
  if ($conn_id != $conn_id0)
  {
    let $try_times = 1;
  }
  if ($conn_id == $conn_id0)
  {
    let $try_times = math($try_times - 1);
  }
}

if ($conn_id != $conn_id0)
{
  if ($conn_id > $max_proxy_conn_id)
  {
  --echo --error conn_id, $conn_id, $max_proxy_conn_id
  }

  let $cs_id = query_get_value(show proxysession attribute $conn_id like "cs_id", value, 1);
  if ($cs_id > $max_proxy_conn_id)
  {
  --echo --error cs_id, $cs_id, $max_proxy_conn_id
  }
  if ($cs_id != $conn_id)
  {
  --echo --error cs_id, conn_id, $cs_id, &conn_id
  }

  --disable_query_log
  --disable_all_result_log

  --error 0
  eval kill query $conn_id;
  sleep 0.5;
  --error 0
  eval kill query $cs_id;
  sleep 0.5;
  --error 0
  eval kill $conn_id;
  sleep 1;
  --error 1094
  eval kill $cs_id;

  --enable_all_result_log
  --enable_query_log
}
--echo ---------------------------------------------------------------------

