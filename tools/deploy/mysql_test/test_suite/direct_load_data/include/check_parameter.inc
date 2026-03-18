# set __parameter_name__ outside
# set __parameter_value__ outside

--disable_query_log
--disable_result_log

connect (root,$OBMYSQL_MS0,root,,test,$OBMYSQL_PORT);
connection root;

let $__i__= 60;
while($__i__ > 0)
{
  sleep 1;
  dec $__i__;
  let $__value__= query_get_value(select value from oceanbase.__all_virtual_tenant_parameter_stat where name = '$__parameter_name__' and (scope = 'CLUSTER' or (scope = 'TENANT')), value, 1);
  if ($__value__ == $__parameter_value__)
  {
    let $__i__ = -5;
  }
}

disconnect root;
connection default;

--enable_query_log
--enable_result_log
