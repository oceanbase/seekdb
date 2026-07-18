
let $server_cnt = 1;


let $sync_cnt = 0;

let $i = 10;
while($i > 0)
{
  sleep 1;
  dec $i;
  let $sync_cnt = query_get_value(select count(*) as cnt from oceanbase.__all_virtual_server_schema_info, cnt, 1);
  if ($sync_cnt == $server_cnt)
  {
    let $i = -4;
  }
}

if ($i != -4)
{
  --echo check tenant sync timeout, sync_cnt:$sync_cnt, server_cnt:$server_cnt
  eval select * from oceanbase.__all_virtual_server_schema_info;
}
