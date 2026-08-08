let $sql_addr = deploy_get_value(ob1.get_last_query_addr sql);
let $addr = deploy_get_value(ob1.get_last_query_addr rpc);
let $obs = deploy_get_value(ob1.get_obs_by_addr $addr);
let $zone = deploy_get_value(ob1.get_zone_by_addr $addr);
