if ($local_conn_id == null)
{
  --echo local_conn_id should not be null
  --exit
}
let $proxy_ip = deploy_get_value(ob1.proxy0.ip);
let $proxy_port = deploy_get_value(ob1.proxy0.port);

connect (conn100,$proxy_ip,admin@obproxy#MetaDataBase,admin,test,$proxy_port);
connection conn100;

let $query_ip = query_get_value(show proxysession attribute $local_conn_id like "server_ip", value, 1);
let $query_port = query_get_value(show proxysession attribute $local_conn_id like "server_port", value, 1);

let $addr = deploy_get_value(ob1.select select concat(svr_ip,':',svr_port) v from __all_server where svr_ip='$query_ip' and inner_port=$query_port,v,1);

let $obs = deploy_get_value(ob1.get_obs_by_addr $addr);
let $zone = deploy_get_value(ob1.get_zone_by_addr $addr);
