
let $conn_id = query_get_value(select connection_id() as c,c,1);

let $proxy_ip = deploy_get_value(ob1.proxy0.ip);
let $proxy_port = deploy_get_value(ob1.proxy0.port);

connect (conn100,$proxy_ip,admin@obproxy#MetaDataBase,admin,test,$proxy_port);
connection conn100;

let $row=1;
let $max_row=60;
let $need_break=0;
while ($need_break == 0)
{
  let $name = query_get_value(show proxysession attribute $conn_id, attribute_name, $row);
  if ($name==server_ip)
  {
    let $query_ip = query_get_value(show proxysession attribute $conn_id, value, $row);
    let $row=math($row + 1);
    let $query_port = query_get_value(show proxysession attribute $conn_id, value, $row);
    let $need_break=1;
  }
  if ($name!=server_ip)
  {
    let $row=math($row + 1);
    if ($row > $max_row)
    {
      let $need_break=2;
      --echo fail to get last server ip port $conn_id
      --exit
    }
  }
}

let $addr = deploy_get_value(ob1.select select concat(svr_ip,':',svr_port) v from __all_server where svr_ip='$query_ip' and inner_port=$query_port,v,1);

let $obs = deploy_get_value(ob1.get_obs_by_addr $addr);
let $zone = deploy_get_value(ob1.get_zone_by_addr $addr);
