

if($var == null)
{
  --echo which variable do you want ? please set
  --exit
}
if($appname == null)
{
  let $appname = deploy_get_value(ob1.app_name);
}

let $conn_id = query_get_value(select connection_id() as c,c,1);

let $proxy_ip = deploy_get_value(ob1.proxy0.ip);
let $proxy_port = deploy_get_value(ob1.proxy0.port);

connect (conn100,$proxy_ip,root@sys#$appname,,test,$proxy_port);
connection conn100;
let $var_value = query_get_value(show proxysession variables all $conn_id like '$var', value, 1);
